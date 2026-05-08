#!/bin/bash
# create_route.sh

# Check whether an argument was passed
if [ $# -eq 0 ]; then
    echo "Usage: $0 <route_name>"
    echo "Example: $0 product"
    exit 1
fi

ROUTE_NAME=$1
ROUTE_DIR="$ROUTE_NAME"

# Check whether such a directory already exists
if [ -d "$ROUTE_DIR" ]; then
    echo "Error: Directory '$ROUTE_DIR' already exists"
    exit 1
fi

# Create a directory for the new route
mkdir -p "$ROUTE_DIR"
echo "Created directory: $ROUTE_DIR"

# Create handler.cpp with a handler template
cat > "$ROUTE_DIR/handler.cpp" << EOF
#include "handler_base.h"
#include "di_container.h"
#include "http_server.h"
#include "extension_interface.h"
#include <iostream>

class ${ROUTE_NAME^}Handler : public HandlerBase {
private:
    std::shared_ptr<DIContainer> container_;

public:
    ${ROUTE_NAME^}Handler(std::shared_ptr<DIContainer> container)
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
        res.body() = R"({"message": "GET request for ${ROUTE_NAME} handled"})";
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
        res.body() = R"({"message": "${ROUTE_NAME^} created", "data": ")" + req.body() + "\"}";
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
        res.body() = R"({"message": "${ROUTE_NAME^} updated", "id": "123"})";
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
        res.body() = R"({"message": "${ROUTE_NAME^} patched"})";
        res.prepare_payload();
    }
};

class ${ROUTE_NAME^}Extension : public ExtensionInterface {
private:
    std::shared_ptr<${ROUTE_NAME^}Handler> handler_;
    std::shared_ptr<DIContainer> container_;

public:
    std::string getName() const override {
        return "${ROUTE_NAME^}Extension";
    }

    void initialize(DIContainer& container) override {
        // Do not create a container copy; use the original
        container_ = std::shared_ptr<DIContainer>(&container, [](DIContainer*) {});
        handler_ = std::make_shared<${ROUTE_NAME^}Handler>(container_);
        std::cout << "${ROUTE_NAME^}Extension initialized" << std::endl;
    }

    void registerRoutes(HttpServer& server, DIContainer& container) override {
        // Register routes for ${ROUTE_NAME}
        server.add_route("/${ROUTE_NAME}s", handler_);
        server.add_route("/${ROUTE_NAME}s/{id}", handler_);
        server.add_route("/api/v1/${ROUTE_NAME}s", handler_);
        server.add_route("/api/v1/${ROUTE_NAME}s/{id}", handler_);
        std::cout << "${ROUTE_NAME^} routes registered" << std::endl;
    }

    void cleanup() override {
        handler_.reset();
        container_.reset();
        std::cout << "${ROUTE_NAME^}Extension cleaned up" << std::endl;
    }
};

// Exported functions for dynamic loading
extern "C" {
    ExtensionInterface* createExtension() {
        return new ${ROUTE_NAME^}Extension();
    }

    void destroyExtension(ExtensionInterface* extension) {
        delete extension;
    }
}
EOF

echo "Created handler.cpp for route: $ROUTE_NAME"
echo "To build extensions, run: ./build_route_extensions.sh"
