/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include "handler_base.h"

#include <boost/json/object.hpp>
#include <boost/json/serialize.hpp>

class HealthHandler : public HandlerBase {
protected:
    void handleGet(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view& url_view,
        const std::map<std::string, std::string>& path_params
    ) override {
        boost::json::object response;
        response["application"] = "@PROJECT_NAME@";
        response["status"] = "ok";
        response["path"] = std::string(url_view.path());

        buildJsonResponse(res, http::status::ok, boost::json::serialize(response));
    }
};
