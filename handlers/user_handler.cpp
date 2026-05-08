/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "user_handler.h"
#include <string>

void UserHandler::handleGet(
    const http::request<http::string_body>& req,
    http::response<http::string_body>& res,
    const urls::url_view& url_view,
    const std::map<std::string, std::string>& path_params
) {
    // Example of using the container to get services
    // if (di_container_ && di_container_->hasService("user_service")) {
    //     auto user_service = di_container_->resolve<UserService>("user_service");
    //     // Service usage
    // }

    // Get query parameters
    auto params = url_view.params();

    // Iterating through query parameters
    for (const auto& param : params) {
        std::string param_name = std::string(param.key);
        std::string param_value = std::string(param.value);
        // Use param_name and param_value as needed
    }

    // Access a specific header as string_view
    auto content_type = req[http::field::content_type];
    if (!content_type.empty()) {
        std::string ct = std::string(content_type);
        // Use the content-type header value
    }

    // Access custom header
    auto authorization = req["Authorization"];
    if (!authorization.empty()) {
        std::string auth_value = std::string(authorization);
    }

    // Iterate through all headers
    for (auto const& field : req) {
        std::string field_name = std::string(field.name_string());
        std::string field_value = std::string(field.value());
        // Process each header field
    }

    // Check if a header exists
    if (req.count(http::field::user_agent) > 0) {
        std::string user_agent = std::string(req[http::field::user_agent]);
    }

    // read path like "/users/1"
    std::string path = std::string(url_view.path());
    // You'll need to determine which pattern matched to properly extract parameters

    // Access the 'id' parameter
    auto id_it = path_params.find("id");
    if (id_it != path_params.end()) {
        std::string user_id = id_it->second;
        // Use the user_id in your logic
        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"message": "User data", "user_id": ")" + user_id + "\"}";
    } else {
        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"message": "Получены данные пользователей", "path": ")" +
                     path + "\"}";
    }

    res.prepare_payload();
}

void UserHandler::handlePost(
    const http::request<http::string_body>& req,
    http::response<http::string_body>& res,
    const urls::url_view& url_view,
    const std::map<std::string, std::string>& path_params
) {
    // Example of using the container to get services
    // if (di_container_ && di_container_->hasService("user_service")) {
    //     auto user_service = di_container_->resolve<UserService>("user_service");
    //     // Use the service to create a user
    // }

    // Example implementation of a POST request for creating a user
    res.result(http::status::created);
    res.set(http::field::content_type, "application/json");
    res.body() = R"({"message": "Пользователь создан", "data": ")" + req.body() + "\"}";
    res.prepare_payload();
}

void UserHandler::handlePut(
    const http::request<http::string_body>& req,
    http::response<http::string_body>& res,
    const urls::url_view& url_view,
    const std::map<std::string, std::string>& path_params
) {
    // Example of using the container to get services
    // if (di_container_ && di_container_->hasService("user_service")) {
    //     auto user_service = di_container_->resolve<UserService>("user_service");
    //     // Use the service to update a user
    // }

    // Example implementation of a PUT request for updating a user
    res.result(http::status::ok);
    res.set(http::field::content_type, "application/json");
    res.body() = R"({"message": "Пользователь обновлен", "id": "123"})";
    res.prepare_payload();
}

void UserHandler::handleDelete(
    const http::request<http::string_body>& req,
    http::response<http::string_body>& res,
    const urls::url_view& url_view,
    const std::map<std::string, std::string>& path_params
) {
    // Example of using the container to get services
    // if (di_container_ && di_container_->hasService("user_service")) {
    //     auto user_service = di_container_->resolve<UserService>("user_service");
    //     // Use the service to delete a user
    // }

    // Example implementation of a DELETE request for deleting a user
    res.result(http::status::no_content);
    res.prepare_payload();
}
