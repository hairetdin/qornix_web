/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "http_server.h"
#include "db/async_db.h"
#include "db/async_db_errors.h"
#include "db/mock_async_driver.h"

#if QORNIX_ENABLE_ASYNC_POSTGRES
#include "db/postgres_async_driver.h"
#endif

#if QORNIX_ENABLE_ASYNC_MYSQL
#include "db/mysql_async_driver.h"
#endif

#include <boost/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

namespace json = boost::json;

using qornix::db::AsyncDatabase;
using qornix::db::AsyncDriverFactory;
using qornix::db::AsyncPoolOptions;
using qornix::db::CancellationToken;
using qornix::db::DbError;
using qornix::db::DbErrorCode;
using qornix::db::QueryOptions;
using qornix::db::QueryParam;
using qornix::db::QueryParams;

struct BenchmarkDatabase {
    std::shared_ptr<AsyncDatabase> db;
    std::string driver;
    std::string select_sql;
    std::string slow_sql;
    std::chrono::milliseconds query_timeout{std::chrono::milliseconds{2000}};
    std::chrono::milliseconds slow_timeout{std::chrono::milliseconds{50}};
};

std::string get_env(std::string_view name, std::string fallback = {}) {
    const char* value = std::getenv(std::string(name).c_str());
    if (!value || std::string(value).empty()) {
        return fallback;
    }
    return value;
}

std::size_t env_size(std::string_view name, std::size_t fallback) {
    const auto value = get_env(name);
    if (value.empty()) {
        return fallback;
    }
    return static_cast<std::size_t>(std::stoull(value));
}

std::chrono::milliseconds env_ms(std::string_view name, std::chrono::milliseconds fallback) {
    const auto value = get_env(name);
    if (value.empty()) {
        return fallback;
    }
    return std::chrono::milliseconds{std::stoll(value)};
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

unsigned short parse_port(int argc, char* argv[]) {
    if (argc < 2) {
        return 18180;
    }
    const int value = std::stoi(argv[1]);
    if (value <= 0 || value > 65535) {
        throw std::runtime_error("port must be in range 1..65535");
    }
    return static_cast<unsigned short>(value);
}

std::size_t parse_thread_count(int argc, char* argv[]) {
    if (argc < 3) {
        const auto detected = std::thread::hardware_concurrency();
        return detected == 0 ? 4 : detected;
    }
    const int value = std::stoi(argv[2]);
    if (value <= 0) {
        throw std::runtime_error("thread count must be positive");
    }
    return static_cast<std::size_t>(value);
}

std::string parse_driver(int argc, char* argv[]) {
    if (argc >= 4 && std::string(argv[3]).size() > 0) {
        return lower_copy(argv[3]);
    }
    return lower_copy(get_env("QORNIX_ASYNC_DB_DRIVER", "mock"));
}

AsyncPoolOptions pool_options_for_driver(const std::string& driver) {
    AsyncPoolOptions options;
    options.pool_name = get_env("QORNIX_ASYNC_DB_POOL_NAME", "async-db-benchmark");
    options.driver_name = driver == "postgres" ? "postgres_async" :
                          driver == "mysql" ? "mysql_async" : "mock_async";
    options.min_connections = env_size("QORNIX_ASYNC_DB_MIN_CONNECTIONS", 1);
    options.max_connections = env_size("QORNIX_ASYNC_DB_POOL_SIZE", 32);
    options.max_waiters = env_size("QORNIX_ASYNC_DB_MAX_WAITERS", 1024);
    options.acquire_timeout = env_ms("QORNIX_ASYNC_DB_ACQUIRE_TIMEOUT_MS", std::chrono::milliseconds{1000});
    options.query_timeout = env_ms("QORNIX_ASYNC_DB_QUERY_TIMEOUT_MS", std::chrono::milliseconds{2000});
    options.idle_timeout = env_ms("QORNIX_ASYNC_DB_IDLE_TIMEOUT_MS", std::chrono::milliseconds{30000});
    options.max_lifetime = env_ms("QORNIX_ASYNC_DB_MAX_LIFETIME_MS", std::chrono::milliseconds{1800000});
    options.shutdown_timeout = env_ms("QORNIX_ASYNC_DB_SHUTDOWN_TIMEOUT_MS", std::chrono::milliseconds{5000});
    options.prepared_cache_size = env_size("QORNIX_ASYNC_DB_PREPARED_CACHE_SIZE", 64);
    return options;
}

AsyncDriverFactory factory_for_driver(const std::string& driver, net::any_io_executor executor) {
    (void)executor;
    if (driver == "mock") {
        const auto latency = env_ms("QORNIX_ASYNC_DB_MOCK_LATENCY_MS", std::chrono::milliseconds{2});
        return qornix::db::make_mock_async_driver_factory(latency);
    }

    if (driver == "postgres") {
#if QORNIX_ENABLE_ASYNC_POSTGRES
        qornix::db::PostgresAsyncOptions options;
        options.connection_info = get_env("QORNIX_ASYNC_POSTGRES_URL");
        if (options.connection_info.empty()) {
            throw std::runtime_error("QORNIX_ASYNC_POSTGRES_URL is required for postgres benchmark driver");
        }
        options.connect_timeout = env_ms("QORNIX_ASYNC_DB_CONNECT_TIMEOUT_MS", std::chrono::milliseconds{5000});
        options.default_query_timeout = env_ms("QORNIX_ASYNC_DB_QUERY_TIMEOUT_MS", std::chrono::milliseconds{2000});
        return qornix::db::make_postgres_async_driver_factory(std::move(options));
#else
        throw std::runtime_error("postgres benchmark driver requires QORNIX_ENABLE_ASYNC_POSTGRES=ON");
#endif
    }

    if (driver == "mysql") {
#if QORNIX_ENABLE_ASYNC_MYSQL
        qornix::db::MySqlAsyncOptions options;
        options.connection_info = get_env("QORNIX_ASYNC_MYSQL_URL");
        options.host = get_env("QORNIX_ASYNC_MYSQL_HOST", "127.0.0.1");
        options.port = get_env("QORNIX_ASYNC_MYSQL_PORT", "3306");
        options.username = get_env("QORNIX_ASYNC_MYSQL_USER");
        options.password = get_env("QORNIX_ASYNC_MYSQL_PASSWORD");
        options.database = get_env("QORNIX_ASYNC_MYSQL_DATABASE");
        if (options.connection_info.empty() && options.username.empty()) {
            throw std::runtime_error("QORNIX_ASYNC_MYSQL_URL or QORNIX_ASYNC_MYSQL_USER is required for mysql benchmark driver");
        }
        options.connect_timeout = env_ms("QORNIX_ASYNC_DB_CONNECT_TIMEOUT_MS", std::chrono::milliseconds{5000});
        options.default_query_timeout = env_ms("QORNIX_ASYNC_DB_QUERY_TIMEOUT_MS", std::chrono::milliseconds{2000});
        return qornix::db::make_mysql_async_driver_factory(std::move(options));
#else
        throw std::runtime_error("mysql benchmark driver requires QORNIX_ENABLE_ASYNC_MYSQL=ON");
#endif
    }

    throw std::runtime_error("unsupported async DB benchmark driver: " + driver);
}

BenchmarkDatabase make_benchmark_database(net::any_io_executor executor, std::string driver) {
    auto pool_options = pool_options_for_driver(driver);
    auto factory = factory_for_driver(driver, executor);

    BenchmarkDatabase runtime;
    runtime.driver = std::move(driver);
    runtime.query_timeout = pool_options.query_timeout;
    runtime.slow_timeout = env_ms("QORNIX_ASYNC_DB_SLOW_TIMEOUT_MS", std::chrono::milliseconds{50});
    runtime.db = std::make_shared<AsyncDatabase>(std::move(executor), std::move(factory), std::move(pool_options));

    if (runtime.driver == "mysql") {
        runtime.select_sql = "SELECT ? AS value";
        runtime.slow_sql = "SELECT SLEEP(?) AS slept";
    } else if (runtime.driver == "postgres") {
        runtime.select_sql = "SELECT $1::text AS value";
        runtime.slow_sql = "SELECT pg_sleep($1::double precision) AS slept";
    } else {
        runtime.select_sql = "SELECT $1 AS value";
        runtime.slow_sql = "SELECT $1 AS value";
    }
    return runtime;
}

CancellationToken token_after(std::chrono::milliseconds timeout) {
    CancellationToken token;
    token.deadline = std::chrono::steady_clock::now() + timeout;
    return token;
}

QueryOptions options_with_timeout(std::chrono::milliseconds timeout, bool prepared = false) {
    QueryOptions options;
    options.timeout = timeout;
    options.prepared = prepared;
    return options;
}

QueryParams one_text_param(std::string value) {
    QueryParams params;
    params.push_back(QueryParam::text(std::move(value)));
    return params;
}

http::status status_for_db_error(DbErrorCode code) {
    switch (code) {
        case DbErrorCode::PoolRejected:
        case DbErrorCode::PoolTimeout:
        case DbErrorCode::Connection:
        case DbErrorCode::ConnectionLost:
        case DbErrorCode::Unavailable:
            return http::status::service_unavailable;
        case DbErrorCode::Timeout:
        case DbErrorCode::Cancelled:
            return http::status::gateway_timeout;
        case DbErrorCode::ConstraintViolation:
        case DbErrorCode::DuplicateKey:
        case DbErrorCode::ForeignKeyViolation:
        case DbErrorCode::Conflict:
            return http::status::conflict;
        case DbErrorCode::Syntax:
        case DbErrorCode::QueryRejected:
            return http::status::bad_request;
        default:
            return http::status::internal_server_error;
    }
}

Response db_error_response(unsigned version, const DbError& error) {
    json::object body;
    body["ok"] = false;
    body["error"] = qornix::db::to_string(error.code());
    body["message"] = error.what();
    if (!error.sql_state().empty()) {
        body["sql_state"] = error.sql_state();
    }
    return response::json(status_for_db_error(error.code()), json::serialize(body), version);
}

json::object metrics_to_json(const qornix::db::AsyncDbMetricsSnapshot& metrics) {
    json::object body;
    body["active_connections"] = metrics.active_connections;
    body["idle_connections"] = metrics.idle_connections;
    body["queued_waiters"] = metrics.queued_waiters;
    body["rejected_acquires"] = metrics.rejected_acquires;
    body["acquire_timeouts"] = metrics.acquire_timeouts;
    body["created_connections"] = metrics.created_connections;
    body["closed_connections"] = metrics.closed_connections;
    body["discarded_connections"] = metrics.discarded_connections;
    body["failed_connects"] = metrics.failed_connects;
    body["query_total"] = metrics.query_total;
    body["query_success"] = metrics.query_success;
    body["query_error"] = metrics.query_error;
    body["query_timeout"] = metrics.query_timeout;
    body["query_cancelled"] = metrics.query_cancelled;
    body["query_option_timeouts"] = metrics.query_option_timeouts;
    body["query_pool_default_timeouts"] = metrics.query_pool_default_timeouts;
    body["request_deadline_timeouts"] = metrics.request_deadline_timeouts;
    body["prepared_cache_hits"] = metrics.prepared_cache_hits;
    body["prepared_cache_misses"] = metrics.prepared_cache_misses;
    body["prepared_cache_evictions"] = metrics.prepared_cache_evictions;
    body["query_latency_p50_us"] = metrics.query_latency_p50_us;
    body["query_latency_p95_us"] = metrics.query_latency_p95_us;
    body["query_latency_p99_us"] = metrics.query_latency_p99_us;
    return body;
}

Response result_response(unsigned version, std::string scenario, const qornix::db::QueryResult& result) {
    json::object body;
    body["ok"] = true;
    body["scenario"] = std::move(scenario);
    body["result"] = result.to_json_value();
    return response::json(json::serialize(body), version);
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const auto port = parse_port(argc, argv);
        const auto thread_count = parse_thread_count(argc, argv);
        auto driver = parse_driver(argc, argv);

        net::io_context ioc;
        HttpServerOptions server_options;
        server_options.route_timeout = env_ms("QORNIX_ASYNC_DB_ROUTE_TIMEOUT_MS", std::chrono::seconds{5});
        server_options.max_active_requests = env_size("QORNIX_ASYNC_DB_MAX_ACTIVE_REQUESTS", 20000);
        HttpServer server(ioc, tcp::endpoint{net::ip::make_address("127.0.0.1"), port}, server_options);

        auto runtime = std::make_shared<BenchmarkDatabase>(
            make_benchmark_database(ioc.get_executor(), std::move(driver)));

        server.add_route("/health", [](const Request& req, Response& res, const auto&, const auto&) {
            res = response::json(R"({"status":"ok"})", req.version());
        });

        server.get_async("/db/select", [runtime](Request req, Url, Params) -> net::awaitable<Response> {
            try {
                auto result = co_await runtime->db->query(
                    runtime->select_sql,
                    one_text_param("qornix"),
                    options_with_timeout(runtime->query_timeout),
                    token_after(runtime->query_timeout));
                co_return result_response(req.version(), "select", result);
            } catch (const DbError& error) {
                co_return db_error_response(req.version(), error);
            }
        });

        server.get_async("/db/prepared", [runtime](Request req, Url, Params) -> net::awaitable<Response> {
            try {
                auto result = co_await runtime->db->query(
                    runtime->select_sql,
                    one_text_param("qornix"),
                    options_with_timeout(runtime->query_timeout, true),
                    token_after(runtime->query_timeout));
                co_return result_response(req.version(), "prepared", result);
            } catch (const DbError& error) {
                co_return db_error_response(req.version(), error);
            }
        });

        server.get_async("/db/transaction", [runtime](Request req, Url, Params) -> net::awaitable<Response> {
            try {
                auto token = token_after(runtime->query_timeout);
                auto tx = co_await runtime->db->begin_transaction(token);
                auto result = co_await tx.query(
                    runtime->select_sql,
                    one_text_param("tx"),
                    options_with_timeout(runtime->query_timeout),
                    token);
                co_await tx.rollback(token);
                co_return result_response(req.version(), "transaction_rollback", result);
            } catch (const DbError& error) {
                co_return db_error_response(req.version(), error);
            }
        });

        server.get_async("/db/slow", [runtime](Request req, Url, Params) -> net::awaitable<Response> {
            try {
                const auto sleep_seconds = get_env("QORNIX_ASYNC_DB_SLOW_SECONDS", "0.25");
                const auto timeout = runtime->driver == "mock"
                    ? std::min(runtime->slow_timeout, std::chrono::milliseconds{1})
                    : runtime->slow_timeout;
                const auto request_token = token_after(runtime->query_timeout);
                auto result = co_await runtime->db->query(
                    runtime->slow_sql,
                    one_text_param(sleep_seconds),
                    options_with_timeout(timeout),
                    request_token);
                co_return result_response(req.version(), "slow", result);
            } catch (const DbError& error) {
                co_return db_error_response(req.version(), error);
            }
        });

        server.add_route("/db/metrics", [runtime](const Request& req, Response& res, const auto&, const auto&) {
            json::object body;
            body["driver"] = runtime->driver;
            body["metrics"] = metrics_to_json(runtime->db->metrics_snapshot());
            res = response::json(json::serialize(body), req.version());
        });

        server.run();

        std::cout << "async_db_benchmark_server listening on 127.0.0.1:" << port
                  << " with driver " << runtime->driver
                  << " and " << thread_count << " io_context threads" << std::endl;

        std::vector<std::thread> threads;
        threads.reserve(thread_count);
        for (std::size_t i = 0; i < thread_count; ++i) {
            threads.emplace_back([&ioc]() {
                ioc.run();
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }
    } catch (const std::exception& e) {
        std::cerr << "async_db_benchmark_server error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
