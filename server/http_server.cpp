/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "http_server.h"
#include <boost/system/result.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>

#include "template_loader.h"

HttpServer::HttpServer(net::io_context& ioc, tcp::endpoint endpoint)
    : ioc_(ioc), acceptor_(ioc), routes_() {
    beast::error_code ec;

    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
        throw std::runtime_error("Failed to open acceptor");
    }

    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec) {
        throw std::runtime_error("Failed to set reuse address option");
    }

    acceptor_.bind(endpoint, ec);
    if (ec) {
        throw std::runtime_error("Failed to bind to endpoint");
    }

    acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec) {
        throw std::runtime_error("Failed to listen on acceptor");
    }
}

void HttpServer::add_route(const std::string& pattern, RouteHandler handler) {
    routes_.push_back(RouteEntry{pattern, SyncRouteHandler{std::move(handler)}, std::nullopt});
}

void HttpServer::add_async_route(const std::string& pattern, AsyncRouteHandler handler) {
    any_async(pattern, std::move(handler));
}

void HttpServer::get_async(const std::string& pattern, AsyncRouteHandler handler) {
    add_async_route_for_method(pattern, http::verb::get, std::move(handler));
}

void HttpServer::post_async(const std::string& pattern, AsyncRouteHandler handler) {
    add_async_route_for_method(pattern, http::verb::post, std::move(handler));
}

void HttpServer::put_async(const std::string& pattern, AsyncRouteHandler handler) {
    add_async_route_for_method(pattern, http::verb::put, std::move(handler));
}

void HttpServer::patch_async(const std::string& pattern, AsyncRouteHandler handler) {
    add_async_route_for_method(pattern, http::verb::patch, std::move(handler));
}

void HttpServer::delete_async(const std::string& pattern, AsyncRouteHandler handler) {
    add_async_route_for_method(pattern, http::verb::delete_, std::move(handler));
}

void HttpServer::head_async(const std::string& pattern, AsyncRouteHandler handler) {
    add_async_route_for_method(pattern, http::verb::head, std::move(handler));
}

void HttpServer::options_async(const std::string& pattern, AsyncRouteHandler handler) {
    add_async_route_for_method(pattern, http::verb::options, std::move(handler));
}

void HttpServer::any_async(const std::string& pattern, AsyncRouteHandler handler) {
    add_async_route_for_method(pattern, std::nullopt, std::move(handler));
}

void HttpServer::add_async_route_for_method(const std::string& pattern,
                                            std::optional<http::verb> method,
                                            AsyncRouteHandler handler) {
    routes_.push_back(RouteEntry{
        pattern,
        AsyncRouteHandler{std::move(handler)},
        method
    });
}

void HttpServer::run() {
    do_accept();
}

void HttpServer::do_accept() {
    acceptor_.async_accept(
            [this](beast::error_code ec, tcp::socket socket) {
                on_accept(ec);
                if (!ec) {
                    std::make_shared<Session>(std::move(socket), routes_, middlewares_)->run();
                }
                do_accept();
            });
}

void HttpServer::on_accept(beast::error_code ec) {
    if (ec) {
        // Handle error silently
        return;
    }
}

Session::Session(tcp::socket socket,
                 std::vector<RouteEntry>& routes,
                 std::vector<MiddlewarePtr>& middlewares
                 )
    : socket_(std::move(socket)),
      strand_(static_cast<net::io_context&>(socket_.get_executor().context()).get_executor()),
      routes_(routes),
      middlewares_(middlewares) {}

void Session::run() {
    auto self = shared_from_this();
    net::dispatch(strand_, [self]() {
        self->do_read();
    });
}

void Session::execute_route_handler() {
    current_params_.clear();
    matched_pattern_.clear();

    for (const auto& route : routes_) {
        if (route.method && req_.method() != *route.method) {
            continue;
        }

        if (match_route(std::string(current_url_.path()), route.pattern)) {
            current_params_ = extract_parameters(std::string(current_url_.path()), route.pattern);
            matched_pattern_ = route.pattern;

            if (auto handler = std::get_if<SyncRouteHandler>(&route.handler)) {
                (*handler)(req_, res_, current_url_, current_params_);
            } else {
                start_async_route_handler(std::get<AsyncRouteHandler>(route.handler));
            }
            return;
        }
    }

    res_ = make_html_response(http::status::not_found, req_.version(), TemplateLoader::load404Template());
}

void Session::start_async_route_handler(AsyncRouteHandler handler) {
    auto req = req_;
    auto url = Url{current_url_};
    auto params = current_params_;
    const auto version = req.version();
    auto self = shared_from_this();

    async_response_pending_ = true;

    try {
        net::co_spawn(strand_,
            [self,
             handler = std::move(handler),
             req = std::move(req),
             url = std::move(url),
             params = std::move(params),
             version]() mutable -> net::awaitable<void> {
                try {
                    Response res = co_await handler(std::move(req), std::move(url), std::move(params));
                    self->send_response(std::move(res));
                } catch (const std::exception& e) {
                    self->send_response(make_error_response(http::status::internal_server_error, version, e.what()));
                } catch (...) {
                    self->send_response(make_error_response(http::status::internal_server_error,
                                                            version,
                                                            "Internal Server Error"));
                }
            },
            net::detached);
    } catch (...) {
        async_response_pending_ = false;
        throw;
    }
}

// Implementation of middleware chain execution
void Session::execute_middlewares(size_t index) {
    // If the end of the middleware chain is reached, execute the main route handler
    if (index >= middlewares_.size()) {
        execute_route_handler();
        return;
    }

    // Execute the current middleware
    auto next = [this, index]() {
        execute_middlewares(index + 1);
    };

    middlewares_[index]->handle(req_, res_, current_url_, current_params_, next);
}

Params Session::extract_parameters(const std::string& path, const std::string& pattern) const {
    Params params;

    // Normalize the path and pattern: remove a trailing slash if present
    std::string normalized_path = path;
    std::string normalized_pattern = pattern;

    // Remove the trailing slash from the path if it is not root
    if (normalized_path.length() > 1 && normalized_path.back() == '/') {
        normalized_path.pop_back();
    }

    // Remove the trailing slash from the pattern if it is not root
    if (normalized_pattern.length() > 1 && normalized_pattern.back() == '/') {
        normalized_pattern.pop_back();
    }

    // Split the path and pattern into segments
    std::vector<std::string> path_segments;
    std::vector<std::string> pattern_segments;

    // Simple split by '/'
    size_t start = 0, end = 0;
    while ((end = normalized_path.find('/', start)) != std::string::npos) {
        if (end > start) {
            path_segments.push_back(normalized_path.substr(start, end - start));
        }
        start = end + 1;
    }
    if (start < normalized_path.length()) {
        path_segments.push_back(normalized_path.substr(start));
    }

    start = 0;
    while ((end = normalized_pattern.find('/', start)) != std::string::npos) {
        if (end > start) {
            pattern_segments.push_back(normalized_pattern.substr(start, end - start));
        }
        start = end + 1;
    }
    if (start < normalized_pattern.length()) {
        pattern_segments.push_back(normalized_pattern.substr(start));
    }

    // Check segment count match
    if (path_segments.size() != pattern_segments.size()) {
        return params; // An empty map indicates no match
    }

    // Extract parameters
    for (size_t i = 0; i < pattern_segments.size(); ++i) {
        const std::string& pattern_seg = pattern_segments[i];
        const std::string& path_seg = path_segments[i];

        if (pattern_seg.front() == '{' && pattern_seg.back() == '}') {
            // This is a parameter
            std::string param_name = pattern_seg.substr(1, pattern_seg.length() - 2);
            params[param_name] = path_seg;
        } else if (pattern_seg != path_seg) {
            // Literal segment does not match
            return {}; // Return an empty map to indicate no match
        }
    }

    return params;
}

// Route matching implementation
bool Session::match_route(const std::string& path, const std::string& pattern) const {
    // Normalize the path and pattern: remove a trailing slash if present
    std::string normalized_path = path;
    std::string normalized_pattern = pattern;

    // Remove the trailing slash from the path if it is not root
    if (normalized_path.length() > 1 && normalized_path.back() == '/') {
        normalized_path.pop_back();
    }

    // Remove the trailing slash from the pattern if it is not root
    if (normalized_pattern.length() > 1 && normalized_pattern.back() == '/') {
        normalized_pattern.pop_back();
    }

    // Check exact match
    if (normalized_pattern == normalized_path) {
        return true;
    }

    // Check parameterized match
    auto params = extract_parameters(normalized_path, normalized_pattern);
    return !params.empty();
}


void Session::do_read() {
    auto self = shared_from_this();
    // Create a new request for each read
    req_ = Request{};  // Explicitly create a new empty request
    buffer_.consume(buffer_.size());  // Clear buffer

    http::async_read(socket_, buffer_, req_,
        net::bind_executor(strand_, [self](beast::error_code ec, std::size_t bytes_transferred) {
            self->on_read(ec, bytes_transferred);
        }));
}

void Session::on_read(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    if (ec == http::error::end_of_stream) {
        do_close();
        return;
    }

    if (ec) {
        // Handle error
        return;
    }

    handle_request();
}

void Session::handle_request() {
    current_params_.clear();
    matched_pattern_.clear();
    async_response_pending_ = false;

    // Parse URL
    boost::system::result<urls::url_view> ru = urls::parse_uri_reference(req_.target());

    if (!ru) {
        send_response(make_error_response(http::status::bad_request, req_.version(), "Invalid URL"));
        return;
    }

    current_url_ = *ru;

    // Initialize response
    res_ = Response{http::status::ok, req_.version()};

    try {
        // Start executing the middleware chain from the first item
        if (!middlewares_.empty()) {
            execute_middlewares(0);
        } else {
            execute_route_handler();
        }
    } catch (const std::exception& e) {
        if (!async_response_pending_) {
            send_response(make_error_response(http::status::internal_server_error, req_.version(), e.what()));
        }
        return;
    } catch (...) {
        if (!async_response_pending_) {
            send_response(make_error_response(http::status::internal_server_error,
                                              req_.version(),
                                              "Internal Server Error"));
        }
        return;
    }

    if (!async_response_pending_) {
        send_response(std::move(res_));
    }
}

void Session::send_response(Response res) {
    // Set common headers
    res.set(http::field::server, "Boost Beast Server");
    res.keep_alive(req_.keep_alive());
    res.prepare_payload();

    res_ = std::move(res);

    auto self = shared_from_this();
    http::async_write(socket_, res_,
        net::bind_executor(strand_, [self](beast::error_code ec, std::size_t bytes_transferred) {
            self->on_write(bool(self->res_.need_eof()), ec, bytes_transferred);
        }));
}

void Session::on_write(bool close, beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    if (ec) {
        // Handle error
        return;
    }

    if (close) {
        do_close();
        return;
    }

    // Continue reading: clear request and response before a new read
    req_ = {};
    res_ = {};
    do_read();
}

void Session::do_close() {
    beast::error_code ec;
    socket_.shutdown(tcp::socket::shutdown_send, ec);

    // Ignore errors during shutdown
    boost::ignore_unused(ec);
}
