# Qornix Auth

`qornix_auth` is the authentication and authorization module for generated
`qornix_web` applications. Standalone local tools such as `qornix_rag` should
remain usable without mandatory login, but networked generated applications
should use `qornix_auth` as the primary auth layer.

## Current Scope

- user registration and password authentication;
- PBKDF2-SHA256 password hashing through OpenSSL;
- cryptographically random session ids;
- optional JWT token generation when `ENABLE_JWT=ON` and `jwt-cpp` is available;
- role and permission metadata on users, sessions, and auth results;
- an `AuthStore` boundary with an in-memory implementation;
- a `QornixOrmAuthStore` implementation that uses `qornix_orm` and can run on
  SQLite, PostgreSQL, or MySQL depending on the configured ORM driver;
- `AuthMiddleware` for session/JWT validation and role/permission checks;
- `AuthApiHandler` routes for login/logout/current-user and admin user-management flows.

The default password policy requires at least 12 characters. Applications can
override this through `AuthConfig::minPasswordLength`, but generated production
apps should keep a stronger policy.

## Core Usage

```cpp
#include "auth_manager.h"

qornix_auth::AuthConfig config;
config.mode = qornix_auth::AuthMode::SESSION;
config.defaultRoles = {"user"};
config.defaultPermissions = {"rag:read"};

auto auth = std::make_shared<qornix_auth::AuthManager>(config);

auth->registerUser(
    "admin",
    "change-this-admin-password",
    "admin@example.com",
    {"admin"},
    {"rag:read", "rag:write", "rag:admin", "auth:admin"});

auto result = auth->authenticate("admin", "change-this-admin-password");
```

`AuthResult` contains structured fields such as `userId`, `username`,
`sessionId`, `token`, `roles`, and `permissions`. The legacy `message` string
still includes session/token fragments for older examples.

## HTTP Routes

`include/auth_routes.h` provides a Qornix Web bridge:

```cpp
#include "auth_routes.h"

setupAuthRoutes(server, authManager, false);
```

Registered routes:

- `POST /auth/login` with `{"username":"...","password":"..."}`;
- `POST /auth/logout`;
- `GET /auth/me`;
- `GET /auth/users` for admin user listing;
- `POST /auth/users` for admin user creation;
- `PATCH /auth/users/{id}` for admin role, permission, active-state, profile, and password updates;
- `POST /auth/register` when registration is explicitly enabled.

Successful session login sets a `session_id` HTTP-only cookie. JWT tokens are
included in the JSON response when JWT support is compiled and enabled.

The `/auth/users` admin routes are regular auth-protected routes. Generated
applications protect them with `auth.admin_permissions`, which defaults to
`auth:admin`. Bootstrap admin users in generated apps should include
`auth:admin` alongside any application permissions they need.

## Middleware

`include/auth_middleware.h` validates `session_id` cookies and bearer tokens:

```cpp
auto middleware = create_auth_middleware(authManager, true, {"/auth/login", "/health"});
middleware->requireAnyRole({"admin"});
middleware->requireAnyPermission({"rag:admin"});
server.add_middleware(middleware);
```

For route-level policies, register the most specific rules first:

```cpp
middleware->addRoutePolicy("GET", "/rag", {"rag:read"}, {}, true);
middleware->addRoutePolicy("POST", "/api/rag/index", {"rag:write"}, {}, true);
middleware->addRoutePolicy("GET", "/api/rag/admin", {"rag:admin"});
```

Generated RAG applications use the default permission split:

- `rag:read` for `/rag`, health, sources, stats, search, ask, and batch;
- `rag:write` for indexing, ingestion, document delete, QA writes, imports, and source writes;
- `rag:admin` for diagnostics, metrics, analytics, and admin routes.
- `auth:admin` for `/auth/users` user-management routes.

Session and bearer-token checks re-read the user from `AuthStore` before
authorizing a request. Role/permission edits therefore apply to existing
sessions and JWTs, and disabling a user blocks further session and bearer-token
access. Non-exact route policies and excluded paths match only whole path
segments, so a policy for `/api/rag/admin` does not match
`/api/rag/administrator`.

Authenticated responses expose downstream headers:

- `X-User-ID`;
- `X-Username`;
- `X-User-Roles`;
- `X-User-Permissions`;
- `X-Auth-Credential-Type`.

These headers are a bridge for the current synchronous handler API. For deeper
integration, prefer passing `AuthContext` through host application request
context or async middleware.

## Storage Boundary

`AuthStore` is the persistence boundary. `InMemoryAuthStore` is suitable for
tests, examples, and local demos only.

Use `QornixOrmAuthStore` for generated applications that need durable users and
role assignments:

```cpp
#include "auth_orm_store.h"
#include "database_interface.h"

DatabaseConfig dbConfig;
dbConfig.driver = "postgresql"; // or sqlite/mysql
dbConfig.connectionString = "host=127.0.0.1 port=5432 dbname=app user=app password=secret";

auto db = DatabaseInterface::init(dbConfig);
auto sharedDb = std::shared_ptr<DatabaseInterface>(std::move(db));
auto store = qornix_auth::QornixOrmAuthStore::create(sharedDb);
auto auth = std::make_shared<qornix_auth::AuthManager>(config, store);
```

`QornixOrmAuthStore::migrate()` creates these tables:

- `auth_users`;
- `auth_roles`;
- `auth_permissions`;
- `auth_user_roles`;
- `auth_user_permissions`.

The same store code is used for SQLite, PostgreSQL, and MySQL. SQLite is the
self-contained test backend; PostgreSQL/MySQL should be verified with the
existing ORM driver flags and DSN-gated integration tests in deployment CI.
