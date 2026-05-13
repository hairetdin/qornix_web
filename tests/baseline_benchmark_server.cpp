/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "http_server.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
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
        HttpServer server(ioc, tcp::endpoint{net::ip::make_address("127.0.0.1"), port});

        server.add_route("/health", benchmark_handler(std::chrono::milliseconds{0}));
        server.add_route("/bench/fast", benchmark_handler(std::chrono::milliseconds{0}));
        server.add_route("/bench/delay/10", benchmark_handler(std::chrono::milliseconds{10}));
        server.add_route("/bench/delay/100", benchmark_handler(std::chrono::milliseconds{100}));

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
