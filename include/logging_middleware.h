/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#ifndef LOGGING_MIDDLEWARE_H
#define LOGGING_MIDDLEWARE_H

#include "middleware.h"
#include <boost/log/trivial.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <chrono>
#include <memory>

class LoggingMiddleware : public Middleware {
public:
    LoggingMiddleware(bool enabled = true)
        : enabled_(enabled) {
       if(enabled_) {
            BOOST_LOG_TRIVIAL(info) << "LoggingMiddleware enabled";
        }
    }

    void enable(bool enabled) {
        enabled_ = enabled;
       if (enabled_) {
            BOOST_LOG_TRIVIAL(info) << "LoggingMiddleware enabled";
        } else {
            BOOST_LOG_TRIVIAL(info) << "LoggingMiddleware disabled";
        }
    }

    void handle(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view& url_view,
        const std::map<std::string, std::string>& path_params,
        NextHandler next
    ) override {
       if (!enabled_) {
            next();
            return;
        }

        auto start_time = std::chrono::high_resolution_clock::now();
        std::string method_str(req.method_string());
        std::string target_str(req.target());
        std::string path_str(std::string(url_view.path()));

        // Log the start of the request
        BOOST_LOG_TRIVIAL(info) << "📥 REQUEST: " << method_str << " " << target_str;
        BOOST_LOG_TRIVIAL(debug) << "Path: " << path_str << ", Body size: " << req.body().size() << " bytes";

        // Execute the middleware chain
        next();

        // Log request completion
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        std::string status_str = std::to_string(res.result_int());
        std::string response_size = std::to_string(res.body().size());
        double duration_ms = duration.count() / 1000.0;

        // Logging depending on the response status
       if (res.result_int() >= 500) {
            BOOST_LOG_TRIVIAL(error) << "📤 RESPONSE: " << status_str
                                     << " (" << response_size << " bytes) in "
                                     << duration_ms << "ms [SERVER ERROR]";
        } else if (res.result_int() >= 400) {
            BOOST_LOG_TRIVIAL(warning) << "📤 RESPONSE: " << status_str
                                       << " (" << response_size << " bytes) in "
                                       << duration_ms << "ms [CLIENT ERROR]";
        } else {
            BOOST_LOG_TRIVIAL(info) << "📤 RESPONSE: " << status_str
                                    << " (" << response_size << " bytes) in "
                                    << duration_ms << "ms";
        }

        BOOST_LOG_TRIVIAL(trace) << "Response status: " << res.result_int()
                                 << " content_type: " << res.count(http::field::content_type);
    }

private:
    bool enabled_;
};

inline std::shared_ptr<LoggingMiddleware> create_logging_middleware(bool enabled = true) {
    return std::make_shared<LoggingMiddleware>(enabled);
}

#endif // LOGGING_MIDDLEWARE_H
