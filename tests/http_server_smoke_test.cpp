/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "http_server.h"

#include <boost/beast/core/tcp_stream.hpp>

#include <cassert>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

class SmokeMiddleware : public Middleware {
public:
    void handle(const Request&,
                Response& res,
                const urls::url_view&,
                const Params&,
                NextHandler next) override {
        next();
        res.set("X-Smoke-Middleware", "pass");
    }
};

unsigned short allocate_loopback_port() {
    net::io_context ioc;
    tcp::acceptor acceptor(ioc, tcp::endpoint{net::ip::make_address("127.0.0.1"), 0});
    return acceptor.local_endpoint().port();
}

void set_text_response(http::response<http::string_body>& res,
                       http::status status,
                       const std::string& body) {
    res = make_text_response(status, 11, body);
}

AsyncRouteHandler async_sleep_handler(std::chrono::milliseconds delay, std::string body) {
    return [delay, body = std::move(body)](Request req, Url, Params) -> net::awaitable<Response> {
        auto executor = co_await net::this_coro::executor;
        net::steady_timer timer(executor);
        timer.expires_after(delay);
        co_await timer.async_wait(net::use_awaitable);

        co_return make_text_response(body, req.version());
    };
}

http::response<http::string_body> request_once(unsigned short port,
                                               http::verb method,
                                               const std::string& target,
                                               std::string body = {},
                                               bool keep_alive = false) {
    net::io_context ioc;
    beast::tcp_stream stream(ioc);
    tcp::resolver resolver(ioc);
    stream.connect(resolver.resolve("127.0.0.1", std::to_string(port)));

    http::request<http::string_body> req{method, target, 11};
    req.set(http::field::host, "127.0.0.1");
    req.keep_alive(keep_alive);
    req.body() = std::move(body);
    req.prepare_payload();

    http::write(stream, req);

    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    http::read(stream, buffer, res);

    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    return res;
}

http::response<http::string_body> request_once(unsigned short port,
                                               const std::string& target,
                                               bool keep_alive = false) {
    return request_once(port, http::verb::get, target, {}, keep_alive);
}

void expect_response(unsigned short port,
                     const std::string& target,
                     http::status status,
                     const std::string& body_fragment) {
    auto res = request_once(port, target);
    assert(res.result() == status);
    assert(res.body().find(body_fragment) != std::string::npos);
}

void expect_response(unsigned short port,
                     http::verb method,
                     const std::string& target,
                     const std::string& request_body,
                     http::status status,
                     const std::string& body_fragment) {
    auto res = request_once(port, method, target, request_body);
    assert(res.result() == status);
    assert(res.body().find(body_fragment) != std::string::npos);
}

void wait_for_server(unsigned short port) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            auto res = request_once(port, "/health");
            if (res.result() == http::status::ok) {
                return;
            }
        } catch (const std::exception&) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    throw std::runtime_error("HTTP smoke test server did not become ready");
}

void assert_keep_alive(unsigned short port) {
    net::io_context ioc;
    beast::tcp_stream stream(ioc);
    tcp::resolver resolver(ioc);
    stream.connect(resolver.resolve("127.0.0.1", std::to_string(port)));

    beast::flat_buffer buffer;

    http::request<http::empty_body> first{http::verb::get, "/health", 11};
    first.set(http::field::host, "127.0.0.1");
    first.keep_alive(true);
    http::write(stream, first);

    http::response<http::string_body> first_res;
    http::read(stream, buffer, first_res);
    assert(first_res.result() == http::status::ok);
    assert(first_res.keep_alive());

    http::request<http::empty_body> second{http::verb::get, "/plain", 11};
    second.set(http::field::host, "127.0.0.1");
    second.keep_alive(false);
    http::write(stream, second);

    http::response<http::string_body> second_res;
    http::read(stream, buffer, second_res);
    assert(second_res.result() == http::status::ok);
    assert(second_res.body() == "plain-ok");

    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
}

void assert_keep_alive_sequence(unsigned short port) {
    net::io_context ioc;
    beast::tcp_stream stream(ioc);
    tcp::resolver resolver(ioc);
    stream.connect(resolver.resolve("127.0.0.1", std::to_string(port)));

    beast::flat_buffer buffer;
    constexpr int request_count = 24;

    for (int i = 0; i < request_count; ++i) {
        const bool keep_alive = i + 1 < request_count;
        const std::string target = "/items/" + std::to_string(i);

        http::request<http::empty_body> req{http::verb::get, target, 11};
        req.set(http::field::host, "127.0.0.1");
        req.keep_alive(keep_alive);
        http::write(stream, req);

        http::response<http::string_body> res;
        http::read(stream, buffer, res);
        assert(res.result() == http::status::ok);
        assert(res.body() == "item:" + std::to_string(i));
        assert(res.keep_alive() == keep_alive);
    }

    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
}

void assert_async_does_not_block_other_requests() {
    const auto port = allocate_loopback_port();

    net::io_context ioc;
    HttpServer server(ioc, tcp::endpoint{net::ip::make_address("127.0.0.1"), port});

    server.add_route("/health", [](const auto&, auto& res, const auto&, const auto&) {
        set_text_response(res, http::status::ok, "healthy");
    });

    server.add_route("/plain", [](const auto&, auto& res, const auto&, const auto&) {
        set_text_response(res, http::status::ok, "plain-ok");
    });

    server.get_async("/async/sleep", async_sleep_handler(std::chrono::milliseconds{500}, "async-ok"));
    server.run();

    std::thread io_thread([&ioc]() {
        ioc.run();
    });

    try {
        wait_for_server(port);

        net::io_context client_ioc;
        beast::tcp_stream sleep_stream(client_ioc);
        tcp::resolver resolver(client_ioc);
        sleep_stream.connect(resolver.resolve("127.0.0.1", std::to_string(port)));

        http::request<http::empty_body> sleep_req{http::verb::get, "/async/sleep", 11};
        sleep_req.set(http::field::host, "127.0.0.1");
        sleep_req.keep_alive(false);
        http::write(sleep_stream, sleep_req);

        const auto start = std::chrono::steady_clock::now();
        auto plain_res = request_once(port, "/plain");
        const auto elapsed = std::chrono::steady_clock::now() - start;

        assert(plain_res.result() == http::status::ok);
        assert(plain_res.body() == "plain-ok");
        assert(elapsed < std::chrono::milliseconds{350});

        beast::flat_buffer buffer;
        http::response<http::string_body> sleep_res;
        http::read(sleep_stream, buffer, sleep_res);
        assert(sleep_res.result() == http::status::ok);
        assert(sleep_res.body() == "async-ok");

        beast::error_code ec;
        sleep_stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    } catch (...) {
        ioc.stop();
        if (io_thread.joinable()) {
            io_thread.join();
        }
        throw;
    }

    ioc.stop();
    if (io_thread.joinable()) {
        io_thread.join();
    }
}

} // namespace

int main() {
    const auto port = allocate_loopback_port();

    net::io_context ioc;
    HttpServer server(ioc, tcp::endpoint{net::ip::make_address("127.0.0.1"), port});
    server.add_middleware(std::make_shared<SmokeMiddleware>());

    server.add_route("/health", [](const auto&, auto& res, const auto&, const auto&) {
        set_text_response(res, http::status::ok, "healthy");
    });

    server.add_route("/plain", [](const auto&, auto& res, const auto&, const auto&) {
        set_text_response(res, http::status::ok, "plain-ok");
    });

    server.add_route("/items/{id}", [](const auto&, auto& res, const auto&, const auto& params) {
        const auto id = params.at("id");
        set_text_response(res, http::status::ok, "item:" + id);
    });

    server.add_route("/boom", [](const auto&, auto&, const auto&, const auto&) {
        throw std::runtime_error("boom");
    });

    server.get_async("/async/sleep", async_sleep_handler(std::chrono::milliseconds{25}, "async-ok"));

    server.get_async("/async/items/{id}", [](Request req, Url url, Params params) -> net::awaitable<Response> {
        auto executor = co_await net::this_coro::executor;
        net::steady_timer timer(executor);
        timer.expires_after(std::chrono::milliseconds{5});
        co_await timer.async_wait(net::use_awaitable);

        const auto body = "async-item:" + params.at("id") +
                          ":path=" + std::string(url.path()) +
                          ":target=" + std::string(req.target());
        co_return make_text_response(http::status::ok, req.version(), body);
    });

    server.get_async("/async/boom", [](Request req, Url, Params) -> net::awaitable<Response> {
        auto executor = co_await net::this_coro::executor;
        net::steady_timer timer(executor);
        timer.expires_after(std::chrono::milliseconds{1});
        co_await timer.async_wait(net::use_awaitable);

        throw std::runtime_error("async boom");
        co_return make_text_response(http::status::ok, req.version(), "unreachable");
    });

    server.post_async("/async/echo", [](Request req, Url, Params) -> net::awaitable<Response> {
        co_return response::text("post:" + req.body(), req.version());
    });

    server.put_async("/async/items/{id}", [](Request req, Url, Params params) -> net::awaitable<Response> {
        co_return response::json("{\"method\":\"PUT\",\"id\":\"" + params.at("id") +
                                 "\",\"body\":\"" + req.body() + "\"}",
                                 req.version());
    });

    server.patch_async("/async/items/{id}", [](Request req, Url, Params params) -> net::awaitable<Response> {
        co_return response::text("patch:" + params.at("id") + ":" + req.body(), req.version());
    });

    server.delete_async("/async/items/{id}", [](Request req, Url, Params params) -> net::awaitable<Response> {
        co_return response::status(http::status::accepted, req.version(), "delete:" + params.at("id"));
    });

    server.options_async("/async/options", [](Request req, Url, Params) -> net::awaitable<Response> {
        auto res = response::status(http::status::no_content, req.version());
        res.set(http::field::allow, "GET, POST, PUT, PATCH, DELETE, OPTIONS");
        co_return res;
    });

    server.head_async("/async/head", [](Request req, Url, Params) -> net::awaitable<Response> {
        co_return response::status(http::status::no_content, req.version());
    });

    server.any_async("/async/any", [](Request req, Url, Params) -> net::awaitable<Response> {
        co_return response::text("any:" + std::string(req.method_string()), req.version());
    });

    server.get_async("/async/redirect", [](Request req, Url, Params) -> net::awaitable<Response> {
        co_return response::redirect("/plain", req.version());
    });

    server.run();
    std::vector<std::thread> io_threads;
    for (int i = 0; i < 4; ++i) {
        io_threads.emplace_back([&ioc]() {
            ioc.run();
        });
    }

    auto join_io_threads = [&io_threads]() {
        for (auto& thread : io_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    };

    try {
        wait_for_server(port);
        expect_response(port, "/health", http::status::ok, "healthy");
        expect_response(port, "/plain", http::status::ok, "plain-ok");
        auto middleware_res = request_once(port, "/plain");
        assert(middleware_res["X-Smoke-Middleware"] == "pass");
        expect_response(port, "/items/42", http::status::ok, "item:42");
        expect_response(port, "/%ZZ", http::status::bad_request, "Invalid URL");
        expect_response(port, "/missing", http::status::not_found, "404");
        expect_response(port, "/boom", http::status::internal_server_error, "boom");
        expect_response(port, "/async/sleep", http::status::ok, "async-ok");
        expect_response(port,
                        "/async/items/77?source=test",
                        http::status::ok,
                        "async-item:77:path=/async/items/77:target=/async/items/77?source=test");
        expect_response(port, "/async/boom", http::status::internal_server_error, "async boom");
        expect_response(port, http::verb::post, "/async/echo", "payload", http::status::ok, "post:payload");
        expect_response(port,
                        http::verb::put,
                        "/async/items/55",
                        "replace",
                        http::status::ok,
                        "\"method\":\"PUT\",\"id\":\"55\",\"body\":\"replace\"");
        expect_response(port,
                        http::verb::patch,
                        "/async/items/55",
                        "delta",
                        http::status::ok,
                        "patch:55:delta");
        expect_response(port,
                        http::verb::delete_,
                        "/async/items/55",
                        "",
                        http::status::accepted,
                        "delete:55");
        auto options_res = request_once(port, http::verb::options, "/async/options");
        assert(options_res.result() == http::status::no_content);
        assert(options_res[http::field::allow] == "GET, POST, PUT, PATCH, DELETE, OPTIONS");
        auto head_res = request_once(port, http::verb::head, "/async/head");
        assert(head_res.result() == http::status::no_content);
        auto redirect_res = request_once(port, "/async/redirect");
        assert(redirect_res.result() == http::status::found);
        assert(redirect_res[http::field::location] == "/plain");
        expect_response(port, http::verb::post, "/async/any", "", http::status::ok, "any:POST");
        assert_keep_alive(port);
        assert_keep_alive_sequence(port);
    } catch (...) {
        ioc.stop();
        join_io_threads();
        throw;
    }

    ioc.stop();
    join_io_threads();

    assert_async_does_not_block_other_requests();

    std::cout << "http_server_smoke_test passed" << std::endl;
    return 0;
}
