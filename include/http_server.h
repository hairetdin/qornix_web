/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>
#include <boost/url.hpp>
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <map>

#include "middleware.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace urls = boost::urls;
using tcp = net::ip::tcp;

// Define the handler type directly in the server header file
using RouteHandler = std::function<void(
    const http::request<http::string_body>&,
    http::response<http::string_body>&,
    const urls::url_view&,
    const std::map<std::string, std::string>&
)>;

class HttpServer {
public:
    HttpServer(net::io_context& ioc, tcp::endpoint endpoint);

    void add_route(const std::string& pattern, RouteHandler handler);

    // Method for registering middleware
    void add_middleware(MiddlewarePtr middleware) {middlewares_.push_back(middleware);}

    template<typename Handler>
    void add_route(const std::string& pattern, std::shared_ptr<Handler> handler) {
        routes_.emplace_back(pattern, [handler](
                const http::request<http::string_body>& req,
                http::response<http::string_body>& res,
                const urls::url_view& url_view,
                const std::map<std::string, std::string>& path_params
        ) {
            handler->handle(req, res, std::string(req.method_string()), url_view, path_params);
        });
    }

    void run();

private:
    void do_accept();
    void on_accept(beast::error_code ec);

    net::io_context& ioc_;
    tcp::acceptor acceptor_;
    std::vector<std::pair<std::string, RouteHandler>> routes_;
    std::vector<MiddlewarePtr> middlewares_; // Middleware list
};

// Session class to handle individual connections
class Session : public std::enable_shared_from_this<Session> {
public:
    explicit Session(tcp::socket socket,
                     std::vector<std::pair<std::string,
                     RouteHandler>>& routes,
                     std::vector<MiddlewarePtr>& middlewares);
    void run();
    void do_read();
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void on_write(bool close, beast::error_code ec, std::size_t bytes_transferred);
    void do_close();

private:
    bool match_route(const std::string& path, const std::string& pattern) const;
    void execute_middlewares(size_t index);

    tcp::socket socket_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    http::response<http::string_body> res_;
    std::vector<std::pair<std::string, RouteHandler>>& routes_;
    std::vector<MiddlewarePtr>& middlewares_; // Middleware reference
    urls::url_view current_url_; // Current URL to pass to middleware
    std::map<std::string, std::string> current_params_; // Current path parameters
    std::string matched_pattern_; // Store the pattern for use in middleware


    std::map<std::string, std::string> extract_parameters(const std::string &path, const std::string &pattern) const;
};
