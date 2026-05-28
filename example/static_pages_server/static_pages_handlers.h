/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

// static_pages_handlers.h
#pragma once
#include "handler_base.h"
#include <boost/beast/http.hpp>
#include <boost/url.hpp>
#include <map>

namespace http = boost::beast::http;
namespace urls = boost::urls;

class HomePageHandler : public HandlerBase {
protected:
    void handleGet(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view& url_view,
        const std::map<std::string, std::string>& path_params
    ) override {
        res.result(http::status::ok);
        res.set(http::field::content_type, "text/html; charset=utf-8");

        std::string html = R"(
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <title>Главная страница</title>
</head>
<body>
    <h1>Добро пожаловать на главную страницу!</h1>
    <p>Это пример статической главной страницы.</p>
    <a href="/about">О нас</a>
</body>
</html>
)";

        res.body() = html;
        res.prepare_payload();
    }
};

class AboutPageHandler : public HandlerBase {
protected:
    void handleGet(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view& url_view,
        const std::map<std::string, std::string>& path_params
    ) override {
        res.result(http::status::ok);
        res.set(http::field::content_type, "text/html; charset=utf-8");

        std::string html = R"(
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <title>О нас</title>
</head>
<body>
    <h1>О нашей компании</h1>
    <p>Это страница "О нас" - пример второй статической страницы.</p>
    <a href="/">Главная</a>
</body>
</html>
)";

        res.body() = html;
        res.prepare_payload();
    }
};
