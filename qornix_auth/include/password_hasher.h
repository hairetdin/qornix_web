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

inline std::unique_ptr<PasswordHasher> PasswordHasher::createDefault() {
    return std::make_unique<SHA256Hasher>();
}

} // namespace qornix_auth
