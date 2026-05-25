#include "auth_manager.h"
#include "auth_orm_store.h"
#include "database_interface.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

std::string envValue(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

} // namespace

int main() {
    const std::string driver = envValue("QORNIX_AUTH_STORE_TEST_DRIVER");
    const std::string dsn = envValue("QORNIX_AUTH_STORE_TEST_DSN");
    if (driver.empty() || dsn.empty()) {
        std::cout << "SKIPPED: set QORNIX_AUTH_STORE_TEST_DRIVER and QORNIX_AUTH_STORE_TEST_DSN\n";
        return 0;
    }

    DatabaseConfig dbConfig;
    dbConfig.driver = driver;
    dbConfig.connectionString = dsn;

    auto db = DatabaseInterface::init(dbConfig);
    auto sharedDb = std::shared_ptr<DatabaseInterface>(std::move(db));
    auto store = qornix_auth::QornixOrmAuthStore::create(sharedDb);

    qornix_auth::AuthConfig authConfig;
    authConfig.mode = qornix_auth::AuthMode::SESSION;
    authConfig.minPasswordLength = 12;
    qornix_auth::AuthManager auth(authConfig, store);

    const std::string suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string username = "auth_dsn_" + suffix;
    const std::string password = "correct-horse-password-" + suffix;

    auto registered = auth.registerUser(
        username,
        password,
        username + "@example.com",
        {"admin"},
        {"rag:read", "rag:write", "rag:admin"});
    if (registered.status != qornix_auth::AuthStatus::SUCCESS) {
        std::cerr << "register failed: " << registered.message << "\n";
        return 1;
    }

    auto login = auth.authenticate(username, password);
    if (login.status != qornix_auth::AuthStatus::SUCCESS) {
        std::cerr << "login failed: " << login.message << "\n";
        return 1;
    }

    if (std::find(login.permissions.begin(), login.permissions.end(), "rag:admin") == login.permissions.end()) {
        std::cerr << "missing rag:admin permission\n";
        return 1;
    }

    if (!auth.grantPermission(login.userId, "rag:metrics")) {
        std::cerr << "grantPermission failed\n";
        return 1;
    }

    auto persisted = store->findUserByUsername(username);
    if (!persisted.has_value() || !persisted->hasPermission("rag:metrics")) {
        std::cerr << "persisted permission was not found\n";
        return 1;
    }

    std::cout << "auth_orm_store_dsn_test passed for driver " << driver << "\n";
    return 0;
}
