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
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "async_runtime.h"
#include "http_response.h"
#include "middleware.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace urls = boost::urls;
using tcp = net::ip::tcp;
using Strand = boost::asio::strand<boost::asio::io_context::executor_type>;
using Url = urls::url;

struct RequestContext {
    Request* request{nullptr};
    Url url;
    Params params;
    std::string route_pattern;
    std::string request_id;
    std::chrono::steady_clock::time_point started_at{};
    std::chrono::steady_clock::time_point deadline{};
    std::shared_ptr<std::atomic_bool> cancelled{std::make_shared<std::atomic_bool>(false)};
    std::unordered_map<std::string, std::string> state;

    bool is_cancelled() const {
        return cancelled && cancelled->load(std::memory_order_relaxed);
    }

    void cancel() const {
        if (cancelled) {
            cancelled->store(true, std::memory_order_relaxed);
        }
    }
};

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

using AsyncMiddleware = std::function<net::awaitable<std::optional<Response>>(RequestContext&)>;

struct MiddlewareEntry {
    std::variant<MiddlewarePtr, AsyncMiddleware> handler;
};

struct RouteOptions {
    std::optional<std::chrono::milliseconds> timeout;
    std::optional<std::size_t> max_concurrent_requests;
    std::optional<std::size_t> max_request_body_size;
};

struct RouteRuntimeState {
    std::atomic<std::size_t> active_requests{0};
    std::atomic<std::uint64_t> rejected_requests{0};
};

struct RouteEntry {
    std::string pattern;
    std::variant<SyncRouteHandler, AsyncRouteHandler> handler;
    std::optional<http::verb> method;
    RouteOptions options;
    std::shared_ptr<RouteRuntimeState> runtime{std::make_shared<RouteRuntimeState>()};
};

struct HttpServerOptions {
    std::chrono::milliseconds route_timeout{std::chrono::seconds{30}};
    std::chrono::milliseconds read_timeout{std::chrono::seconds{30}};
    std::chrono::milliseconds write_timeout{std::chrono::seconds{30}};
    std::chrono::milliseconds graceful_shutdown_timeout{std::chrono::seconds{10}};
    std::size_t max_active_requests{10000};
    std::size_t max_queued_requests{0};
    std::size_t max_request_body_size{1024 * 1024};
    bool structured_access_log{false};
};

class HttpServer {
public:
    HttpServer(net::io_context& ioc, tcp::endpoint endpoint);
    HttpServer(net::io_context& ioc, tcp::endpoint endpoint, HttpServerOptions options);

    void add_route(const std::string& pattern, RouteHandler handler);
    void add_route(const std::string& pattern, RouteHandler handler, RouteOptions options);
    void add_async_route(const std::string& pattern, AsyncRouteHandler handler);
    void add_async_route(const std::string& pattern, AsyncRouteHandler handler, RouteOptions options);
    void get_async(const std::string& pattern, AsyncRouteHandler handler);
    void get_async(const std::string& pattern, AsyncRouteHandler handler, RouteOptions options);
    void post_async(const std::string& pattern, AsyncRouteHandler handler);
    void put_async(const std::string& pattern, AsyncRouteHandler handler);
    void patch_async(const std::string& pattern, AsyncRouteHandler handler);
    void delete_async(const std::string& pattern, AsyncRouteHandler handler);
    void head_async(const std::string& pattern, AsyncRouteHandler handler);
    void options_async(const std::string& pattern, AsyncRouteHandler handler);
    void any_async(const std::string& pattern, AsyncRouteHandler handler);
    void any_async(const std::string& pattern, AsyncRouteHandler handler, RouteOptions options);

    // Method for registering middleware
    void add_middleware(MiddlewarePtr middleware) { middleware_entries_.push_back(MiddlewareEntry{std::move(middleware)}); }
    void add_async_middleware(AsyncMiddleware middleware) { middleware_entries_.push_back(MiddlewareEntry{std::move(middleware)}); }

    template<typename Handler>
    void add_route(const std::string& pattern, std::shared_ptr<Handler> handler) {
        add_route(pattern, RouteHandler{[handler](
                const Request& req,
                Response& res,
                const urls::url_view& url_view,
                const Params& path_params
        ) {
            handler->handle(req, res, std::string(req.method_string()), url_view, path_params);
        }});
    }

    void set_global_route_timeout(std::chrono::milliseconds timeout) { options_.route_timeout = timeout; }
    void set_read_timeout(std::chrono::milliseconds timeout) { options_.read_timeout = timeout; }
    void set_write_timeout(std::chrono::milliseconds timeout) { options_.write_timeout = timeout; }
    void set_max_active_requests(std::size_t value) { options_.max_active_requests = value; }
    void set_max_queued_requests(std::size_t value) { options_.max_queued_requests = value; }
    void set_max_request_body_size(std::size_t value) { options_.max_request_body_size = value; }
    void set_structured_access_log(bool enabled) { options_.structured_access_log = enabled; }
    void set_route_timeout(const std::string& pattern, std::chrono::milliseconds timeout);
    void set_route_concurrency_limit(const std::string& pattern, std::size_t limit);
    void set_route_body_limit(const std::string& pattern, std::size_t limit);

    qornix::async::HttpMetrics& metrics() { return *metrics_; }
    const qornix::async::HttpMetrics& metrics() const { return *metrics_; }
    std::shared_ptr<qornix::async::HttpMetrics> metrics_ptr() const { return metrics_; }
    net::any_io_executor executor() const { return ioc_.get_executor(); }
    void add_metrics_route(const std::string& pattern = "/qornix/metrics");

    void run();
    void stop_accepting();
    void graceful_shutdown(std::chrono::milliseconds timeout = std::chrono::milliseconds{0});

private:
    void add_async_route_for_method(const std::string& pattern,
                                    std::optional<http::verb> method,
                                    AsyncRouteHandler handler,
                                    RouteOptions options = {});
    void do_accept();
    void on_accept(beast::error_code ec);

    net::io_context& ioc_;
    tcp::acceptor acceptor_;
    std::vector<RouteEntry> routes_;
    std::vector<MiddlewareEntry> middleware_entries_;
    HttpServerOptions options_;
    std::shared_ptr<qornix::async::HttpMetrics> metrics_;
    std::atomic_bool accepting_{true};
};

// Session class to handle individual connections
class Session : public std::enable_shared_from_this<Session> {
public:
    explicit Session(tcp::socket socket,
                     std::vector<RouteEntry>& routes,
                     std::vector<MiddlewareEntry>& middleware_entries,
                     HttpServerOptions& options,
                     std::shared_ptr<qornix::async::HttpMetrics> metrics);
    void run();
    void do_read();
    void on_read(beast::error_code ec, std::size_t bytes_transferred, std::uint64_t generation);
    void handle_request();
    void send_response(Response res);
    void on_write(bool close, beast::error_code ec, std::size_t bytes_transferred, std::uint64_t generation);
    void do_close();

private:
    bool match_route(const std::string& path, const std::string& pattern) const;
    void execute_route_handler();
    void start_async_route_handler(AsyncRouteHandler handler);
    void execute_middlewares(size_t index);
    void start_async_middleware(size_t index, AsyncMiddleware middleware);
    void start_read_timeout(std::uint64_t generation);
    void start_write_timeout(std::uint64_t generation);
    void start_request_timeout(std::chrono::milliseconds timeout, unsigned version, std::uint64_t generation);
    void cancel_current_request(const std::string& reason);
    bool try_begin_request();
    bool try_begin_route(RouteEntry& route);
    void finish_request(http::status status);
    void log_access(http::status status);

    tcp::socket socket_;
    Strand strand_;
    net::steady_timer read_timer_;
    net::steady_timer write_timer_;
    net::steady_timer request_timer_;
    beast::flat_buffer buffer_;
    Request req_;
    Response res_;
    std::vector<RouteEntry>& routes_;
    std::vector<MiddlewareEntry>& middleware_entries_; // Middleware reference
    HttpServerOptions& options_;
    std::shared_ptr<qornix::async::HttpMetrics> metrics_;
    urls::url_view current_url_; // Current URL to pass to middleware
    Params current_params_; // Current path parameters
    std::string matched_pattern_; // Store the pattern for use in middleware
    RequestContext current_context_;
    RouteEntry* active_route_{nullptr};
    std::chrono::steady_clock::time_point request_started_at_{};
    std::uint64_t current_generation_{0};
    bool async_response_pending_{false};
    bool response_started_{false};
    bool request_permit_acquired_{false};
    bool route_permit_acquired_{false};
    bool disconnected_{false};
    std::string cancellation_reason_;

    Params extract_parameters(const std::string &path, const std::string &pattern) const;
};
