/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#include "handler_base.h"
#include "di_container.h"
#include "http_server.h"
#include "extension_interface.h"
#include <iostream>

class ProductHandler : public HandlerBase {
private:
    std::shared_ptr<DIContainer> container_;

public:
    ProductHandler(std::shared_ptr<DIContainer> container)
        : container_(container) {}

protected:
    void handleGet(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view& url_view,
        const std::map<std::string, std::string>& path_params
    ) override {
        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"message": "GET request for product handled"})";
        res.prepare_payload();
    }

    void handlePost(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view& url_view,
        const std::map<std::string, std::string>& path_params
    ) override {
        res.result(http::status::created);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"message": "Product created", "data": ")" + req.body() + "\"}";
        res.prepare_payload();
    }

    void handlePut(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view& url_view,
        const std::map<std::string, std::string>& path_params
    ) override {
        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"message": "Product updated", "id": "123"})";
        res.prepare_payload();
    }

    void handleDelete(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view& url_view,
        const std::map<std::string, std::string>& path_params
    ) override {
        res.result(http::status::no_content);
        res.prepare_payload();
    }

    void handlePatch(
        const http::request<http::string_body>& req,
        http::response<http::string_body>& res,
        const urls::url_view& url_view,
        const std::map<std::string, std::string>& path_params
    ) override {
        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"message": "Product patched"})";
        res.prepare_payload();
    }
};

class ProductExtension : public ExtensionInterface {
private:
    std::shared_ptr<ProductHandler> handler_;
    std::shared_ptr<DIContainer> container_;

public:
    std::string getName() const override {
        return "ProductExtension";
    }

    void initialize(DIContainer& container) override {
        // Do not create a container copy; use the original
        container_ = std::shared_ptr<DIContainer>(&container, [](DIContainer*) {});
        handler_ = std::make_shared<ProductHandler>(container_);
        std::cout << "ProductExtension initialized" << std::endl;
    }

    void registerRoutes(HttpServer& server, DIContainer& container) override {
        // Register routes for product
        server.add_route("/products", handler_);
        server.add_route("/products/{id}", handler_);
        server.add_route("/api/v1/products", handler_);
        server.add_route("/api/v1/products/{id}", handler_);
        std::cout << "Product routes registered" << std::endl;
    }

    void cleanup() override {
        handler_.reset();
        container_.reset();
        std::cout << "ProductExtension cleaned up" << std::endl;
    }
};

// Exported functions for dynamic loading
extern "C" {
    ExtensionInterface* createExtension() {
        return new ProductExtension();
    }

    void destroyExtension(ExtensionInterface* extension) {
        delete extension;
    }
}
