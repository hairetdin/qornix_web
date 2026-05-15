/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "http_server.h"
#include "db/async_db.h"
#include "db/async_db_errors.h"
#include "db/postgres_async_driver.h"

#include <boost/json.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string required_env(const char* name) {
    const char* value = std::getenv(name);
    if (!value || std::string(value).empty()) {
        throw std::runtime_error(std::string(name) + " is required");
    }
    return value;
}

unsigned short parse_port(int argc, char* argv[]) {
    if (argc < 2) {
        return 8021;
    }
    const int port = std::stoi(argv[1]);
    if (port <= 0 || port > 65535) {
        throw std::runtime_error("port must be in range 1..65535");
    }
    return static_cast<unsigned short>(port);
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

Response db_error_response(unsigned version, const qornix::db::DbError& error) {
    boost::json::object body;
    body["ok"] = false;
    body["error"] = qornix::db::to_string(error.code());
    body["message"] = error.what();
    return response::json(http::status::service_unavailable, boost::json::serialize(body), version);
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const auto port = parse_port(argc, argv);
        const auto thread_count = parse_thread_count(argc, argv);

        net::io_context ioc;

        qornix::db::PostgresAsyncOptions driver_options;
        driver_options.connection_info = required_env("QORNIX_ASYNC_POSTGRES_URL");

        qornix::db::AsyncPoolOptions pool_options;
        pool_options.pool_name = "async-postgres-example";
        pool_options.driver_name = "postgres_async";
        pool_options.max_connections = 8;
        pool_options.max_waiters = 128;
        pool_options.query_timeout = std::chrono::milliseconds{2000};

        auto db = std::make_shared<qornix::db::AsyncDatabase>(
            ioc.get_executor(),
            qornix::db::make_postgres_async_driver_factory(std::move(driver_options)),
            pool_options);

        HttpServer server(ioc, tcp::endpoint{net::ip::make_address("127.0.0.1"), port});
        server.add_route("/health", [](const Request& req, Response& res, const auto&, const auto&) {
            res = response::json(R"({"status":"ok","driver":"postgres_async"})", req.version());
        });

        server.get_async("/users/{id}", [db](Request req, Url, Params params) -> net::awaitable<Response> {
            try {
                qornix::db::QueryParams query_params;
                query_params.push_back(qornix::db::QueryParam::text(params.at("id")));
                qornix::db::QueryOptions options;
                options.prepared = true;
                auto result = co_await db->query_one(
                    "SELECT $1::text AS id, ('user-' || $1::text) AS name",
                    std::move(query_params),
                    options);
                co_return response::json(result.to_json(), req.version());
            } catch (const qornix::db::DbError& error) {
                co_return db_error_response(req.version(), error);
            }
        });

        server.run();

        std::cout << "async_postgres_server listening on 127.0.0.1:" << port
                  << " with " << thread_count << " io_context threads" << std::endl;

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
    } catch (const std::exception& error) {
        std::cerr << "async_postgres_server error: " << error.what() << std::endl;
        return 1;
    }
    return 0;
}
