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
    routes_.emplace_back(pattern, handler);
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
                 std::vector<std::pair<std::string,
                 RouteHandler>>& routes,
                 std::vector<MiddlewarePtr>& middlewares
                 )
    : socket_(std::move(socket)), routes_(routes), middlewares_(middlewares) {}

void Session::run() {
    do_read();
}

// Implementation of middleware chain execution
void Session::execute_middlewares(size_t index) {
    // If the end of the middleware chain is reached, execute the main route handler
    if (index >= middlewares_.size()) {
        // Find the matching route and execute its handler
        bool route_found = false;
        for (const auto& [pattern, handler] : routes_) {
            if (match_route(std::string(current_url_.path()), pattern)) {
                current_params_ = extract_parameters(std::string(current_url_.path()), pattern);
                handler(req_, res_, current_url_, current_params_);
                route_found = true;
                break;
            }
        }

        if (!route_found) {
            res_ = http::response<http::string_body>{http::status::not_found, req_.version()};
            res_.set(http::field::content_type, "text/html");
            res_.body() = TemplateLoader::load404Template();
            res_.prepare_payload();
        }
        return;
    }

    // Execute the current middleware
    auto next = [this, index]() {
        execute_middlewares(index + 1);
    };

    middlewares_[index]->handle(req_, res_, current_url_, current_params_, next);
}

std::map<std::string, std::string> Session::extract_parameters(const std::string& path, const std::string& pattern) const {
    std::map<std::string, std::string> params;

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
    req_ = http::request<http::string_body>{};  // Explicitly create a new empty request
    buffer_.consume(buffer_.size());  // Clear buffer

    http::async_read(socket_, buffer_, req_,
        [self](beast::error_code ec, std::size_t bytes_transferred) {
            self->on_read(ec, bytes_transferred);
        });
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

    // Parse URL
    boost::system::result<urls::url_view> ru = urls::parse_uri_reference(req_.target());

    if (!ru) {
        res_ = http::response<http::string_body>{http::status::bad_request, req_.version()};
        res_.set(http::field::content_type, "text/plain");
        res_.body() = "Invalid URL";
        res_.prepare_payload();
        do_close();
        return;
    }

    current_url_ = *ru;

    // Initialize response
    res_ = {};

    // Start executing the middleware chain from the first item
    if (!middlewares_.empty()) {
        execute_middlewares(0);
    } else {
        // If there is no middleware, handle the route directly
        bool route_found = false;
        for (const auto& [pattern, handler] : routes_) {
            if (match_route(std::string(current_url_.path()), pattern)) {
                auto params = extract_parameters(std::string(current_url_.path()), pattern);
                handler(req_, res_, current_url_, params);
                route_found = true;
                break;
            }
        }

        if (!route_found) {
            res_ = http::response<http::string_body>{http::status::not_found, req_.version()};
            res_.set(http::field::content_type, "text/html");
            res_.body() = TemplateLoader::load404Template();
            res_.prepare_payload();
        }
    }

    // Set common headers
    res_.set(http::field::server, "Boost Beast Server");
    res_.keep_alive(req_.keep_alive());

    auto self = shared_from_this();
    http::async_write(socket_, res_,
        [self](beast::error_code ec, std::size_t bytes_transferred) {
            self->on_write(bool(self->res_.need_eof()), ec, bytes_transferred);
        });
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
