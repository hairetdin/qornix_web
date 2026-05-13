/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "http_server.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

void set_text_response(http::response<http::string_body>& res, const std::string& body) {
    res.result(http::status::ok);
    res.set(http::field::content_type, "text/plain");
    res.body() = body;
    res.prepare_payload();
}


int parse_delay_ms(const Url& url, const Params& params, int fallback) {
    if (auto it = params.find("ms"); it != params.end()) {
        return std::clamp(std::stoi(it->second), 0, 10000);
    }

    const std::string query(url.query());
    const auto pos = query.find("ms=");
    if (pos == std::string::npos) {
        return fallback;
    }
    const auto start = pos + 3;
    const auto end = query.find('&', start);
    return std::clamp(std::stoi(query.substr(start, end == std::string::npos ? end : end - start)), 0, 10000);
}

AsyncRouteHandler async_sleep_route(int fallback_ms) {
    return [fallback_ms](Request req, Url url, Params params) -> net::awaitable<Response> {
        const auto delay_ms = parse_delay_ms(url, params, fallback_ms);
        auto executor = co_await net::this_coro::executor;
        net::steady_timer timer(executor);
        timer.expires_after(std::chrono::milliseconds{delay_ms});
        co_await timer.async_wait(net::use_awaitable);
        co_return response::text("slept:" + std::to_string(delay_ms), req.version());
    };
}

AsyncRouteHandler async_benchmark_handler(int delay_ms) {
    return [delay_ms](Request req, Url, Params) -> net::awaitable<Response> {
        auto executor = co_await net::this_coro::executor;
        net::steady_timer timer(executor);
        timer.expires_after(std::chrono::milliseconds{delay_ms});
        co_await timer.async_wait(net::use_awaitable);
        co_return response::text("ok", req.version());
    };
}

RouteHandler benchmark_handler(std::chrono::milliseconds delay) {
    return [delay](const auto&, auto& res, const auto&, const auto&) {
        if (delay.count() > 0) {
            std::this_thread::sleep_for(delay);
        }
        set_text_response(res, "ok");
    };
}

unsigned short parse_port(int argc, char* argv[]) {
    if (argc < 2) {
        return 18080;
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

} // namespace

int main(int argc, char* argv[]) {
    try {
        const auto port = parse_port(argc, argv);
        const auto thread_count = parse_thread_count(argc, argv);

        net::io_context ioc;
        HttpServerOptions options;
        options.route_timeout = std::chrono::seconds{5};
        options.max_active_requests = 10000;
        options.max_request_body_size = 1024 * 1024;
        HttpServer server(ioc, tcp::endpoint{net::ip::make_address("127.0.0.1"), port}, options);
        server.add_metrics_route();

        auto metrics = server.metrics_ptr();
        auto blocking_pool = std::make_shared<qornix::async::BlockingTaskPool>(
            std::max<std::size_t>(2, thread_count), 4096, metrics);
        auto db_pool = std::make_shared<qornix::async::AsyncDbPool>(100, 5000, metrics);

        server.add_route("/health", benchmark_handler(std::chrono::milliseconds{0}));
        server.add_route("/bench/fast", benchmark_handler(std::chrono::milliseconds{0}));
        server.get_async("/bench/delay/10", async_benchmark_handler(10),
                         RouteOptions{std::chrono::seconds{2}, std::nullopt, std::nullopt});
        server.get_async("/bench/delay/100", async_benchmark_handler(100),
                         RouteOptions{std::chrono::seconds{2}, std::nullopt, std::nullopt});
        server.add_route("/bench/blocking-delay/10", benchmark_handler(std::chrono::milliseconds{10}));
        server.add_route("/bench/blocking-delay/100", benchmark_handler(std::chrono::milliseconds{100}));
        server.get_async("/sleep", async_sleep_route(100),
                         RouteOptions{std::chrono::seconds{2}, std::nullopt, std::nullopt});
        server.get_async("/sleep/{ms}", async_sleep_route(100),
                         RouteOptions{std::chrono::seconds{2}, std::nullopt, std::nullopt});
        server.get_async("/users/{id}", [db_pool](Request req, Url, Params params) -> net::awaitable<Response> {
            auto conn = co_await db_pool->acquire(std::chrono::milliseconds{250});
            auto user = co_await conn.fetch_user(params.at("id"), std::chrono::milliseconds{10});
            co_return response::json(std::move(user), req.version());
        }, RouteOptions{std::chrono::seconds{2}, std::nullopt, std::nullopt});
        server.get_async("/blocking/{ms}", [blocking_pool](Request req, Url url, Params params) -> net::awaitable<Response> {
            const auto delay_ms = parse_delay_ms(url, params, 10);
            auto value = co_await blocking_pool->submit([delay_ms] {
                std::this_thread::sleep_for(std::chrono::milliseconds{delay_ms});
                return std::string{"blocking:"} + std::to_string(delay_ms);
            }, std::chrono::seconds{2});
            co_return response::text(std::move(value), req.version());
        }, RouteOptions{std::chrono::seconds{3}, std::size_t{1000}, std::nullopt});

        server.run();

        std::cout << "qornix baseline benchmark server listening on 127.0.0.1:"
                  << port << " with " << thread_count << " threads" << std::endl;

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
        std::cerr << "baseline_benchmark_server error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
