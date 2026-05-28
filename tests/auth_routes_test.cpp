/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "auth_routes.h"

#include <boost/url/parse.hpp>

#include <cassert>
#include <iostream>
#include <string>

namespace {

Response call(AuthApiHandler& handler,
              http::verb method,
              const std::string& target,
              const std::string& body = {},
              const std::string& cookie = {},
              Params params = {}) {
    Request req{method, target, 11};
    req.set(http::field::host, "localhost");
    if (!cookie.empty()) {
        req.set(http::field::cookie, cookie);
    }
    req.body() = body;
    req.prepare_payload();

    Response res;
    auto parsed = boost::urls::parse_origin_form(target);
    assert(parsed.has_value());
    handler.handle(req, res, std::string(req.method_string()), *parsed, params);
    return res;
}

} // namespace

std::string jsonStringField(const std::string& body, const std::string& key) {
    const std::string marker = "\"" + key + "\":\"";
    const auto start = body.find(marker);
    if (start == std::string::npos) {
        return "";
    }
    const auto valueStart = start + marker.size();
    const auto end = body.find('"', valueStart);
    return end == std::string::npos ? "" : body.substr(valueStart, end - valueStart);
}

int main() {
    qornix_auth::AuthConfig config;
    config.mode = qornix_auth::AuthMode::SESSION;
    config.minPasswordLength = 12;
    auto manager = std::make_shared<qornix_auth::AuthManager>(config);

    auto handler = makeAuthApiHandler(manager, true, true, "Strict", "/");

    auto registerRes = call(*handler,
                            http::verb::post,
                            "/auth/register",
                            R"({"username":"admin","password":"correct-horse-password","email":"admin@example.com"})");
    assert(registerRes.result() == http::status::created);

    auto badLogin = call(*handler,
                         http::verb::post,
                         "/auth/login",
                         R"({"username":"admin","password":"wrong-password"})");
    assert(badLogin.result() == http::status::unauthorized);

    auto login = call(*handler,
                      http::verb::post,
                      "/auth/login",
                      R"({"username":"admin","password":"correct-horse-password"})");
    assert(login.result() == http::status::ok);
    assert(login.find(http::field::set_cookie) != login.end());
    const std::string csrfToken = jsonStringField(login.body(), "csrf_token");
    assert(csrfToken.find("csrf_") == 0);
    const std::string cookie(login[http::field::set_cookie]);
    assert(cookie.find("session_id=sess_") != std::string::npos);
    assert(cookie.find("HttpOnly") != std::string::npos);
    assert(cookie.find("SameSite=Strict") != std::string::npos);
    assert(cookie.find("Secure") != std::string::npos);

    auto me = call(*handler, http::verb::get, "/auth/me", {}, cookie);
    assert(me.result() == http::status::ok);
    assert(me.body().find("\"username\":\"admin\"") != std::string::npos);
    assert(jsonStringField(me.body(), "csrf_token") == csrfToken);

    auto misleadingCookieMe = call(*handler,
                                   http::verb::get,
                                   "/auth/me",
                                   {},
                                   "other_" + cookie);
    assert(misleadingCookieMe.result() == http::status::unauthorized);

    auto logout = call(*handler, http::verb::post, "/auth/logout", {}, cookie);
    assert(logout.result() == http::status::ok);
    assert(std::string(logout[http::field::set_cookie]).find("Max-Age=0") != std::string::npos);

    auto loggedOutMe = call(*handler, http::verb::get, "/auth/me", {}, cookie);
    assert(loggedOutMe.result() == http::status::unauthorized);

    auto adminUser = manager->authenticate("admin", "correct-horse-password");
    assert(adminUser.status == qornix_auth::AuthStatus::SUCCESS);
    const std::string adminCookie = "session_id=" + adminUser.sessionId;

    auto invite = call(*handler,
                       http::verb::post,
                       "/auth/invites",
                       R"({"username":"invited","email":"invited@example.com","roles":["user"],"permissions":["rag:read"],"ttl_minutes":60})",
                       adminCookie);
    assert(invite.result() == http::status::created);
    const std::string inviteToken = jsonStringField(invite.body(), "invite_token");
    assert(inviteToken.rfind("invite_", 0) == 0);

    auto acceptInvite = call(*handler,
                             http::verb::post,
                             "/auth/invites/accept",
                             "{\"token\":\"" + inviteToken + "\",\"password\":\"correct-invited-password\"}");
    assert(acceptInvite.result() == http::status::created);
    assert(manager->getUserByUsername("invited") != nullptr);

    auto resetRequest = call(*handler,
                             http::verb::post,
                             "/auth/password-reset/request",
                             R"({"username":"invited"})");
    assert(resetRequest.result() == http::status::ok);
    const std::string resetToken = jsonStringField(resetRequest.body(), "reset_token");
    assert(resetToken.rfind("reset_", 0) == 0);
    auto resetConfirm = call(*handler,
                             http::verb::post,
                             "/auth/password-reset/confirm",
                             "{\"token\":\"" + resetToken + "\",\"password\":\"correct-invited-new-password\"}");
    assert(resetConfirm.result() == http::status::ok);
    assert(manager->authenticate("invited", "correct-invited-new-password").status == qornix_auth::AuthStatus::SUCCESS);

    auto audit = call(*handler, http::verb::get, "/auth/audit", {}, adminCookie);
    assert(audit.result() == http::status::ok);
    assert(audit.body().find("\"type\":\"login\"") != std::string::npos);

    auto createUser = call(*handler,
                           http::verb::post,
                           "/auth/users",
                           R"({"username":"writer","password":"correct-writer-password","email":"writer@example.com","roles":["user"],"permissions":["rag:read","rag:write"]})",
                           adminCookie);
    assert(createUser.result() == http::status::created);
    auto* writer = manager->getUserByUsername("writer");
    assert(writer != nullptr);
    assert(writer->hasPermission("rag:write"));

    auto listUsers = call(*handler, http::verb::get, "/auth/users", {}, adminCookie);
    assert(listUsers.result() == http::status::ok);
    assert(listUsers.body().find("\"username\":\"writer\"") != std::string::npos);

    auto updateUser = call(*handler,
                           http::verb::patch,
                           "/auth/users/" + writer->id,
                           R"({"roles":["admin"],"permissions":["rag:read","rag:admin","auth:admin"],"active":false})",
                           adminCookie,
                           {{"id", writer->id}});
    assert(updateUser.result() == http::status::ok);
    auto* updatedWriter = manager->getUserByUsername("writer");
    assert(updatedWriter != nullptr);
    assert(updatedWriter->hasRole("admin"));
    assert(updatedWriter->hasPermission("auth:admin"));
    assert(!updatedWriter->isActive);

    std::cout << "auth_routes_test passed\n";
    return 0;
}
