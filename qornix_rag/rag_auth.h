#pragma once

#include <string>

struct RagRouteAuthOptions {
    bool enabled = false;
    bool protect_admin_routes = true;
    bool protect_write_routes = true;
    std::string mode = "admin_token"; // admin_token | host_header
    std::string admin_token;
    std::string admin_token_env = "QORNIX_RAG_ADMIN_TOKEN";
    std::string token_header = "X-Qornix-RAG-Admin-Token";
    std::string role_header = "X-Qornix-Role";
    std::string admin_role = "admin";
};
