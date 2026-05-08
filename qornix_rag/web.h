/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
/**
 * HTTP request handlers for the RAG system
 * Integration with qornix_web
 */

#include "core.h"
#include "../include/handler_base.h"
#include "../include/template_loader.h"
#include "../include/http_server.h"
#include <boost/beast.hpp>
#include <boost/url.hpp>
#include <boost/json.hpp>
#include <cstdint>

namespace http = boost::beast::http;
namespace urls = boost::urls;

/**
 * Base handler for the RAG API
 */
class RagApiHandler : public HandlerBase {
protected:
    std::shared_ptr<RagEngine> rag_engine_;

public:
    explicit RagApiHandler(std::shared_ptr<RagEngine> engine)
        : rag_engine_(engine) {
    }

    /**
     * POST /api/index and /api/search - handle POST requests
     */
    void handlePost(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_view,
        const std::map<std::string, std::string> &path_params
    ) override {
        try {
            // Determine request type by URL
            std::string path = url_view.path();

            if (path == "/api/index" || path.find("/api/index") != std::string::npos) {
                // Project indexing
                boost::json::value json_req = boost::json::parse(req.body());
                std::string project_path = rag_engine_->get_indexed_project_root();
                if (project_path.empty()) {
                    project_path = ".";
                }

                if (json_req.if_object()) {
                    auto obj = json_req.as_object();
                    if (obj.contains("project_path")) {
                        std::string requested_path = obj.at("project_path").as_string().c_str();
                        if (!requested_path.empty() && requested_path != "." && requested_path != "./") {
                            project_path = requested_path;
                        }
                    }
                }

                // Index the project
                rag_engine_->index_project(project_path);

                // Get statistics
                auto stats = rag_engine_->get_statistics();

                // Build response
                boost::json::object response;
                response["success"] = true;
                response["message"] = "Project indexed successfully";
                response["stats"] = {
                    {"total_files", stats.total_files},
                    {"total_lines", stats.total_lines},
                    {"total_size_kb", stats.total_size_bytes / 1024},
                    {"index_duration_ms", static_cast<std::int64_t>(stats.index_duration_ms)}
                };

                buildJsonResponse(res, http::status::ok,
                                  boost::json::serialize(response));

            } else if (path == "/api/search" || path.find("/api/search") != std::string::npos) {
                // Project search
                boost::json::value json_req = boost::json::parse(req.body());
                std::string query;
                size_t top_k = 10;
                bool full_context = false;

                if (json_req.if_object()) {
                    auto obj = json_req.as_object();
                    if (obj.contains("query")) {
                        query = obj.at("query").as_string().c_str();
                    }
                    if (obj.contains("top_k")) {
                        top_k = static_cast<size_t>(obj.at("top_k").as_int64());
                    }
                    if (obj.contains("full_context")) {
                        full_context = obj.at("full_context").as_bool();
                    }
                }

                if (query.empty()) {
                    buildErrorResponse(res, http::status::bad_request, "Query is required");
                    return;
                }

                // Search
                auto results = rag_engine_->search(query, top_k);

                // Build response
                boost::json::array results_array;
                for (const auto &result: results) {
                    boost::json::object result_obj{
                        {"path", result.document.relative_path},
                        {"type", result.document.type},
                        {"language", result.document.language},
                        {"score", result.score},
                        {"vector_score", result.vector_score},
                        {"text_score", result.text_score},
                        {"fused_score", result.fused_score},
                        {"snippet", result.snippet},
                        {"lines", result.document.lines_count},
                        {"size", result.document.size_bytes}
                    };
                    results_array.emplace_back(result_obj);
                }

                boost::json::object response;
                response["success"] = true;
                response["query"] = query;
                response["results"] = results_array;
                response["count"] = results.size();

                buildJsonResponse(res, http::status::ok,
                                  boost::json::serialize(response));
            } else {
                buildErrorResponse(res, http::status::not_found, "Unknown endpoint");
            }
        } catch (const std::exception &e) {
            buildErrorResponse(res, http::status::internal_server_error, e.what());
        }
    }

    /**
     * GET /api/stats - project statistics
     */
    void handleGet(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_view,
        const std::map<std::string, std::string> &path_params
    ) override {
        try {
            auto stats = rag_engine_->get_statistics();

            boost::json::object response;
            response["success"] = true;
            response["indexed"] = rag_engine_->is_indexed();
            response["embedding_backend"] = rag_engine_->get_embedding_backend();
            response["embedding_dim"] = static_cast<std::int64_t>(rag_engine_->get_embedding_dim());
            response["onnx_ready"] = rag_engine_->is_onnx_ready();
            response["onnx_status"] = rag_engine_->get_onnx_status_message();
            response["project_root"] = rag_engine_->get_indexed_project_root();

            boost::json::object stats_obj;
            stats_obj["total_files"] = stats.total_files;
            stats_obj["total_lines"] = stats.total_lines;
            stats_obj["total_size_kb"] = stats.total_size_bytes / 1024;
            stats_obj["index_duration_ms"] = static_cast<std::int64_t>(stats.index_duration_ms);

            boost::json::object files_by_type;
            for (const auto &[type, count]: stats.files_by_type) {
                files_by_type[type] = count;
            }
            stats_obj["files_by_type"] = files_by_type;

            boost::json::object files_by_dir;
            for (const auto &[dir, count]: stats.files_by_directory) {
                files_by_dir[dir] = count;
            }
            stats_obj["files_by_directory"] = files_by_dir;

            response["stats"] = stats_obj;

            buildJsonResponse(res, http::status::ok,
                              boost::json::serialize(response));
        } catch (const std::exception &e) {
            buildErrorResponse(res, http::status::internal_server_error, e.what());
        }
    }
};

/**
 * Handler for the main page
 */
class RagWebHandler : public HandlerBase {
private:
    std::string templates_dir_;

public:
    explicit RagWebHandler(const std::string &templates_dir = "templates")
        : templates_dir_(templates_dir) {
    }

public:
    void handleGet(
        const http::request<http::string_body> &req,
        http::response<http::string_body> &res,
        const urls::url_view &url_view,
        const std::map<std::string, std::string> &path_params
    ) override {
        try {
            std::string html = TemplateLoader::loadFile("rag_interface.html", templates_dir_);
            buildHtmlResponse(res, http::status::ok, html);
        } catch (const std::exception &e) {
            std::string error_html = TemplateLoader::load500Template(templates_dir_);
            buildHtmlResponse(res, http::status::internal_server_error, error_html);
        }
    }
};

/**
 * Configure routes for the RAG system
 */
inline void setupRagRoutes(HttpServer& server, std::shared_ptr<RagEngine> rag_engine) {
    std::cout << "🔧 Добавление RAG маршрутов..." << std::endl;

    // Main page
    server.add_route("/", std::make_shared<RagWebHandler>("templates"));
    std::cout << "  ✓ GET  / - Web интерфейс" << std::endl;

    // API endpoints
    server.add_route("/api/index", std::make_shared<RagApiHandler>(rag_engine));
    std::cout << "  ✓ POST /api/index - Индексация проекта" << std::endl;

    server.add_route("/api/search", std::make_shared<RagApiHandler>(rag_engine));
    std::cout << "  ✓ POST /api/search - Поиск" << std::endl;

    server.add_route("/api/stats", std::make_shared<RagApiHandler>(rag_engine));
    std::cout << "  ✓ GET  /api/stats - Статистика" << std::endl;

    std::cout << "✅ Все маршруты добавлены" << std::endl;
}
