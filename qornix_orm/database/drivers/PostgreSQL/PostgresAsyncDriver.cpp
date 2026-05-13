/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "db/postgres_async_driver.h"
#include "db/async_db_errors.h"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <libpq-fe.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <variant>
#include <vector>

namespace qornix::db {

namespace {

struct PgResultHandle {
    PGresult* value{nullptr};

    explicit PgResultHandle(PGresult* result = nullptr) : value(result) {}
    PgResultHandle(const PgResultHandle&) = delete;
    PgResultHandle& operator=(const PgResultHandle&) = delete;
    PgResultHandle(PgResultHandle&& other) noexcept : value(other.value) { other.value = nullptr; }
    PgResultHandle& operator=(PgResultHandle&& other) noexcept {
        if (this != &other) {
            if (value) {
                PQclear(value);
            }
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }
    ~PgResultHandle() {
        if (value) {
            PQclear(value);
        }
    }

    PGresult* get() const noexcept { return value; }
};

std::chrono::steady_clock::time_point min_deadline(
    std::chrono::steady_clock::time_point a,
    std::chrono::steady_clock::time_point b) {
    return a < b ? a : b;
}

std::optional<std::chrono::steady_clock::time_point> operation_deadline(
    const CancellationToken& token,
    std::chrono::milliseconds timeout) {
    std::optional<std::chrono::steady_clock::time_point> deadline;
    if (timeout.count() > 0) {
        deadline = std::chrono::steady_clock::now() + timeout;
    }
    if (token.deadline) {
        deadline = deadline ? std::optional{min_deadline(*deadline, *token.deadline)} : token.deadline;
    }
    return deadline;
}

std::uint64_t parse_affected_rows(const char* value) {
    if (!value || *value == '\0') {
        return 0;
    }
    std::uint64_t result = 0;
    const auto* end = value + std::strlen(value);
    const auto parsed = std::from_chars(value, end, result);
    return parsed.ec == std::errc{} ? result : 0;
}

DbErrorCode map_sql_state(const char* sql_state, ExecStatusType status) {
    if (!sql_state || std::strlen(sql_state) < 2) {
        if (status == PGRES_FATAL_ERROR || status == PGRES_BAD_RESPONSE) {
            return DbErrorCode::QueryRejected;
        }
        return DbErrorCode::Unknown;
    }

    const std::string state(sql_state);
    if (state == "23505") {
        return DbErrorCode::DuplicateKey;
    }
    if (state == "23503") {
        return DbErrorCode::ForeignKeyViolation;
    }
    if (state.rfind("23", 0) == 0) {
        return DbErrorCode::ConstraintViolation;
    }
    if (state == "42601") {
        return DbErrorCode::Syntax;
    }
    if (state == "40001" || state == "40P01") {
        return DbErrorCode::Conflict;
    }
    if (state.rfind("28", 0) == 0) {
        return DbErrorCode::Auth;
    }
    if (state.rfind("08", 0) == 0) {
        return DbErrorCode::ConnectionLost;
    }
    if (state.rfind("53", 0) == 0 || state.rfind("57", 0) == 0) {
        return DbErrorCode::Unavailable;
    }
    return DbErrorCode::QueryRejected;
}

std::string result_error_message(PGconn* conn, PGresult* result) {
    if (result) {
        if (const char* message = PQresultErrorMessage(result); message && *message) {
            return message;
        }
    }
    if (conn) {
        if (const char* message = PQerrorMessage(conn); message && *message) {
            return message;
        }
    }
    return "PostgreSQL async driver error";
}

DbError make_result_error(PGconn* conn, PGresult* result, ExecStatusType status) {
    const char* state = result ? PQresultErrorField(result, PG_DIAG_SQLSTATE) : nullptr;
    return DbError(map_sql_state(state, status), result_error_message(conn, result), state ? state : "");
}

Row parse_row(PGresult* result, int row_index) {
    Row row;
    const int fields = PQnfields(result);
    for (int field_index = 0; field_index < fields; ++field_index) {
        const char* name = PQfname(result, field_index);
        if (!name) {
            continue;
        }
        if (PQgetisnull(result, row_index, field_index)) {
            row.columns.emplace(name, "");
        } else {
            row.columns.emplace(name, PQgetvalue(result, row_index, field_index));
        }
    }
    return row;
}

void append_result_rows(QueryResult& output, PGresult* result, const QueryOptions& options) {
    const int tuples = PQntuples(result);
    for (int row_index = 0; row_index < tuples; ++row_index) {
        if (options.max_rows > 0 && output.rows.size() >= options.max_rows) {
            return;
        }
        output.rows.push_back(parse_row(result, row_index));
        if (options.single_row) {
            return;
        }
    }
}

} // namespace

struct PostgresAsyncDriver::Impl {
    net::any_io_executor executor;
    PostgresAsyncOptions options;
    PGconn* conn{nullptr};
    bool connected{false};
    bool in_flight{false};

    Impl(net::any_io_executor ex, PostgresAsyncOptions opts)
        : executor(std::move(ex)), options(std::move(opts)) {}

    ~Impl() {
        if (conn) {
            PQfinish(conn);
            conn = nullptr;
        }
    }

    std::string error_message() const {
        if (!conn) {
            return "PostgreSQL connection is not initialized";
        }
        const char* message = PQerrorMessage(conn);
        if (message && *message) {
            return message;
        }
        return "PostgreSQL connection error";
    }

    void ensure_connection() const {
        if (!conn || !connected || PQstatus(conn) != CONNECTION_OK) {
            throw DbError(DbErrorCode::ConnectionLost, "PostgreSQL async connection is not open");
        }
    }

    int socket_or_throw() const {
        const int socket = conn ? PQsocket(conn) : -1;
        if (socket < 0) {
            throw DbError(DbErrorCode::ConnectionLost, "PostgreSQL connection does not expose a valid socket");
        }
        return socket;
    }

    net::awaitable<void> wait_socket(
        net::posix::stream_descriptor::wait_type wait_type,
        std::optional<std::chrono::steady_clock::time_point> deadline,
        const CancellationToken& token,
        const char* timeout_message) {
        using namespace boost::asio::experimental::awaitable_operators;

        if (token.is_cancelled()) {
            throw db_cancelled();
        }
        if (deadline && std::chrono::steady_clock::now() >= *deadline) {
            throw db_timeout(timeout_message);
        }

        boost::system::error_code assign_ec;
        net::posix::stream_descriptor descriptor(executor);
        descriptor.assign(socket_or_throw(), assign_ec);
        if (assign_ec) {
            throw DbError(DbErrorCode::ConnectionLost, assign_ec.message());
        }

        struct DescriptorReleaseGuard {
            net::posix::stream_descriptor& descriptor;
            ~DescriptorReleaseGuard() {
                if (descriptor.is_open()) {
                    try {
                        descriptor.release();
                    } catch (...) {
                    }
                }
            }
        } release_guard{descriptor};

        if (deadline) {
            net::steady_timer timer(executor);
            timer.expires_at(*deadline);
            auto result = co_await (
                descriptor.async_wait(wait_type, net::as_tuple(net::use_awaitable)) ||
                timer.async_wait(net::as_tuple(net::use_awaitable)));

            if (result.index() == 1) {
                const auto& [timer_ec] = std::get<1>(result);
                if (!timer_ec) {
                    throw db_timeout(timeout_message);
                }
                if (timer_ec != net::error::operation_aborted) {
                    throw DbError(DbErrorCode::Unknown, timer_ec.message());
                }
            } else {
                const auto& [wait_ec] = std::get<0>(result);
                if (wait_ec && wait_ec != net::error::operation_aborted) {
                    throw DbError(DbErrorCode::ConnectionLost, wait_ec.message());
                }
            }
        } else {
            const auto [wait_ec] = co_await descriptor.async_wait(wait_type, net::as_tuple(net::use_awaitable));
            if (wait_ec) {
                throw DbError(DbErrorCode::ConnectionLost, wait_ec.message());
            }
        }

        if (token.is_cancelled()) {
            throw db_cancelled();
        }
    }

    net::awaitable<void> wait_polling(PostgresPollingStatusType status,
                                      std::optional<std::chrono::steady_clock::time_point> deadline,
                                      const CancellationToken& token) {
        switch (status) {
            case PGRES_POLLING_READING:
                co_await wait_socket(net::posix::stream_descriptor::wait_read, deadline, token,
                                     "PostgreSQL connection timed out while waiting for read readiness");
                break;
            case PGRES_POLLING_WRITING:
                co_await wait_socket(net::posix::stream_descriptor::wait_write, deadline, token,
                                     "PostgreSQL connection timed out while waiting for write readiness");
                break;
            default:
                break;
        }
    }

    net::awaitable<void> wait_read(std::optional<std::chrono::steady_clock::time_point> deadline,
                                   const CancellationToken& token,
                                   const char* timeout_message) {
        co_await wait_socket(net::posix::stream_descriptor::wait_read, deadline, token, timeout_message);
    }

    net::awaitable<void> wait_write(std::optional<std::chrono::steady_clock::time_point> deadline,
                                    const CancellationToken& token,
                                    const char* timeout_message) {
        co_await wait_socket(net::posix::stream_descriptor::wait_write, deadline, token, timeout_message);
    }

    net::awaitable<void> flush_out(std::optional<std::chrono::steady_clock::time_point> deadline,
                                   const CancellationToken& token) {
        while (true) {
            if (token.is_cancelled()) {
                throw db_cancelled();
            }
            const int flush_result = PQflush(conn);
            if (flush_result == 0) {
                co_return;
            }
            if (flush_result < 0) {
                throw DbError(DbErrorCode::ConnectionLost, error_message());
            }
            co_await wait_write(deadline, token, "PostgreSQL query timed out while flushing request");
        }
    }

    net::awaitable<void> consume_input(std::optional<std::chrono::steady_clock::time_point> deadline,
                                       const CancellationToken& token) {
        while (PQisBusy(conn)) {
            if (token.is_cancelled()) {
                throw db_cancelled();
            }
            co_await wait_read(deadline, token, "PostgreSQL query timed out while waiting for response");
            if (PQconsumeInput(conn) == 0) {
                throw DbError(DbErrorCode::ConnectionLost, error_message());
            }
        }
    }

    net::awaitable<QueryResult> collect_results(QueryOptions query_options,
                                                std::optional<std::chrono::steady_clock::time_point> deadline,
                                                const CancellationToken& token) {
        QueryResult output;

        while (true) {
            co_await consume_input(deadline, token);
            PgResultHandle result(PQgetResult(conn));
            if (!result.get()) {
                break;
            }

            const auto status = PQresultStatus(result.get());
            switch (status) {
                case PGRES_TUPLES_OK:
                case PGRES_SINGLE_TUPLE:
                    output.command_tag = PQcmdStatus(result.get()) ? PQcmdStatus(result.get()) : "";
                    append_result_rows(output, result.get(), query_options);
                    break;
                case PGRES_COMMAND_OK:
                    output.command_tag = PQcmdStatus(result.get()) ? PQcmdStatus(result.get()) : "";
                    output.affected_rows += parse_affected_rows(PQcmdTuples(result.get()));
                    break;
                case PGRES_EMPTY_QUERY:
                    output.command_tag = "EMPTY";
                    break;
                default:
                    throw make_result_error(conn, result.get(), status);
            }
        }

        co_return output;
    }

    void throw_if_busy() const {
        if (in_flight) {
            throw DbError(DbErrorCode::QueryRejected, "PostgreSQL async driver already has an operation in flight");
        }
    }

    struct InFlightGuard {
        Impl& impl;
        explicit InFlightGuard(Impl& i) : impl(i) { impl.in_flight = true; }
        ~InFlightGuard() { impl.in_flight = false; }
    };

    net::awaitable<QueryResult> run_sql(std::string sql,
                                        QueryParams params,
                                        QueryOptions query_options,
                                        CancellationToken token) {
        ensure_connection();
        throw_if_busy();
        InFlightGuard guard(*this);

        const auto timeout = query_options.timeout.count() > 0 ? query_options.timeout : options.default_query_timeout;
        const auto deadline = operation_deadline(token, timeout);

        std::vector<const char*> values;
        std::vector<int> lengths;
        std::vector<int> formats;
        values.reserve(params.size());
        lengths.reserve(params.size());
        formats.reserve(params.size());
        for (const auto& param : params) {
            if (param.is_null) {
                values.push_back(nullptr);
                lengths.push_back(0);
            } else {
                values.push_back(param.value.c_str());
                lengths.push_back(static_cast<int>(param.value.size()));
            }
            formats.push_back(0);
        }

        const int parameter_count = static_cast<int>(params.size());
        int sent = 0;
        if (query_options.prepared || !query_options.statement_name.empty()) {
            if (query_options.statement_name.empty()) {
                throw DbError(DbErrorCode::QueryRejected, "prepared query requires statement_name");
            }
            sent = PQsendQueryPrepared(conn,
                                       query_options.statement_name.c_str(),
                                       parameter_count,
                                       values.empty() ? nullptr : values.data(),
                                       lengths.empty() ? nullptr : lengths.data(),
                                       formats.empty() ? nullptr : formats.data(),
                                       0);
        } else {
            sent = PQsendQueryParams(conn,
                                     sql.c_str(),
                                     parameter_count,
                                     nullptr,
                                     values.empty() ? nullptr : values.data(),
                                     lengths.empty() ? nullptr : lengths.data(),
                                     formats.empty() ? nullptr : formats.data(),
                                     0);
        }

        if (sent != 1) {
            throw DbError(DbErrorCode::QueryRejected, error_message());
        }
        if (query_options.single_row) {
            PQsetSingleRowMode(conn);
        }

        try {
            co_await flush_out(deadline, token);
            co_return co_await collect_results(std::move(query_options), deadline, token);
        } catch (const DbError& error) {
            if (error.code() == DbErrorCode::Timeout || error.code() == DbErrorCode::Cancelled) {
                PQrequestCancel(conn);
                connected = false;
            }
            throw;
        }
    }
};

PostgresAsyncDriver::PostgresAsyncDriver(net::any_io_executor executor, PostgresAsyncOptions options)
    : impl_(std::make_unique<Impl>(std::move(executor), std::move(options))) {}

PostgresAsyncDriver::~PostgresAsyncDriver() = default;

net::awaitable<void> PostgresAsyncDriver::connect(const CancellationToken& token) {
    if (impl_->conn) {
        PQfinish(impl_->conn);
        impl_->conn = nullptr;
        impl_->connected = false;
    }

    if (impl_->options.connection_info.empty()) {
        throw DbError(DbErrorCode::Connection, "PostgreSQL async connection string is empty");
    }

    impl_->conn = PQconnectStart(impl_->options.connection_info.c_str());
    if (!impl_->conn) {
        throw DbError(DbErrorCode::Connection, "PQconnectStart failed");
    }
    if (PQstatus(impl_->conn) == CONNECTION_BAD) {
        throw DbError(DbErrorCode::Connection, impl_->error_message());
    }
    if (PQsetnonblocking(impl_->conn, 1) != 0) {
        throw DbError(DbErrorCode::Connection, impl_->error_message());
    }

    const auto deadline = operation_deadline(token, impl_->options.connect_timeout);
    while (true) {
        if (token.is_cancelled()) {
            throw db_cancelled("PostgreSQL connection cancelled");
        }
        const auto status = PQconnectPoll(impl_->conn);
        if (status == PGRES_POLLING_OK) {
            impl_->connected = true;
            co_return;
        }
        if (status == PGRES_POLLING_FAILED) {
            throw DbError(DbErrorCode::Connection, impl_->error_message());
        }
        co_await impl_->wait_polling(status, deadline, token);
    }
}

net::awaitable<void> PostgresAsyncDriver::close() {
    if (impl_->conn) {
        PQfinish(impl_->conn);
        impl_->conn = nullptr;
    }
    impl_->connected = false;
    co_return;
}

bool PostgresAsyncDriver::is_open() const {
    return impl_ && impl_->conn && impl_->connected && PQstatus(impl_->conn) == CONNECTION_OK;
}

std::string PostgresAsyncDriver::driver_name() const {
    return "postgres_async";
}

net::awaitable<QueryResult> PostgresAsyncDriver::query(
    std::string sql,
    QueryParams params,
    QueryOptions options,
    CancellationToken token) {
    co_return co_await impl_->run_sql(std::move(sql), std::move(params), std::move(options), std::move(token));
}

net::awaitable<QueryResult> PostgresAsyncDriver::execute(
    std::string sql,
    QueryParams params,
    QueryOptions options,
    CancellationToken token) {
    co_return co_await impl_->run_sql(std::move(sql), std::move(params), std::move(options), std::move(token));
}

net::awaitable<void> PostgresAsyncDriver::prepare(std::string name, std::string sql, CancellationToken token) {
    impl_->ensure_connection();
    impl_->throw_if_busy();
    Impl::InFlightGuard guard(*impl_);
    const auto deadline = operation_deadline(token, impl_->options.default_query_timeout);

    if (name.empty()) {
        throw DbError(DbErrorCode::QueryRejected, "prepared statement name cannot be empty");
    }
    if (PQsendPrepare(impl_->conn, name.c_str(), sql.c_str(), 0, nullptr) != 1) {
        throw DbError(DbErrorCode::QueryRejected, impl_->error_message());
    }

    try {
        co_await impl_->flush_out(deadline, token);
        QueryOptions options;
        co_await impl_->collect_results(std::move(options), deadline, token);
    } catch (const DbError& error) {
        if (error.code() == DbErrorCode::Timeout || error.code() == DbErrorCode::Cancelled) {
            PQrequestCancel(impl_->conn);
            impl_->connected = false;
        }
        throw;
    }
}

net::awaitable<void> PostgresAsyncDriver::begin(CancellationToken token) {
    QueryOptions options;
    options.timeout = impl_->options.default_query_timeout;
    co_await execute("BEGIN", {}, std::move(options), std::move(token));
}

net::awaitable<void> PostgresAsyncDriver::commit(CancellationToken token) {
    QueryOptions options;
    options.timeout = impl_->options.default_query_timeout;
    co_await execute("COMMIT", {}, std::move(options), std::move(token));
}

net::awaitable<void> PostgresAsyncDriver::rollback(CancellationToken token) {
    QueryOptions options;
    options.timeout = impl_->options.default_query_timeout;
    co_await execute("ROLLBACK", {}, std::move(options), std::move(token));
}

AsyncDriverFactory make_postgres_async_driver_factory(PostgresAsyncOptions options) {
    return [options = std::move(options)](net::any_io_executor executor) mutable {
        return std::make_unique<PostgresAsyncDriver>(std::move(executor), options);
    };
}

} // namespace qornix::db
