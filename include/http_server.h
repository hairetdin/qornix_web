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
#include <optional>
#include <variant>

#include "http_response.h"
#include "middleware.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace urls = boost::urls;
using tcp = net::ip::tcp;
using Strand = boost::asio::strand<boost::asio::io_context::executor_type>;
using Url = urls::url;

// Define the handler type directly in the server header file
using SyncRouteHandler = std::function<void(
    const Request&,
    Response&,
    const urls::url_view&,
    const Params&
)>;
using RouteHandler = SyncRouteHandler;

using AsyncRouteHandler = std::function<net::awaitable<Response>(
    Request,
    Url,
    Params
)>;

struct RouteEntry {
    std::string pattern;
    std::variant<SyncRouteHandler, AsyncRouteHandler> handler;
    std::optional<http::verb> method;
};

class HttpServer {
public:
    HttpServer(net::io_context& ioc, tcp::endpoint endpoint);

    void add_route(const std::string& pattern, RouteHandler handler);
    void add_async_route(const std::string& pattern, AsyncRouteHandler handler);
    void get_async(const std::string& pattern, AsyncRouteHandler handler);
    void post_async(const std::string& pattern, AsyncRouteHandler handler);
    void put_async(const std::string& pattern, AsyncRouteHandler handler);
    void patch_async(const std::string& pattern, AsyncRouteHandler handler);
    void delete_async(const std::string& pattern, AsyncRouteHandler handler);
    void head_async(const std::string& pattern, AsyncRouteHandler handler);
    void options_async(const std::string& pattern, AsyncRouteHandler handler);
    void any_async(const std::string& pattern, AsyncRouteHandler handler);

    // Method for registering middleware
    void add_middleware(MiddlewarePtr middleware) {middlewares_.push_back(middleware);}

    template<typename Handler>
    void add_route(const std::string& pattern, std::shared_ptr<Handler> handler) {
        routes_.push_back(RouteEntry{pattern, RouteHandler{[handler](
                const Request& req,
                Response& res,
                const urls::url_view& url_view,
                const Params& path_params
        ) {
            handler->handle(req, res, std::string(req.method_string()), url_view, path_params);
        }}, std::nullopt});
    }

    void run();

private:
    void add_async_route_for_method(const std::string& pattern,
                                    std::optional<http::verb> method,
                                    AsyncRouteHandler handler);
    void do_accept();
    void on_accept(beast::error_code ec);

    net::io_context& ioc_;
    tcp::acceptor acceptor_;
    std::vector<RouteEntry> routes_;
    std::vector<MiddlewarePtr> middlewares_; // Middleware list
};

// Session class to handle individual connections
class Session : public std::enable_shared_from_this<Session> {
public:
    explicit Session(tcp::socket socket,
                     std::vector<RouteEntry>& routes,
                     std::vector<MiddlewarePtr>& middlewares);
    void run();
    void do_read();
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void handle_request();
    void send_response(Response res);
    void on_write(bool close, beast::error_code ec, std::size_t bytes_transferred);
    void do_close();

private:
    bool match_route(const std::string& path, const std::string& pattern) const;
    void execute_route_handler();
    void start_async_route_handler(AsyncRouteHandler handler);
    void execute_middlewares(size_t index);

    tcp::socket socket_;
    Strand strand_;
    beast::flat_buffer buffer_;
    Request req_;
    Response res_;
    std::vector<RouteEntry>& routes_;
    std::vector<MiddlewarePtr>& middlewares_; // Middleware reference
    urls::url_view current_url_; // Current URL to pass to middleware
    Params current_params_; // Current path parameters
    std::string matched_pattern_; // Store the pattern for use in middleware
    bool async_response_pending_{false};


    Params extract_parameters(const std::string &path, const std::string &pattern) const;
};
