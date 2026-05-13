/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "http_server.h"

#include <boost/system/result.hpp>

#include <algorithm>
#include <iostream>
#include <map>
#include <sstream>

#include "template_loader.h"

namespace {

std::atomic<std::uint64_t> global_request_id{0};

bool duration_enabled(std::chrono::milliseconds value) {
    return value.count() > 0;
}

std::string make_request_id() {
    return std::to_string(global_request_id.fetch_add(1, std::memory_order_relaxed) + 1);
}

} // namespace

HttpServer::HttpServer(net::io_context& ioc, tcp::endpoint endpoint)
    : HttpServer(ioc, endpoint, HttpServerOptions{}) {}

HttpServer::HttpServer(net::io_context& ioc, tcp::endpoint endpoint, HttpServerOptions options)
    : ioc_(ioc), acceptor_(ioc), routes_(), options_(options),
      metrics_(std::make_shared<qornix::async::HttpMetrics>()) {
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
    add_route(pattern, std::move(handler), RouteOptions{});
}

void HttpServer::add_route(const std::string& pattern, RouteHandler handler, RouteOptions options) {
    routes_.push_back(RouteEntry{
        pattern,
        SyncRouteHandler{std::move(handler)},
        std::nullopt,
        std::move(options),
        std::make_shared<RouteRuntimeState>()
    });
}

void HttpServer::add_async_route(const std::string& pattern, AsyncRouteHandler handler) {
    any_async(pattern, std::move(handler));
}

void HttpServer::add_async_route(const std::string& pattern,
                                 AsyncRouteHandler handler,
                                 RouteOptions options) {
    any_async(pattern, std::move(handler), std::move(options));
}

void HttpServer::get_async(const std::string& pattern, AsyncRouteHandler handler) {
    add_async_route_for_method(pattern, http::verb::get, std::move(handler));
}

void HttpServer::get_async(const std::string& pattern,
                           AsyncRouteHandler handler,
                           RouteOptions options) {
    add_async_route_for_method(pattern, http::verb::get, std::move(handler), std::move(options));
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

void HttpServer::any_async(const std::string& pattern,
                           AsyncRouteHandler handler,
                           RouteOptions options) {
    add_async_route_for_method(pattern, std::nullopt, std::move(handler), std::move(options));
}

void HttpServer::add_async_route_for_method(const std::string& pattern,
                                            std::optional<http::verb> method,
                                            AsyncRouteHandler handler,
                                            RouteOptions options) {
    routes_.push_back(RouteEntry{
        pattern,
        AsyncRouteHandler{std::move(handler)},
        method,
        std::move(options),
        std::make_shared<RouteRuntimeState>()
    });
}

void HttpServer::set_route_timeout(const std::string& pattern, std::chrono::milliseconds timeout) {
    for (auto& route : routes_) {
        if (route.pattern == pattern) {
            route.options.timeout = timeout;
        }
    }
}

void HttpServer::set_route_concurrency_limit(const std::string& pattern, std::size_t limit) {
    for (auto& route : routes_) {
        if (route.pattern == pattern) {
            route.options.max_concurrent_requests = limit;
        }
    }
}

void HttpServer::set_route_body_limit(const std::string& pattern, std::size_t limit) {
    for (auto& route : routes_) {
        if (route.pattern == pattern) {
            route.options.max_request_body_size = limit;
        }
    }
}

void HttpServer::add_metrics_route(const std::string& pattern) {
    auto metrics = metrics_;
    add_route(pattern, [metrics](const Request& req, Response& res, const urls::url_view&, const Params&) {
        res = response::json(metrics->to_json(), req.version());
    });
}

void HttpServer::run() {
    accepting_.store(true, std::memory_order_relaxed);
    do_accept();
}

void HttpServer::stop_accepting() {
    accepting_.store(false, std::memory_order_relaxed);
    net::post(ioc_, [this]() {
        beast::error_code ec;
        acceptor_.cancel(ec);
        acceptor_.close(ec);
    });
}

void HttpServer::graceful_shutdown(std::chrono::milliseconds timeout) {
    stop_accepting();
    const auto effective_timeout = timeout.count() > 0 ? timeout : options_.graceful_shutdown_timeout;
    const auto deadline = std::chrono::steady_clock::now() + effective_timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (metrics_->snapshot().active_requests == 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    ioc_.stop();
}

void HttpServer::do_accept() {
    if (!accepting_.load(std::memory_order_relaxed) || !acceptor_.is_open()) {
        return;
    }

    acceptor_.async_accept(
            [this](beast::error_code ec, tcp::socket socket) {
                on_accept(ec);
                if (!ec) {
                    std::make_shared<Session>(std::move(socket),
                                              routes_,
                                              middleware_entries_,
                                              options_,
                                              metrics_)->run();
                }
                if (accepting_.load(std::memory_order_relaxed) && acceptor_.is_open()) {
                    do_accept();
                }
            });
}

void HttpServer::on_accept(beast::error_code ec) {
    if (ec) {
        // Handle error silently. During graceful shutdown the acceptor is closed intentionally.
        return;
    }
}

Session::Session(tcp::socket socket,
                 std::vector<RouteEntry>& routes,
                 std::vector<MiddlewareEntry>& middleware_entries,
                 HttpServerOptions& options,
                 std::shared_ptr<qornix::async::HttpMetrics> metrics)
    : socket_(std::move(socket)),
      strand_(static_cast<net::io_context&>(socket_.get_executor().context()).get_executor()),
      read_timer_(strand_),
      write_timer_(strand_),
      request_timer_(strand_),
      routes_(routes),
      middleware_entries_(middleware_entries),
      options_(options),
      metrics_(std::move(metrics)) {}

void Session::run() {
    auto self = shared_from_this();
    net::dispatch(strand_, [self]() {
        self->do_read();
    });
}

bool Session::try_begin_request() {
    const auto snapshot = metrics_->snapshot();
    if (snapshot.active_requests >= options_.max_active_requests) {
        metrics_->record_request_rejected();
        return false;
    }
    request_permit_acquired_ = true;
    request_started_at_ = std::chrono::steady_clock::now();
    metrics_->record_request_started();
    return true;
}

bool Session::try_begin_route(RouteEntry& route) {
    if (!route.options.max_concurrent_requests) {
        active_route_ = &route;
        return true;
    }

    auto current = route.runtime->active_requests.load(std::memory_order_relaxed);
    while (current < *route.options.max_concurrent_requests) {
        if (route.runtime->active_requests.compare_exchange_weak(current,
                                                                 current + 1,
                                                                 std::memory_order_relaxed)) {
            active_route_ = &route;
            route_permit_acquired_ = true;
            return true;
        }
    }

    route.runtime->rejected_requests.fetch_add(1, std::memory_order_relaxed);
    metrics_->record_request_rejected();
    return false;
}

void Session::execute_route_handler() {
    current_params_.clear();
    matched_pattern_.clear();

    for (auto& route : routes_) {
        if (route.method && req_.method() != *route.method) {
            continue;
        }

        if (match_route(std::string(current_url_.path()), route.pattern)) {
            current_params_ = extract_parameters(std::string(current_url_.path()), route.pattern);
            matched_pattern_ = route.pattern;
            current_context_.params = current_params_;
            current_context_.route_pattern = matched_pattern_;

            const auto body_limit = route.options.max_request_body_size.value_or(options_.max_request_body_size);
            if (req_.body().size() > body_limit) {
                res_ = make_error_response(http::status::payload_too_large,
                                           req_.version(),
                                           "Request body is too large");
                metrics_->record_request_rejected();
                return;
            }

            if (!try_begin_route(route)) {
                res_ = make_error_response(http::status::too_many_requests,
                                           req_.version(),
                                           "Too Many Requests");
                return;
            }

            const auto timeout = route.options.timeout.value_or(options_.route_timeout);
            if (std::holds_alternative<AsyncRouteHandler>(route.handler)) {
                start_request_timeout(timeout, req_.version(), current_generation_);
            }
            current_context_.deadline = std::chrono::steady_clock::now() + timeout;

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
    const auto generation = current_generation_;
    auto self = shared_from_this();

    async_response_pending_ = true;

    try {
        net::co_spawn(strand_,
            [self,
             handler = std::move(handler),
             req = std::move(req),
             url = std::move(url),
             params = std::move(params),
             version,
             generation]() mutable -> net::awaitable<void> {
                try {
                    Response res = co_await handler(std::move(req), std::move(url), std::move(params));
                    if (self->current_generation_ == generation &&
                        !self->response_started_ &&
                        !self->current_context_.is_cancelled() &&
                        !self->disconnected_) {
                        self->send_response(std::move(res));
                    }
                } catch (const qornix::async::TimeoutError& e) {
                    if (self->current_generation_ == generation && !self->response_started_) {
                        self->metrics_->record_request_timed_out();
                        self->send_response(make_error_response(http::status::gateway_timeout, version, e.what()));
                    }
                } catch (const qornix::async::OverloadError& e) {
                    if (self->current_generation_ == generation && !self->response_started_) {
                        self->metrics_->record_request_rejected();
                        self->send_response(make_error_response(http::status::service_unavailable, version, e.what()));
                    }
                } catch (const std::exception& e) {
                    if (self->current_generation_ == generation && !self->response_started_) {
                        self->send_response(make_error_response(http::status::internal_server_error, version, e.what()));
                    }
                } catch (...) {
                    if (self->current_generation_ == generation && !self->response_started_) {
                        self->send_response(make_error_response(http::status::internal_server_error,
                                                                version,
                                                                "Internal Server Error"));
                    }
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
    if (index >= middleware_entries_.size()) {
        execute_route_handler();
        return;
    }

    auto& entry = middleware_entries_[index];
    if (auto sync = std::get_if<MiddlewarePtr>(&entry.handler)) {
        auto next = [this, index]() {
            execute_middlewares(index + 1);
        };
        (*sync)->handle(req_, res_, current_url_, current_params_, next);
        return;
    }

    start_async_middleware(index, std::get<AsyncMiddleware>(entry.handler));
}

void Session::start_async_middleware(size_t index, AsyncMiddleware middleware) {
    const auto generation = current_generation_;
    const auto version = req_.version();
    auto self = shared_from_this();
    async_response_pending_ = true;

    net::co_spawn(strand_,
        [self, index, middleware = std::move(middleware), generation, version]() mutable -> net::awaitable<void> {
            try {
                auto maybe_response = co_await middleware(self->current_context_);
                if (self->current_generation_ != generation || self->response_started_ || self->disconnected_) {
                    co_return;
                }

                if (maybe_response) {
                    self->send_response(std::move(*maybe_response));
                    co_return;
                }

                self->async_response_pending_ = false;
                self->execute_middlewares(index + 1);
                if (!self->async_response_pending_ && !self->response_started_) {
                    self->send_response(std::move(self->res_));
                }
            } catch (const qornix::async::OverloadError& e) {
                if (self->current_generation_ == generation && !self->response_started_) {
                    self->metrics_->record_request_rejected();
                    self->send_response(make_error_response(http::status::service_unavailable, version, e.what()));
                }
            } catch (const qornix::async::TimeoutError& e) {
                if (self->current_generation_ == generation && !self->response_started_) {
                    self->metrics_->record_request_timed_out();
                    self->send_response(make_error_response(http::status::gateway_timeout, version, e.what()));
                }
            } catch (const std::exception& e) {
                if (self->current_generation_ == generation && !self->response_started_) {
                    self->send_response(make_error_response(http::status::internal_server_error, version, e.what()));
                }
            } catch (...) {
                if (self->current_generation_ == generation && !self->response_started_) {
                    self->send_response(make_error_response(http::status::internal_server_error,
                                                            version,
                                                            "Internal Server Error"));
                }
            }
        },
        net::detached);
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

void Session::start_read_timeout(std::uint64_t generation) {
    read_timer_.cancel();
    if (!duration_enabled(options_.read_timeout)) {
        return;
    }
    auto self = shared_from_this();
    read_timer_.expires_after(options_.read_timeout);
    read_timer_.async_wait(net::bind_executor(strand_, [self, generation](beast::error_code ec) {
        if (!ec && self->current_generation_ == generation && !self->response_started_) {
            self->cancel_current_request("read_timeout");
            beast::error_code ignored;
            self->socket_.cancel(ignored);
        }
    }));
}

void Session::start_write_timeout(std::uint64_t generation) {
    write_timer_.cancel();
    if (!duration_enabled(options_.write_timeout)) {
        return;
    }
    auto self = shared_from_this();
    write_timer_.expires_after(options_.write_timeout);
    write_timer_.async_wait(net::bind_executor(strand_, [self, generation](beast::error_code ec) {
        if (!ec && self->current_generation_ == generation) {
            self->cancel_current_request("write_timeout");
            beast::error_code ignored;
            self->socket_.cancel(ignored);
        }
    }));
}

void Session::start_request_timeout(std::chrono::milliseconds timeout,
                                    unsigned version,
                                    std::uint64_t generation) {
    request_timer_.cancel();
    if (!duration_enabled(timeout)) {
        return;
    }
    auto self = shared_from_this();
    request_timer_.expires_after(timeout);
    request_timer_.async_wait(net::bind_executor(strand_, [self, version, generation](beast::error_code ec) {
        if (!ec && self->current_generation_ == generation && !self->response_started_) {
            self->metrics_->record_request_timed_out();
            self->cancel_current_request("route_timeout");
            self->send_response(make_error_response(http::status::gateway_timeout,
                                                    version,
                                                    "Gateway Timeout"));
        }
    }));
}

void Session::do_read() {
    auto self = shared_from_this();
    // Create a new request for each read
    req_ = Request{};  // Explicitly create a new empty request
    buffer_.consume(buffer_.size());  // Clear buffer
    response_started_ = false;
    async_response_pending_ = false;
    request_permit_acquired_ = false;
    route_permit_acquired_ = false;
    active_route_ = nullptr;
    disconnected_ = false;
    cancellation_reason_.clear();
    ++current_generation_;
    const auto generation = current_generation_;

    start_read_timeout(generation);
    http::async_read(socket_, buffer_, req_,
        net::bind_executor(strand_, [self, generation](beast::error_code ec, std::size_t bytes_transferred) {
            self->on_read(ec, bytes_transferred, generation);
        }));
}

void Session::on_read(beast::error_code ec, std::size_t bytes_transferred, std::uint64_t generation) {
    boost::ignore_unused(bytes_transferred);
    if (generation != current_generation_) {
        return;
    }
    read_timer_.cancel();

    if (ec == http::error::end_of_stream) {
        cancel_current_request("client_disconnect");
        do_close();
        return;
    }

    if (ec) {
        cancel_current_request("read_error");
        return;
    }

    handle_request();
}

void Session::handle_request() {
    current_params_.clear();
    matched_pattern_.clear();
    async_response_pending_ = false;
    response_started_ = false;
    request_permit_acquired_ = false;
    route_permit_acquired_ = false;
    active_route_ = nullptr;
    cancellation_reason_.clear();

    // Parse URL
    boost::system::result<urls::url_view> ru = urls::parse_uri_reference(req_.target());

    if (!ru) {
        send_response(make_error_response(http::status::bad_request, req_.version(), "Invalid URL"));
        return;
    }

    current_url_ = *ru;

    if (req_.body().size() > options_.max_request_body_size) {
        metrics_->record_request_rejected();
        send_response(make_error_response(http::status::payload_too_large,
                                          req_.version(),
                                          "Request body is too large"));
        return;
    }

    if (!try_begin_request()) {
        send_response(make_error_response(http::status::service_unavailable,
                                          req_.version(),
                                          "Service Unavailable"));
        return;
    }

    current_context_ = RequestContext{};
    current_context_.request = &req_;
    current_context_.url = Url{current_url_};
    current_context_.request_id = make_request_id();
    current_context_.started_at = request_started_at_;
    current_context_.deadline = request_started_at_ + options_.route_timeout;
    start_request_timeout(options_.route_timeout, req_.version(), current_generation_);

    // Initialize response
    res_ = Response{http::status::ok, req_.version()};

    try {
        // Start executing the middleware chain from the first item
        if (!middleware_entries_.empty()) {
            execute_middlewares(0);
        } else {
            execute_route_handler();
        }
    } catch (const std::exception& e) {
        if (!async_response_pending_ && !response_started_) {
            send_response(make_error_response(http::status::internal_server_error, req_.version(), e.what()));
        }
        return;
    } catch (...) {
        if (!async_response_pending_ && !response_started_) {
            send_response(make_error_response(http::status::internal_server_error,
                                              req_.version(),
                                              "Internal Server Error"));
        }
        return;
    }

    if (!async_response_pending_ && !response_started_) {
        send_response(std::move(res_));
    }
}

void Session::send_response(Response res) {
    if (response_started_ || disconnected_) {
        return;
    }
    response_started_ = true;
    async_response_pending_ = false;
    request_timer_.cancel();

    // Set common headers
    res.set(http::field::server, "Boost Beast Server");
    res.keep_alive(req_.keep_alive());
    res.prepare_payload();

    res_ = std::move(res);

    const auto generation = current_generation_;
    auto self = shared_from_this();
    start_write_timeout(generation);
    http::async_write(socket_, res_,
        net::bind_executor(strand_, [self, generation](beast::error_code ec, std::size_t bytes_transferred) {
            self->on_write(bool(self->res_.need_eof()), ec, bytes_transferred, generation);
        }));
}

void Session::on_write(bool close,
                       beast::error_code ec,
                       std::size_t bytes_transferred,
                       std::uint64_t generation) {
    boost::ignore_unused(bytes_transferred);
    if (generation != current_generation_) {
        return;
    }
    write_timer_.cancel();

    const auto status = res_.result();
    if (ec) {
        cancel_current_request("write_error");
        finish_request(status);
        return;
    }

    finish_request(status);

    if (close) {
        do_close();
        return;
    }

    // Continue reading: clear request and response before a new read
    req_ = {};
    res_ = {};
    do_read();
}

void Session::cancel_current_request(const std::string& reason) {
    cancellation_reason_ = reason;
    disconnected_ = reason == "client_disconnect" || reason == "read_error" || reason == "write_error";
    current_context_.cancel();
    metrics_->record_request_cancelled();
}

void Session::finish_request(http::status status) {
    request_timer_.cancel();
    read_timer_.cancel();
    write_timer_.cancel();

    if (route_permit_acquired_ && active_route_) {
        auto& active = active_route_->runtime->active_requests;
        auto current = active.load(std::memory_order_relaxed);
        while (current > 0 && !active.compare_exchange_weak(current,
                                                            current - 1,
                                                            std::memory_order_relaxed)) {
        }
    }

    if (request_permit_acquired_) {
        metrics_->record_request_finished(status, std::chrono::steady_clock::now() - request_started_at_);
    }

    log_access(status);

    request_permit_acquired_ = false;
    route_permit_acquired_ = false;
    active_route_ = nullptr;
}

void Session::log_access(http::status status) {
    if (!options_.structured_access_log) {
        return;
    }
    const auto latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - request_started_at_).count();
    std::clog << "event=qornix_access"
              << " request_id=" << current_context_.request_id
              << " method=" << req_.method_string()
              << " target=\"" << req_.target() << "\""
              << " route=\"" << matched_pattern_ << "\""
              << " status=" << static_cast<unsigned>(status)
              << " duration_ms=" << latency_ms;
    if (!cancellation_reason_.empty()) {
        std::clog << " cancellation_reason=" << cancellation_reason_;
    }
    std::clog << std::endl;
}

void Session::do_close() {
    beast::error_code ec;
    request_timer_.cancel();
    read_timer_.cancel();
    write_timer_.cancel();
    socket_.shutdown(tcp::socket::shutdown_send, ec);

    // Ignore errors during shutdown
    boost::ignore_unused(ec);
}
