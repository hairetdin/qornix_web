/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <memory>
#include <string>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace qornix_auth {

class PasswordHasher {
public:
    virtual ~PasswordHasher() = default;

    virtual std::string hash(const std::string& password,
                            const std::string& salt = "") = 0;

    virtual bool verify(const std::string& password,
                       const std::string& expectedHash,
                       const std::string& salt) = 0;

    virtual std::string generateSalt() = 0;

    static std::unique_ptr<PasswordHasher> createDefault();
};

class SHA256Hasher : public PasswordHasher {
private:
    static constexpr int SALT_LENGTH = 32;
    static constexpr int ITERATIONS = 10000;

    static std::string bytesToHex(const unsigned char* bytes, size_t length) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (size_t i = 0; i < length; ++i) {
            ss << std::setw(2) << static_cast<int>(bytes[i]);
        }
        return ss.str();
    }

    static std::string sha256(const std::string& input) {
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hash_length = 0;

        if (EVP_Digest(input.data(), input.size(), hash, &hash_length, EVP_sha256(), nullptr) != 1) {
            throw std::runtime_error("Failed to calculate SHA-256 digest");
        }

        return bytesToHex(hash, hash_length);
    }

public:
    std::string generateSalt() override {
        unsigned char salt[SALT_LENGTH];
        if (RAND_bytes(salt, SALT_LENGTH) != 1) {
            throw std::runtime_error("Failed to generate random salt");
        }
        return bytesToHex(salt, SALT_LENGTH);
    }

    std::string hash(const std::string& password,
                    const std::string& salt = "") override {
        if (salt.empty()) {
            throw std::invalid_argument("Salt cannot be empty");
        }

        std::string currentHash = password + salt;
        for (int i = 0; i < ITERATIONS; ++i) {
            currentHash = sha256(currentHash);
        }
        return currentHash;
    }

    bool verify(const std::string& password,
               const std::string& expectedHash,
               const std::string& salt) override {
        try {
            std::string computedHash = this->hash(password, salt);
            return computedHash == expectedHash;
        } catch (...) {
            return false;
        }
    }
};

class PBKDF2SHA256Hasher : public PasswordHasher {
private:
    static constexpr int SALT_LENGTH = 32;
    static constexpr int DEFAULT_ITERATIONS = 210000;
    static constexpr int KEY_LENGTH = 32;

    int iterations_;

    static std::string bytesToHex(const unsigned char* bytes, size_t length) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (size_t i = 0; i < length; ++i) {
            ss << std::setw(2) << static_cast<int>(bytes[i]);
        }
        return ss.str();
    }

public:
    explicit PBKDF2SHA256Hasher(int iterations = DEFAULT_ITERATIONS)
        : iterations_(iterations > 0 ? iterations : DEFAULT_ITERATIONS) {}

    std::string generateSalt() override {
        unsigned char salt[SALT_LENGTH];
        if (RAND_bytes(salt, SALT_LENGTH) != 1) {
            throw std::runtime_error("Failed to generate random salt");
        }
        return bytesToHex(salt, SALT_LENGTH);
    }

    std::string hash(const std::string& password,
                    const std::string& salt = "") override {
        if (salt.empty()) {
            throw std::invalid_argument("Salt cannot be empty");
        }

        unsigned char derived[KEY_LENGTH];
        if (PKCS5_PBKDF2_HMAC(
                password.c_str(),
                static_cast<int>(password.size()),
                reinterpret_cast<const unsigned char*>(salt.data()),
                static_cast<int>(salt.size()),
                iterations_,
                EVP_sha256(),
                KEY_LENGTH,
                derived) != 1) {
            throw std::runtime_error("Failed to derive password hash");
        }

        return "pbkdf2-sha256$" + std::to_string(iterations_) + "$" + bytesToHex(derived, KEY_LENGTH);
    }

    bool verify(const std::string& password,
               const std::string& expectedHash,
               const std::string& salt) override {
        try {
            if (expectedHash.rfind("pbkdf2-sha256$", 0) != 0) {
                SHA256Hasher legacy;
                return legacy.verify(password, expectedHash, salt);
            }

            const auto first = expectedHash.find('$');
            const auto second = expectedHash.find('$', first + 1);
            if (first == std::string::npos || second == std::string::npos) {
                return false;
            }

            const int previousIterations = std::stoi(expectedHash.substr(first + 1, second - first - 1));
            PBKDF2SHA256Hasher verifier(previousIterations);
            return verifier.hash(password, salt) == expectedHash;
        } catch (...) {
            return false;
        }
    }
};

inline std::unique_ptr<PasswordHasher> PasswordHasher::createDefault() {
    return std::make_unique<PBKDF2SHA256Hasher>();
}

} // namespace qornix_auth
