/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#ifndef MIDDLEWARE_H
#define MIDDLEWARE_H

#include <boost/url.hpp>
#include <memory>
#include <functional>
#include <map>

#include "http_response.h"

namespace urls = boost::urls;

// Type for the next handler in the chain
using NextHandler = std::function<void()>;

// Base class for middleware
class Middleware {
public:
    virtual ~Middleware() = default;

    // Request handling method
    virtual void handle(
        const Request& req,
        Response& res,
        const urls::url_view& url_view,
        const Params& path_params,
        NextHandler next
    ) = 0;
};

// Shared pointer for middleware
using MiddlewarePtr = std::shared_ptr<Middleware>;

#endif // MIDDLEWARE_H
