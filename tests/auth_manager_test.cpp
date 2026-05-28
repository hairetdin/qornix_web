/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "auth_manager.h"

#include <cassert>
#include <iostream>

int main() {
    qornix_auth::AuthConfig config;
    config.mode = qornix_auth::AuthMode::SESSION;
    config.minPasswordLength = 12;
    config.defaultRoles = {"user"};
    config.defaultPermissions = {"rag:read"};

    qornix_auth::AuthManager auth(config);

    auto weak = auth.registerUser("weak", "short", "weak@example.com");
    assert(weak.status != qornix_auth::AuthStatus::SUCCESS);

    auto registered = auth.registerUser(
        "admin",
        "correct-horse-password",
        "admin@example.com",
        {"admin"},
        {"rag:read", "rag:write", "rag:admin"});
    assert(registered.status == qornix_auth::AuthStatus::SUCCESS);
    assert(!registered.userId.empty());
    assert(registered.roles.size() == 1 && registered.roles[0] == "admin");

    const auto* stored = auth.getUserByUsername("admin");
    assert(stored != nullptr);
    assert(stored->password.rfind("pbkdf2-sha256$", 0) == 0);
    assert(stored->salt.size() >= 32);

    auto duplicate = auth.registerUser("admin", "correct-horse-password", "admin2@example.com");
    assert(duplicate.status == qornix_auth::AuthStatus::USER_ALREADY_EXISTS);

    auto badLogin = auth.authenticate("admin", "wrong-password");
    assert(badLogin.status == qornix_auth::AuthStatus::INVALID_CREDENTIALS);

    auto login = auth.authenticate("admin", "correct-horse-password");
    assert(login.status == qornix_auth::AuthStatus::SUCCESS);
    assert(!login.sessionId.empty());
    assert(login.sessionId.rfind("sess_", 0) == 0);
    assert(login.sessionId.size() > 40);
    assert(login.username == "admin");
    assert(login.token.empty());

    auto secondLogin = auth.authenticate("admin", "correct-horse-password");
    assert(secondLogin.status == qornix_auth::AuthStatus::SUCCESS);
    assert(secondLogin.sessionId != login.sessionId);
    assert(!auth.authenticateSession(login.sessionId).has_value());

    auto context = auth.authenticateSession(secondLogin.sessionId);
    assert(context.has_value());
    assert(context->authenticated);
    assert(context->hasRole("admin"));
    assert(context->hasPermission("rag:admin"));
    assert(context->hasPermission("rag:write"));

    auto sessionUser = auth.store()->findUserById(login.userId);
    assert(sessionUser.has_value());
    sessionUser->roles = {"user"};
    sessionUser->permissions = {"rag:read"};
    assert(auth.store()->updateUser(*sessionUser));

    auto refreshedContext = auth.authenticateSession(secondLogin.sessionId);
    assert(refreshedContext.has_value());
    assert(!refreshedContext->hasRole("admin"));
    assert(!refreshedContext->hasPermission("rag:admin"));
    assert(refreshedContext->hasPermission("rag:read"));

    sessionUser->isActive = false;
    assert(auth.store()->updateUser(*sessionUser));
    auto disabledContext = auth.authenticateSession(secondLogin.sessionId);
    assert(!disabledContext.has_value());

    sessionUser->isActive = true;
    sessionUser->roles = {"admin"};
    sessionUser->permissions = {"rag:read", "rag:write", "rag:admin"};
    assert(auth.store()->updateUser(*sessionUser));

    assert(auth.userHasRole(login.userId, "admin"));
    assert(auth.userHasPermission(login.userId, "rag:read"));
    assert(auth.grantPermission(login.userId, "rag:metrics"));
    assert(auth.userHasPermission(login.userId, "rag:metrics"));

    assert(auth.changePassword(login.userId, "correct-horse-password", "new-correct-horse-password"));
    auto oldPasswordLogin = auth.authenticate("admin", "correct-horse-password");
    assert(oldPasswordLogin.status == qornix_auth::AuthStatus::INVALID_CREDENTIALS);
    auto newPasswordLogin = auth.authenticate("admin", "new-correct-horse-password");
    assert(newPasswordLogin.status == qornix_auth::AuthStatus::SUCCESS);

    const std::string invite = auth.createInviteToken(
        "invited",
        "invited@example.com",
        {"user"},
        {"rag:read"});
    assert(invite.rfind("invite_", 0) == 0);
    auto accepted = auth.acceptInviteToken(invite, "invited-strong-password");
    assert(accepted.status == qornix_auth::AuthStatus::SUCCESS);
    assert(auth.acceptInviteToken(invite, "another-strong-password").status != qornix_auth::AuthStatus::SUCCESS);

    const std::string reset = auth.createPasswordResetToken("invited");
    assert(reset.rfind("reset_", 0) == 0);
    assert(auth.resetPasswordWithToken(reset, "invited-new-strong-password"));
    assert(!auth.resetPasswordWithToken(reset, "invited-newer-strong-password"));
    assert(auth.authenticate("invited", "invited-new-strong-password").status == qornix_auth::AuthStatus::SUCCESS);

    auto auditEvents = auth.auditEvents();
    assert(!auditEvents.empty());

    qornix_auth::AuthConfig bothConfig = config;
    bothConfig.mode = qornix_auth::AuthMode::BOTH;
    qornix_auth::AuthManager jwtAuth(bothConfig);
    auto jwtRegistered = jwtAuth.registerUser(
        "jwt-admin",
        "correct-jwt-password",
        "jwt-admin@example.com",
        {"admin"},
        {"rag:read", "rag:admin"});
    assert(jwtRegistered.status == qornix_auth::AuthStatus::SUCCESS);
    auto jwtLogin = jwtAuth.authenticate("jwt-admin", "correct-jwt-password");
    assert(jwtLogin.status == qornix_auth::AuthStatus::SUCCESS);
    if (!jwtLogin.token.empty()) {
        auto jwtUser = jwtAuth.store()->findUserById(jwtLogin.userId);
        assert(jwtUser.has_value());
        jwtUser->roles = {"user"};
        jwtUser->permissions = {"rag:read"};
        assert(jwtAuth.store()->updateUser(*jwtUser));

        auto jwtContext = jwtAuth.authenticateBearerToken(jwtLogin.token);
        assert(jwtContext.has_value());
        assert(!jwtContext->hasRole("admin"));
        assert(!jwtContext->hasPermission("rag:admin"));
        assert(jwtContext->hasPermission("rag:read"));

        jwtUser->isActive = false;
        assert(jwtAuth.store()->updateUser(*jwtUser));
        auto disabledJwtContext = jwtAuth.authenticateBearerToken(jwtLogin.token);
        assert(!disabledJwtContext.has_value());
    }

    std::cout << "auth_manager_test passed\n";
    return 0;
}
