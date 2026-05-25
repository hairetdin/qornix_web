#include "auth_manager.h"
#include "auth_orm_store.h"
#include "database_interface.h"

#include <cassert>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>

namespace {

class TempSqliteDb {
public:
    explicit TempSqliteDb(const std::string& prefix) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path root =
            std::filesystem::temp_directory_path() / "qornix_auth_tests";
        std::filesystem::create_directories(root);
        dbPath_ = root / (prefix + "_" + std::to_string(now) + ".sqlite");
    }

    ~TempSqliteDb() {
        cleanup();
    }

    TempSqliteDb(const TempSqliteDb&) = delete;
    TempSqliteDb& operator=(const TempSqliteDb&) = delete;

    DatabaseConfig config() const {
        DatabaseConfig cfg;
        cfg.driver = "sqlite";
        cfg.dbname = dbPath_.string();
        cfg.connectionString = dbPath_.string();
        return cfg;
    }

    void cleanup() {
        if (cleaned_) {
            return;
        }
        cleaned_ = true;
        std::error_code ec;
        std::filesystem::remove(dbPath_, ec);
        std::filesystem::remove(std::filesystem::path(dbPath_.string() + "-wal"), ec);
        std::filesystem::remove(std::filesystem::path(dbPath_.string() + "-shm"), ec);
    }

private:
    std::filesystem::path dbPath_;
    bool cleaned_{false};
};

} // namespace

int main() {
    TempSqliteDb temp("auth_orm_store_test");
    auto db = DatabaseInterface::init(temp.config());
    auto sharedDb = std::shared_ptr<DatabaseInterface>(std::move(db));
    auto store = qornix_auth::QornixOrmAuthStore::create(sharedDb);

    qornix_auth::AuthConfig authConfig;
    authConfig.mode = qornix_auth::AuthMode::SESSION;
    authConfig.minPasswordLength = 12;
    qornix_auth::AuthManager auth(authConfig, store);

    auto registered = auth.registerUser(
        "admin",
        "correct-horse-password",
        "admin@example.com",
        {"admin"},
        {"rag:read", "rag:write", "rag:admin"});
    assert(registered.status == qornix_auth::AuthStatus::SUCCESS);

    auto fetched = store->findUserByUsername("admin");
    assert(fetched.has_value());
    assert(fetched->username == "admin");
    assert(fetched->hasRole("admin"));
    assert(fetched->hasPermission("rag:admin"));
    assert(fetched->password.rfind("pbkdf2-sha256$", 0) == 0);

    auto login = auth.authenticate("admin", "correct-horse-password");
    assert(login.status == qornix_auth::AuthStatus::SUCCESS);
    assert(std::find(login.permissions.begin(), login.permissions.end(), "rag:admin") != login.permissions.end());
    assert(!login.sessionId.empty());

    assert(auth.grantPermission(login.userId, "rag:metrics"));
    auto afterGrant = store->findUserById(login.userId);
    assert(afterGrant.has_value());
    assert(afterGrant->hasPermission("rag:metrics"));

    assert(auth.assignRole(login.userId, "operator"));
    auto afterRole = store->findUserByUsername("admin");
    assert(afterRole.has_value());
    assert(afterRole->hasRole("operator"));

    auto users = store->listUsers();
    assert(users.size() == 1);
    assert(users.front().username == "admin");

    auto duplicate = auth.registerUser("admin", "another-correct-password", "duplicate@example.com");
    assert(duplicate.status == qornix_auth::AuthStatus::USER_ALREADY_EXISTS);

    auto store2 = qornix_auth::QornixOrmAuthStore::create(sharedDb, false);
    auto persisted = store2->findUserByUsername("admin");
    assert(persisted.has_value());
    assert(persisted->hasRole("admin"));
    assert(persisted->hasPermission("rag:metrics"));

    std::cout << "auth_orm_store_test passed\n";
    return 0;
}
