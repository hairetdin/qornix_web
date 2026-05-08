/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include <boost/beast/http/status.hpp>
#include <boost/json.hpp>
#include <string>

namespace qornix_dynamic_api {

struct DynamicApiResponse {
    boost::beast::http::status status = boost::beast::http::status::ok;
    std::string message;
    boost::json::value body = boost::json::object{};

    static DynamicApiResponse ok(boost::json::value value, std::string message = "OK") {
        DynamicApiResponse response;
        response.status = boost::beast::http::status::ok;
        response.message = std::move(message);
        response.body = std::move(value);
        return response;
    }

    static DynamicApiResponse error(
        boost::beast::http::status status,
        const std::string& code,
        const std::string& message
    ) {
        boost::json::object payload;
        payload["ok"] = false;
        payload["error"] = code;
        payload["message"] = message;
        DynamicApiResponse response;
        response.status = status;
        response.message = message;
        response.body = payload;
        return response;
    }

    std::string toJsonString() const {
        return boost::json::serialize(body);
    }
};

inline boost::json::object okEnvelope(const std::string& message = "OK") {
    boost::json::object payload;
    payload["ok"] = true;
    payload["message"] = message;
    return payload;
}

} // namespace qornix_dynamic_api
