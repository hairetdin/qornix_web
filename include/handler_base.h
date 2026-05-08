/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#ifndef HANDLER_BASE_H
#define HANDLER_BASE_H

#include <map>
#include <boost/beast.hpp>
#include <boost/url.hpp>
#include <boost/json/object.hpp>
#include <boost/json/serialize.hpp>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace http = boost::beast::http;
namespace urls = boost::urls;

class HandlerBase {
public:
    virtual ~HandlerBase() = default;

    void handle(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const std::string &method,
        const urls::url_view &url_params,
        const std::map<std::string, std::string> &path_params
    ) {
        if (method == "GET") { handleGet(req, res, url_params, path_params); } else if (
            method == "POST") { handlePost(req, res, url_params, path_params); } else if (
            method == "PUT") { handlePut(req, res, url_params, path_params); } else if (
            method == "DELETE") { handleDelete(req, res, url_params, path_params); } else if (method == "PATCH") {
            handlePatch(req, res, url_params, path_params);
        } else if (method == "HEAD") { handleHead(req, res, url_params, path_params); } else if (method == "OPTIONS") {
            handleOptions(req, res, url_params, path_params);
        } else {
            res.result(http::status::method_not_allowed);
            res.set(http::field::content_type, "text/plain");
            res.body() = "Метод " + method + " не поддерживается";
            res.prepare_payload();
        }
    }

    // Universal method for building responses with an arbitrary content_type
    static void buildResponse(http::response<http::string_body> &res,
                              http::status status,
                              const std::string &body_content,
                              const std::string &content_type,
                              const std::string &message = "") {
        // If this is an error, use buildErrorResponse
        if (status == http::status::bad_request ||
            status == http::status::not_found ||
            status == http::status::internal_server_error) {
            buildErrorResponse(res, status, message.empty() ? "Error" : message);
            return;
            }

        res.result(status);
        res.set(http::field::content_type, content_type);
        res.body() = body_content;
        res.prepare_payload();
    }

    // Helper method for determining Content-Type by file extension
    static std::string getContentType(const std::string &filename) {
        size_t dotPos = filename.rfind('.');
        if (dotPos == std::string::npos) {
            return "application/octet-stream";
        }

        std::string extension = filename.substr(dotPos);

        if (extension == ".html" || extension == ".htm") return "text/html; charset=utf-8";
        if (extension == ".css") return "text/css; charset=utf-8";
        if (extension == ".js") return "application/javascript; charset=utf-8";
        if (extension == ".json") return "application/json; charset=utf-8";
        if (extension == ".txt") return "text/plain; charset=utf-8";
        if (extension == ".csv") return "text/csv; charset=utf-8";
        if (extension == ".xml") return "application/xml; charset=utf-8";
        if (extension == ".png") return "image/png";
        if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
        if (extension == ".gif") return "image/gif";
        if (extension == ".svg") return "image/svg+xml";
        if (extension == ".ico") return "image/x-icon";
        if (extension == ".pdf") return "application/pdf";

        return "application/octet-stream";
    }

protected:
    virtual void handleGet(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_view,
        const std::map<std::string, std::string> &path_params
    ) {
        res.result(http::status::not_implemented);
        res.set(http::field::content_type, "text/plain");
        res.body() = "GET не реализован";
        res.prepare_payload();
    }

    virtual void handlePost(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_view,
        const std::map<std::string, std::string> &path_params
    ) {
        res.result(http::status::not_implemented);
        res.set(http::field::content_type, "text/plain");
        res.body() = "POST не реализован";
        res.prepare_payload();
    }

    virtual void handlePut(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_view,
        const std::map<std::string, std::string> &path_params
    ) {
        res.result(http::status::not_implemented);
        res.set(http::field::content_type, "text/plain");
        res.body() = "PUT не реализован";
        res.prepare_payload();
    }

    virtual void handleDelete(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_view,
        const std::map<std::string, std::string> &path_params
    ) {
        res.result(http::status::not_implemented);
        res.set(http::field::content_type, "text/plain");
        res.body() = "DELETE не реализован";
        res.prepare_payload();
    }

    virtual void handlePatch(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_params,
        const std::map<std::string, std::string> &path_params
    ) {
        res.result(http::status::not_implemented);
        res.set(http::field::content_type, "text/plain");
        res.body() = "PATCH не реализован";
        res.prepare_payload();
    }

    virtual void handleHead(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_params,
        const std::map<std::string, std::string> &path_params
    ) {
        res.result(http::status::not_implemented);
        res.set(http::field::content_type, "text/plain");
        res.body() = "HEAD не реализован";
        res.prepare_payload();
    }

    virtual void handleOptions(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_params,
        const std::map<std::string, std::string> &path_params
    ) {
        res.result(http::status::not_implemented);
        res.set(http::field::content_type, "text/plain");
        res.body() = "OPTIONS не реализован";
        res.prepare_payload();
    }

    static void buildErrorResponse(http::response<http::string_body> &res,
                                   http::status status,
                                   const std::string &message) {
        boost::json::object error_response;
        error_response["error"] = message;
        error_response["status"] = std::to_string(static_cast<int>(status));

        res.result(status);
        res.set(http::field::content_type, "application/json; charset=utf-8");
        res.body() = boost::json::serialize(error_response);
        res.prepare_payload();
    }

    // Helper methods for building responses
    static void buildJsonResponse(http::response<http::string_body> &res,
                           http::status status,
                           const std::string &json_data,
                           const std::string &message = "") {
        buildResponse(res, status, json_data, "application/json; charset=utf-8", message);
    }

    static void buildHtmlResponse(http::response<http::string_body> &res,
                           http::status status,
                           const std::string &html_content,
                           const std::string &message = "") {
        buildResponse(res, status, html_content, "text/html; charset=utf-8", message);
    }

    static void buildTextResponse(http::response<http::string_body> &res,
                           http::status status,
                           const std::string &text,
                           const std::string &message = "") {
        buildResponse(res, status, text, "text/plain; charset=utf-8", message);
    }
};

// Universal static file handler - separate class
class StaticFileHandler : public HandlerBase {
private:
    std::string baseDirectory_;

public:
    explicit StaticFileHandler(const std::string &baseDir) : baseDirectory_(baseDir) {
    }

    // Override only handleGet to handle GET requests
    void handleGet(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url,
        const std::map<std::string, std::string> &params
    ) override {
        // Get the file name from parameters or from the URL
        std::string filename;

        if (params.count("filename")) {
            filename = params.at("filename");
        } else {
            // If there are no parameters, take the last path segment
            std::string path = std::string(url.path());
            size_t lastSlash = path.rfind('/');
            if (lastSlash != std::string::npos && lastSlash < path.length() - 1) {
                filename = path.substr(lastSlash + 1);
            }
        }

        // Remove leading slashes and dots for security
        while (!filename.empty() && (filename[0] == '/' || filename[0] == '.')) {
            filename = filename.substr(1);
        }

        // Protection against path traversal attacks
        if (filename.find("..") != std::string::npos || filename[0] == '/') {
            buildResponse(res, http::status::bad_request, "Invalid filename", "text/plain; charset=utf-8", "Security error");
            return;
        }

        serveStaticFile(res, filename, baseDirectory_);
    }

    // Method for creating a shared_ptr handler
    static std::shared_ptr<StaticFileHandler> create(const std::string &baseDir) {
        return std::make_shared<StaticFileHandler>(baseDir);
    }

    // Helper method for loading and sending static files
    static void serveStaticFile(http::response<http::string_body> &res,
                                const std::string &filename,
                                const std::string &directory) {
        std::string fullPath = directory + "/" + filename;

        // Check whether the file exists
        if (!std::filesystem::exists(fullPath)) {
            buildResponse(res, http::status::not_found,
                         "File not found: " + filename,
                         "text/plain; charset=utf-8",
                         "File not found");
            return;
        }

        try {
            std::ifstream file(fullPath, std::ios::binary);
            if (!file.is_open()) {
                buildResponse(res, http::status::forbidden,
                             "Access denied: " + filename,
                             "text/plain; charset=utf-8",
                             "Access denied");
                return;
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();

            // Determine Content-Type by extension
            std::string contentType = getContentType(filename);

            buildResponse(res, http::status::ok, content, contentType);
        } catch (const std::exception &e) {
            buildResponse(res, http::status::internal_server_error,
                         "Error loading file: " + std::string(e.what()),
                         "text/plain; charset=utf-8",
                         "Internal server error");
        }
    }
};

#endif // HANDLER_BASE_H
