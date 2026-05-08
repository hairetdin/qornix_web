/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */
#pragma once

#include <stdexcept>
#include <string>

// Base exception for all errors database.
class DatabaseError : public std::runtime_error {
public:
    explicit DatabaseError(const std::string &message)
        : std::runtime_error(message) {
    }
};

// Exception for errors connections.
class ConnectionError : public DatabaseError {
public:
    explicit ConnectionError(const std::string &message)
        : DatabaseError(message) {
    }
};

// Exception for errors "not found" (404)
class NotFoundError : public DatabaseError {
public:
    explicit NotFoundError(const std::string &message)
        : DatabaseError(message) {
    }
};

// Exception for errors validation (400)
class ValidationError : public DatabaseError {
public:
    explicit ValidationError(const std::string &message)
        : DatabaseError(message) {
    }
};

// Exception for server errors (500)
class ServerError : public DatabaseError {
public:
    explicit ServerError(const std::string &message)
        : DatabaseError(message) {
    }
};
