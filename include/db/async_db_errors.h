/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace qornix::db {

enum class DbErrorCode {
    Unknown,
    Connection,
    Auth,
    Timeout,
    Cancelled,
    PoolRejected,
    PoolTimeout,
    QueryRejected,
    ConstraintViolation,
    DuplicateKey,
    ForeignKeyViolation,
    Syntax,
    Conflict,
    Unavailable,
    ConnectionLost,
    NotSupported
};

inline const char* to_string(DbErrorCode code) {
    switch (code) {
        case DbErrorCode::Unknown: return "unknown";
        case DbErrorCode::Connection: return "connection";
        case DbErrorCode::Auth: return "auth";
        case DbErrorCode::Timeout: return "timeout";
        case DbErrorCode::Cancelled: return "cancelled";
        case DbErrorCode::PoolRejected: return "pool_rejected";
        case DbErrorCode::PoolTimeout: return "pool_timeout";
        case DbErrorCode::QueryRejected: return "query_rejected";
        case DbErrorCode::ConstraintViolation: return "constraint_violation";
        case DbErrorCode::DuplicateKey: return "duplicate_key";
        case DbErrorCode::ForeignKeyViolation: return "foreign_key_violation";
        case DbErrorCode::Syntax: return "syntax";
        case DbErrorCode::Conflict: return "conflict";
        case DbErrorCode::Unavailable: return "unavailable";
        case DbErrorCode::ConnectionLost: return "connection_lost";
        case DbErrorCode::NotSupported: return "not_supported";
    }
    return "unknown";
}

class DbError : public std::runtime_error {
public:
    DbError(DbErrorCode code, std::string message, std::string sql_state = {})
        : std::runtime_error(std::move(message)), code_(code), sql_state_(std::move(sql_state)) {}

    DbErrorCode code() const noexcept { return code_; }
    const std::string& sql_state() const noexcept { return sql_state_; }

private:
    DbErrorCode code_{DbErrorCode::Unknown};
    std::string sql_state_;
};

inline DbError db_timeout(std::string message = "database operation timed out") {
    return DbError(DbErrorCode::Timeout, std::move(message));
}

inline DbError db_cancelled(std::string message = "database operation cancelled") {
    return DbError(DbErrorCode::Cancelled, std::move(message));
}

inline DbError db_pool_rejected(std::string message = "database pool waiter limit exceeded") {
    return DbError(DbErrorCode::PoolRejected, std::move(message));
}

inline DbError db_pool_timeout(std::string message = "database pool acquisition timed out") {
    return DbError(DbErrorCode::PoolTimeout, std::move(message));
}

inline DbError db_not_supported(std::string message = "database feature is not supported by this driver") {
    return DbError(DbErrorCode::NotSupported, std::move(message));
}

} // namespace qornix::db
