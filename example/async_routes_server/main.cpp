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
#include <string>
#include <thread>
#include <vector>

namespace {

unsigned short parse_port(int argc, char* argv[]) {
    if (argc < 2) {
        return 8010;
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

    const int thread_count = std::stoi(argv[2]);
    if (thread_count <= 0) {
        throw std::runtime_error("thread count must be positive");
    }

    return static_cast<std::size_t>(thread_count);
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const auto port = parse_port(argc, argv);
        const auto thread_count = parse_thread_count(argc, argv);

        net::io_context ioc;
        HttpServer server(ioc, tcp::endpoint{net::ip::make_address("127.0.0.1"), port});

        server.add_route("/health", [](const Request& req, Response& res, const auto&, const auto&) {
            res = response::text("healthy", req.version());
        });

        server.get_async("/sleep/{ms}", [](Request req, Url, Params params) -> net::awaitable<Response> {
            auto executor = co_await net::this_coro::executor;
            const auto requested = std::stoi(params.at("ms"));
            const auto delay_ms = std::clamp(requested, 0, 2000);

            net::steady_timer timer(executor);
            timer.expires_after(std::chrono::milliseconds{delay_ms});
            co_await timer.async_wait(net::use_awaitable);

            co_return response::text("slept:" + std::to_string(delay_ms), req.version());
        });

        server.get_async("/api/status", [](Request req, Url, Params) -> net::awaitable<Response> {
            co_return response::json(R"({"status":"ok","mode":"async"})", req.version());
        });

        server.post_async("/api/echo", [](Request req, Url, Params) -> net::awaitable<Response> {
            co_return response::json("{\"echo\":\"" + req.body() + "\"}", req.version());
        });

        server.any_async("/api/method", [](Request req, Url, Params) -> net::awaitable<Response> {
            co_return response::text("method:" + std::string(req.method_string()), req.version());
        });

        server.run();

        std::cout << "async_routes_server listening on 127.0.0.1:" << port
                  << " with " << thread_count << " io_context threads" << std::endl;
        std::cout << "try: curl http://127.0.0.1:" << port << "/sleep/100" << std::endl;
        std::cout << "try: curl http://127.0.0.1:" << port << "/api/status" << std::endl;
        std::cout << "try: curl -X POST http://127.0.0.1:" << port << "/api/echo -d hello" << std::endl;

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
        std::cerr << "async_routes_server error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
