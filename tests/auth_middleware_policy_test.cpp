#include "auth_middleware.h"

#include <boost/url/parse.hpp>

#include <cassert>
#include <iostream>

namespace {

Response call(AuthMiddleware& middleware,
              http::verb method,
              const std::string& target,
              const std::string& cookie = {},
              const std::string& csrfToken = {}) {
    Request req{method, target, 11};
    req.set(http::field::host, "localhost");
    if (!cookie.empty()) {
        req.set(http::field::cookie, cookie);
    }
    if (!csrfToken.empty()) {
        req.set("X-CSRF-Token", csrfToken);
    }
    req.prepare_payload();

    Response res;
    auto parsed = boost::urls::parse_origin_form(target);
    assert(parsed.has_value());

    bool nextCalled = false;
    middleware.handle(req, res, *parsed, {}, [&]() {
        nextCalled = true;
        res = make_text_response(http::status::ok, req.version(), "next");
    });

    if (res.result() == http::status::ok) {
        assert(nextCalled);
    }
    return res;
}

std::string sessionCookie(const qornix_auth::AuthResult& result) {
    return "session_id=" + result.sessionId;
}

} // namespace

int main() {
    qornix_auth::AuthConfig config;
    config.mode = qornix_auth::AuthMode::SESSION;
    config.minPasswordLength = 12;
    auto manager = std::make_shared<qornix_auth::AuthManager>(config);

    auto readerReg = manager->registerUser(
        "reader",
        "correct-reader-password",
        "reader@example.com",
        {"user"},
        {"rag:read"});
    assert(readerReg.status == qornix_auth::AuthStatus::SUCCESS);

    auto writerReg = manager->registerUser(
        "writer",
        "correct-writer-password",
        "writer@example.com",
        {"user"},
        {"rag:read", "rag:write"});
    assert(writerReg.status == qornix_auth::AuthStatus::SUCCESS);

    auto adminReg = manager->registerUser(
        "admin",
        "correct-admin-password",
        "admin@example.com",
        {"admin"},
        {"rag:read", "rag:write", "rag:admin"});
    assert(adminReg.status == qornix_auth::AuthStatus::SUCCESS);

    const auto reader = manager->authenticate("reader", "correct-reader-password");
    const auto writer = manager->authenticate("writer", "correct-writer-password");
    const auto admin = manager->authenticate("admin", "correct-admin-password");
    assert(reader.status == qornix_auth::AuthStatus::SUCCESS);
    assert(writer.status == qornix_auth::AuthStatus::SUCCESS);
    assert(admin.status == qornix_auth::AuthStatus::SUCCESS);

    auto middleware = create_auth_middleware(manager, true, {"/auth/login"});
    middleware->addRoutePolicy("GET", "/rag", {"rag:read"}, {}, true);
    middleware->addRoutePolicy("POST", "/api/rag/index", {"rag:write"}, {}, true);
    middleware->addRoutePolicy("GET", "/api/rag/admin", {"rag:admin"});

    auto noCookie = call(*middleware, http::verb::get, "/rag");
    assert(noCookie.result() == http::status::unauthorized);

    auto misleadingCookie = call(*middleware,
                                 http::verb::get,
                                 "/rag",
                                 "other_session_id=" + reader.sessionId);
    assert(misleadingCookie.result() == http::status::unauthorized);

    auto readerRag = call(*middleware, http::verb::get, "/rag", sessionCookie(reader));
    assert(readerRag.result() == http::status::ok);

    auto readerWrite = call(*middleware, http::verb::post, "/api/rag/index", sessionCookie(reader));
    assert(readerWrite.result() == http::status::forbidden);

    auto writerWrite = call(*middleware, http::verb::post, "/api/rag/index", sessionCookie(writer));
    assert(writerWrite.result() == http::status::ok);

    middleware->setCsrfProtectionEnabled(true);
    const std::string writerCsrf = manager->csrfTokenForSession(writer.sessionId);
    auto writerWriteMissingCsrf = call(*middleware, http::verb::post, "/api/rag/index", sessionCookie(writer));
    assert(writerWriteMissingCsrf.result() == http::status::forbidden);
    assert(writerWriteMissingCsrf.body().find("CSRF token required") != std::string::npos);

    auto writerWriteBadCsrf = call(*middleware, http::verb::post, "/api/rag/index", sessionCookie(writer), "bad-token");
    assert(writerWriteBadCsrf.result() == http::status::forbidden);

    auto writerWriteWithCsrf = call(*middleware, http::verb::post, "/api/rag/index", sessionCookie(writer), writerCsrf);
    assert(writerWriteWithCsrf.result() == http::status::ok);

    middleware->setCsrfProtectionEnabled(false);

    auto writerAdmin = call(*middleware, http::verb::get, "/api/rag/admin/diagnostics", sessionCookie(writer));
    assert(writerAdmin.result() == http::status::forbidden);

    auto writerAdministrator = call(*middleware, http::verb::get, "/api/rag/administrator", sessionCookie(writer));
    assert(writerAdministrator.result() == http::status::ok);

    auto adminDiagnostics = call(*middleware, http::verb::get, "/api/rag/admin/diagnostics", sessionCookie(admin));
    assert(adminDiagnostics.result() == http::status::ok);

    auto excluded = call(*middleware, http::verb::post, "/auth/login");
    assert(excluded.result() == http::status::ok);

    auto excludedBoundary = call(*middleware, http::verb::post, "/auth/login-extra");
    assert(excludedBoundary.result() == http::status::unauthorized);

    std::cout << "auth_middleware_policy_test passed\n";
    return 0;
}
