/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#ifndef MIDDLEWARE_H
#define MIDDLEWARE_H

#include <boost/beast/http.hpp>
#include <boost/url.hpp>
#include <memory>
#include <functional>
#include <map>

namespace beast = boost::beast;
namespace http = beast::http;
namespace urls = boost::urls;

// Type for the next handler in the chain
using NextHandler = std::function<void()>;

// Base class for middleware
class Middleware {
public:
    virtual ~Middleware() = default;

    // Request handling method
    virtual void handle(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view& url_view,
        const std::map<std::string, std::string>& path_params,
        NextHandler next
    ) = 0;
};

// Shared pointer for middleware
using MiddlewarePtr = std::shared_ptr<Middleware>;

#endif // MIDDLEWARE_H
