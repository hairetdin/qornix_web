/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>

#include <map>
#include <string>
#include <utility>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;

using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;
using Params = std::map<std::string, std::string>;

inline Response make_response(http::status status,
                              unsigned version,
                              std::string body,
                              const std::string& content_type) {
    Response res{status, version};
    if (!content_type.empty()) {
        res.set(http::field::content_type, content_type);
    }
    res.body() = std::move(body);
    res.prepare_payload();
    return res;
}

inline Response make_text_response(http::status status,
                                   unsigned version,
                                   std::string body) {
    return make_response(status, version, std::move(body), "text/plain; charset=utf-8");
}

inline Response make_text_response(std::string body, unsigned version = 11) {
    return make_text_response(http::status::ok, version, std::move(body));
}

inline Response make_json_response(http::status status,
                                   unsigned version,
                                   std::string json) {
    return make_response(status, version, std::move(json), "application/json; charset=utf-8");
}

inline Response make_html_response(http::status status,
                                   unsigned version,
                                   std::string html) {
    return make_response(status, version, std::move(html), "text/html; charset=utf-8");
}

inline Response make_error_response(http::status status,
                                    unsigned version,
                                    std::string message) {
    return make_text_response(status, version, std::move(message));
}

namespace response {

inline Response text(std::string body, unsigned version = 11) {
    return make_text_response(std::move(body), version);
}

inline Response text(http::status status, std::string body, unsigned version = 11) {
    return make_text_response(status, version, std::move(body));
}

inline Response json(std::string json_body, unsigned version = 11) {
    return make_json_response(http::status::ok, version, std::move(json_body));
}

inline Response json(http::status status, std::string json_body, unsigned version = 11) {
    return make_json_response(status, version, std::move(json_body));
}

inline Response status(http::status status_code, unsigned version = 11, std::string body = {}) {
    return make_response(status_code, version, std::move(body), "text/plain; charset=utf-8");
}

inline Response redirect(std::string location,
                         unsigned version = 11,
                         http::status status_code = http::status::found) {
    Response res{status_code, version};
    res.set(http::field::location, std::move(location));
    res.prepare_payload();
    return res;
}

} // namespace response
