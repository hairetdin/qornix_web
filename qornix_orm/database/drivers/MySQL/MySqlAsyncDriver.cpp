/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "db/mysql_async_driver.h"
#include "db/async_db_errors.h"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/mysql.hpp>
#include <boost/mysql/diagnostics.hpp>
#include <boost/mysql/error_code.hpp>
#include <boost/mysql/field_view.hpp>
#include <boost/mysql/handshake_params.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/ssl_mode.hpp>
#include <boost/mysql/statement.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace qornix::db {

namespace mysql = boost::mysql;
namespace ip = boost::asio::ip;

namespace {

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

void throw_if_deadline_expired(
    const std::optional<std::chrono::steady_clock::time_point>& deadline,
    const char* message) {
    if (deadline && std::chrono::steady_clock::now() >= *deadline) {
        throw db_timeout(message);
    }
}

std::string from_mysql_string(mysql::string_view value) {
    return std::string(value.data(), value.size());
}

std::string field_to_string(mysql::field_view field) {
    if (field.is_null()) {
        return "";
    }
    if (field.is_string()) {
        return from_mysql_string(field.get_string());
    }
    if (field.is_int64()) {
        return std::to_string(field.get_int64());
    }
    if (field.is_uint64()) {
        return std::to_string(field.get_uint64());
    }
    if (field.is_float()) {
        return std::to_string(field.get_float());
    }
    if (field.is_double()) {
        return std::to_string(field.get_double());
    }
    if (field.is_blob()) {
        const auto blob = field.get_blob();
        return std::string(reinterpret_cast<const char*>(blob.data()), blob.size());
    }
    std::ostringstream out;
    if (field.is_date()) {
        const auto date = field.get_date();
        out << date.year() << '-' << static_cast<unsigned>(date.month()) << '-' << static_cast<unsigned>(date.day());
        return out.str();
    }
    if (field.is_datetime()) {
        const auto dt = field.get_datetime();
        out << dt.year() << '-'
            << static_cast<unsigned>(dt.month()) << '-'
            << static_cast<unsigned>(dt.day()) << ' '
            << static_cast<unsigned>(dt.hour()) << ':'
            << static_cast<unsigned>(dt.minute()) << ':'
            << static_cast<unsigned>(dt.second());
        return out.str();
    }
    if (field.is_time()) {
        return std::to_string(field.get_time().count());
    }
    return "";
}

DbErrorCode map_mysql_error(const boost::system::error_code& ec) {
    if (!ec) {
        return DbErrorCode::Unknown;
    }
    const auto value = ec.value();
    if (value == 1045) {
        return DbErrorCode::Auth;
    }
    if (value == 1062) {
        return DbErrorCode::DuplicateKey;
    }
    if (value == 1064) {
        return DbErrorCode::Syntax;
    }
    if (value == 1213 || value == 1205) {
        return DbErrorCode::Conflict;
    }
    if (value == 1451 || value == 1452) {
        return DbErrorCode::ForeignKeyViolation;
    }
    if (value >= 2000 && value < 3000) {
        return DbErrorCode::ConnectionLost;
    }
    return DbErrorCode::QueryRejected;
}

std::string diagnostic_message(const boost::system::error_code& ec, const mysql::diagnostics& diag) {
    std::string message = ec.message();
    const auto server_message = diag.server_message();
    if (!server_message.empty()) {
        message += ": ";
        message += from_mysql_string(server_message);
    }
    return message;
}

DbError make_mysql_error(const boost::system::error_code& ec, const mysql::diagnostics& diag) {
    return DbError(map_mysql_error(ec), diagnostic_message(ec, diag));
}

struct ParsedMySqlUrl {
    std::string username;
    std::string password;
    std::string host;
    std::string port;
    std::string database;
};

ParsedMySqlUrl parse_mysql_url(const std::string& dsn) {
    ParsedMySqlUrl result;
    const std::string prefix = "mysql://";
    if (dsn.rfind(prefix, 0) != 0) {
        return result;
    }

    std::string rest = dsn.substr(prefix.size());
    const auto query_pos = rest.find('?');
    if (query_pos != std::string::npos) {
        rest.erase(query_pos);
    }

    const auto slash_pos = rest.find('/');
    std::string authority = slash_pos == std::string::npos ? rest : rest.substr(0, slash_pos);
    if (slash_pos != std::string::npos && slash_pos + 1 < rest.size()) {
        result.database = rest.substr(slash_pos + 1);
    }

    const auto at_pos = authority.rfind('@');
    std::string host_port = authority;
    if (at_pos != std::string::npos) {
        const auto credentials = authority.substr(0, at_pos);
        host_port = authority.substr(at_pos + 1);
        const auto colon_pos = credentials.find(':');
        result.username = colon_pos == std::string::npos ? credentials : credentials.substr(0, colon_pos);
        if (colon_pos != std::string::npos) {
            result.password = credentials.substr(colon_pos + 1);
        }
    }

    const auto colon_pos = host_port.rfind(':');
    if (colon_pos != std::string::npos) {
        result.host = host_port.substr(0, colon_pos);
        result.port = host_port.substr(colon_pos + 1);
    } else {
        result.host = host_port;
    }
    return result;
}

MySqlAsyncOptions normalize_options(MySqlAsyncOptions options) {
    if (!options.connection_info.empty()) {
        const auto parsed = parse_mysql_url(options.connection_info);
        if (!parsed.host.empty() && (options.host.empty() || options.host == "127.0.0.1")) {
            options.host = parsed.host;
        }
        if (!parsed.port.empty() && (options.port.empty() || options.port == "3306")) {
            options.port = parsed.port;
        }
        if (!parsed.username.empty() && options.username.empty()) {
            options.username = parsed.username;
        }
        if (!parsed.password.empty() && options.password.empty()) {
            options.password = parsed.password;
        }
        if (!parsed.database.empty() && options.database.empty()) {
            options.database = parsed.database;
        }
    }
    if (options.host.empty()) {
        options.host = "127.0.0.1";
    }
    if (options.port.empty()) {
        options.port = "3306";
    }
    return options;
}

} // namespace

struct MySqlAsyncDriver::Impl {
    net::any_io_executor executor;
    MySqlAsyncOptions options;
    std::unique_ptr<mysql::tcp_connection> conn;
    bool connected{false};
    bool in_flight{false};
    std::unordered_map<std::string, mysql::statement> prepared_statements;

    Impl(net::any_io_executor ex, MySqlAsyncOptions opts)
        : executor(std::move(ex)), options(normalize_options(std::move(opts))) {}

    void ensure_connection() const {
        if (!conn || !connected) {
            throw DbError(DbErrorCode::ConnectionLost, "MySQL async connection is not open");
        }
    }

    void throw_if_busy() const {
        if (in_flight) {
            throw DbError(DbErrorCode::QueryRejected, "MySQL async driver already has an operation in flight");
        }
    }

    struct InFlightGuard {
        Impl& impl;
        explicit InFlightGuard(Impl& i) : impl(i) { impl.in_flight = true; }
        ~InFlightGuard() { impl.in_flight = false; }
    };

    void fail_connection() {
        connected = false;
        prepared_statements.clear();
        if (conn) {
            boost::system::error_code ignored;
            conn->stream().close(ignored);
        }
    }

    void check_token(const CancellationToken& token) {
        if (token.is_cancelled()) {
            fail_connection();
            throw db_cancelled();
        }
    }

    QueryResult convert_results(const mysql::results& input, const QueryOptions& options) {
        QueryResult output;
        output.affected_rows = input.affected_rows();
        output.command_tag = "mysql";

        const auto meta = input.meta();
        for (const auto row_view : input.rows()) {
            if (options.max_rows > 0 && output.rows.size() >= options.max_rows) {
                break;
            }
            Row row;
            std::size_t index = 0;
            for (const auto field : row_view) {
                std::string column_name = index < meta.size()
                    ? from_mysql_string(meta[index].column_name())
                    : "column_" + std::to_string(index + 1);
                if (column_name.empty() || row.columns.find(column_name) != row.columns.end()) {
                    column_name = "column_" + std::to_string(index + 1);
                }
                row.columns.emplace(std::move(column_name), field_to_string(field));
                ++index;
            }
            output.rows.push_back(std::move(row));
            if (options.single_row) {
                break;
            }
        }

        return output;
    }

    std::vector<mysql::field_view> make_field_views(const QueryParams& params) {
        std::vector<mysql::field_view> fields;
        fields.reserve(params.size());
        for (const auto& param : params) {
            if (param.is_null) {
                fields.emplace_back(nullptr);
            } else {
                fields.emplace_back(mysql::string_view(param.value.data(), param.value.size()));
            }
        }
        return fields;
    }

    net::awaitable<mysql::statement> prepare_statement_impl(
        std::string sql,
        std::optional<std::chrono::steady_clock::time_point> deadline,
        const CancellationToken& token) {
        check_token(token);
        throw_if_deadline_expired(deadline, "MySQL prepare timed out");
        mysql::diagnostics diag;
        auto [ec, statement] = co_await conn->async_prepare_statement(sql, diag, net::as_tuple(net::use_awaitable));
        if (ec) {
            fail_connection();
            throw make_mysql_error(ec, diag);
        }
        check_token(token);
        throw_if_deadline_expired(deadline, "MySQL prepare timed out");
        co_return statement;
    }

    net::awaitable<QueryResult> run_statement(
        mysql::statement statement,
        QueryParams params,
        QueryOptions options,
        std::optional<std::chrono::steady_clock::time_point> deadline,
        const CancellationToken& token) {
        check_token(token);
        throw_if_deadline_expired(deadline, "MySQL prepared query timed out");
        mysql::diagnostics diag;
        mysql::results results;
        auto fields = make_field_views(params);
        auto [ec] = co_await conn->async_execute(
            statement.bind(fields.begin(), fields.end()),
            results,
            diag,
            net::as_tuple(net::use_awaitable));
        if (ec) {
            fail_connection();
            throw make_mysql_error(ec, diag);
        }
        check_token(token);
        throw_if_deadline_expired(deadline, "MySQL prepared query timed out");
        co_return convert_results(results, options);
    }

    net::awaitable<void> close_temporary_statement(
        const mysql::statement& statement,
        std::optional<std::chrono::steady_clock::time_point> deadline,
        const CancellationToken& token) {
        check_token(token);
        throw_if_deadline_expired(deadline, "MySQL close temporary prepared statement timed out");
        mysql::diagnostics diag;
        auto [ec] = co_await conn->async_close_statement(statement, diag, net::as_tuple(net::use_awaitable));
        if (ec) {
            fail_connection();
            throw make_mysql_error(ec, diag);
        }
        check_token(token);
        throw_if_deadline_expired(deadline, "MySQL close temporary prepared statement timed out");
        co_return;
    }

    net::awaitable<QueryResult> run_temporary_statement(
        mysql::statement statement,
        QueryParams params,
        QueryOptions options,
        std::optional<std::chrono::steady_clock::time_point> deadline,
        const CancellationToken& token) {
        auto result = co_await run_statement(statement, std::move(params), std::move(options), deadline, token);
        co_await close_temporary_statement(statement, deadline, token);
        co_return result;
    }

    net::awaitable<QueryResult> run_text(
        std::string sql,
        QueryOptions options,
        std::optional<std::chrono::steady_clock::time_point> deadline,
        const CancellationToken& token) {
        check_token(token);
        throw_if_deadline_expired(deadline, "MySQL query timed out");
        mysql::diagnostics diag;
        mysql::results results;
        auto [ec] = co_await conn->async_execute(sql, results, diag, net::as_tuple(net::use_awaitable));
        if (ec) {
            fail_connection();
            throw make_mysql_error(ec, diag);
        }
        check_token(token);
        throw_if_deadline_expired(deadline, "MySQL query timed out");
        co_return convert_results(results, options);
    }

    net::awaitable<QueryResult> run_sql(std::string sql,
                                        QueryParams params,
                                        QueryOptions options,
                                        CancellationToken token) {
        ensure_connection();
        throw_if_busy();
        InFlightGuard guard(*this);

        const auto timeout = options.timeout.count() > 0 ? options.timeout : this->options.default_query_timeout;
        const auto deadline = operation_deadline(token, timeout);

        try {
            if (options.prepared || !options.statement_name.empty()) {
                if (options.statement_name.empty()) {
                    throw DbError(DbErrorCode::QueryRejected, "prepared MySQL query requires statement_name");
                }
                const auto found = prepared_statements.find(options.statement_name);
                if (found == prepared_statements.end()) {
                    throw DbError(DbErrorCode::QueryRejected, "unknown MySQL prepared statement: " + options.statement_name);
                }
                auto result = co_await run_statement(found->second, std::move(params), std::move(options), deadline, token);
                co_return result;
            }

            if (!params.empty()) {
                auto statement = co_await prepare_statement_impl(sql, deadline, token);
                auto result = co_await run_temporary_statement(statement, std::move(params), std::move(options), deadline, token);
                co_return result;
            }

            auto result = co_await run_text(std::move(sql), std::move(options), deadline, token);
            co_return result;
        } catch (const DbError& error) {
            if (error.code() == DbErrorCode::Timeout || error.code() == DbErrorCode::Cancelled ||
                error.code() == DbErrorCode::ConnectionLost || error.code() == DbErrorCode::Connection) {
                fail_connection();
            }
            throw;
        }
    }
};

MySqlAsyncDriver::MySqlAsyncDriver(net::any_io_executor executor, MySqlAsyncOptions options)
    : impl_(std::make_unique<Impl>(std::move(executor), std::move(options))) {}

MySqlAsyncDriver::~MySqlAsyncDriver() = default;

net::awaitable<void> MySqlAsyncDriver::connect(const CancellationToken& token) {
    if (impl_->conn) {
        co_await close();
    }
    if (impl_->options.username.empty()) {
        throw DbError(DbErrorCode::Connection, "MySQL async username is empty");
    }

    impl_->conn = std::make_unique<mysql::tcp_connection>(impl_->executor);
    impl_->conn->set_meta_mode(mysql::metadata_mode::full);
    const auto deadline = operation_deadline(token, impl_->options.connect_timeout);
    impl_->check_token(token);
    throw_if_deadline_expired(deadline, "MySQL connection timed out");

    ip::tcp::resolver resolver(impl_->executor);
    auto [resolve_ec, endpoints] = co_await resolver.async_resolve(
        impl_->options.host,
        impl_->options.port,
        net::as_tuple(net::use_awaitable));
    if (resolve_ec) {
        impl_->fail_connection();
        throw DbError(DbErrorCode::Connection, resolve_ec.message());
    }
    impl_->check_token(token);
    throw_if_deadline_expired(deadline, "MySQL connection timed out");

    mysql::diagnostics diag;
    mysql::handshake_params params(
        impl_->options.username,
        impl_->options.password,
        impl_->options.database,
        mysql::handshake_params::default_collation,
        mysql::ssl_mode::disable);
    auto [connect_ec] = co_await impl_->conn->async_connect(
        *endpoints.begin(),
        params,
        diag,
        net::as_tuple(net::use_awaitable));
    if (connect_ec) {
        impl_->fail_connection();
        throw make_mysql_error(connect_ec, diag);
    }
    impl_->check_token(token);
    throw_if_deadline_expired(deadline, "MySQL connection timed out");
    impl_->connected = true;
}

net::awaitable<void> MySqlAsyncDriver::close() {
    if (impl_->conn && impl_->connected) {
        mysql::diagnostics diag;
        auto [ec] = co_await impl_->conn->async_close(diag, net::as_tuple(net::use_awaitable));
        (void)ec;
    }
    impl_->fail_connection();
    impl_->conn.reset();
    co_return;
}

bool MySqlAsyncDriver::is_open() const {
    return impl_ && impl_->conn && impl_->connected;
}

std::string MySqlAsyncDriver::driver_name() const {
    return "mysql_async";
}

net::awaitable<QueryResult> MySqlAsyncDriver::query(
    std::string sql,
    QueryParams params,
    QueryOptions options,
    CancellationToken token) {
    auto result = co_await impl_->run_sql(std::move(sql), std::move(params), std::move(options), std::move(token));
    co_return result;
}

net::awaitable<QueryResult> MySqlAsyncDriver::execute(
    std::string sql,
    QueryParams params,
    QueryOptions options,
    CancellationToken token) {
    auto result = co_await impl_->run_sql(std::move(sql), std::move(params), std::move(options), std::move(token));
    co_return result;
}

net::awaitable<void> MySqlAsyncDriver::prepare(std::string name, std::string sql, CancellationToken token) {
    impl_->ensure_connection();
    impl_->throw_if_busy();
    Impl::InFlightGuard guard(*impl_);
    if (name.empty()) {
        throw DbError(DbErrorCode::QueryRejected, "prepared statement name cannot be empty");
    }
    const auto deadline = operation_deadline(token, impl_->options.default_query_timeout);
    auto statement = co_await impl_->prepare_statement_impl(std::move(sql), deadline, token);
    impl_->prepared_statements[name] = statement;
}

net::awaitable<void> MySqlAsyncDriver::begin(CancellationToken token) {
    QueryOptions options;
    options.timeout = impl_->options.default_query_timeout;
    auto result = co_await execute("BEGIN", {}, std::move(options), std::move(token));
    (void)result;
}

net::awaitable<void> MySqlAsyncDriver::commit(CancellationToken token) {
    QueryOptions options;
    options.timeout = impl_->options.default_query_timeout;
    auto result = co_await execute("COMMIT", {}, std::move(options), std::move(token));
    (void)result;
}

net::awaitable<void> MySqlAsyncDriver::rollback(CancellationToken token) {
    QueryOptions options;
    options.timeout = impl_->options.default_query_timeout;
    auto result = co_await execute("ROLLBACK", {}, std::move(options), std::move(token));
    (void)result;
}

AsyncDriverFactory make_mysql_async_driver_factory(MySqlAsyncOptions options) {
    return [options = std::move(options)](net::any_io_executor executor) mutable {
        return std::make_unique<MySqlAsyncDriver>(std::move(executor), options);
    };
}

} // namespace qornix::db
