/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <string>
#include <chrono>
#include <optional>
#include <map>

#ifdef HAVE_JWT_CPP
#include <jwt-cpp/jwt.h>
#endif

namespace qornix_auth {
    struct JwtClaims {
        std::string subject;
        std::string issuer;
        std::map<std::string, std::string> customClaims;
        std::chrono::system_clock::time_point issuedAt;
        std::chrono::system_clock::time_point expiresAt;

        JwtClaims() : issuedAt(std::chrono::system_clock::now()) {
        }

        JwtClaims(const std::string &sub,
                  const std::string &iss,
                  std::chrono::minutes duration = std::chrono::minutes(60))
            : subject(sub), issuer(iss), issuedAt(std::chrono::system_clock::now()) {
            expiresAt = issuedAt + duration;
        }
    };

    struct JwtTokenResult {
        bool success;
        std::string token;
        std::string error;
        std::optional<JwtClaims> claims;

        static JwtTokenResult ok(const std::string &tok) {
            return {true, tok, "", std::nullopt};
        }

        static JwtTokenResult fail(const std::string &err) {
            return {false, "", err, std::nullopt};
        }
    };

    class JwtManager {
    private:
        std::string secretKey_;
        std::string issuer_;
        std::chrono::minutes tokenDuration_;

    public:
        JwtManager(const std::string &secretKey,
                   const std::string &issuer = "qornix-auth",
                   std::chrono::minutes duration = std::chrono::minutes(60))
            : secretKey_(secretKey), issuer_(issuer), tokenDuration_(duration) {
            if (secretKey.empty()) {
                throw std::invalid_argument("Secret key cannot be empty");
            }
        }

        JwtTokenResult generateToken(const std::string &userId,
                                     const std::string &username,
                                     const std::map<std::string, std::string> &extraClaims = {}) {
            try {
#ifdef HAVE_JWT_CPP
                auto now = std::chrono::system_clock::now();
                auto expire = now + tokenDuration_;

                jwt::jwt_object obj{
                    jwt::params{{"alg", "HS256"}}
                };

                obj.payload().set_subject(userId);
                obj.payload().set_issuer(issuer_);
                obj.payload().set_issued_at(now);
                obj.payload().set_expires_at(expire);
                obj.payload().set_claim("username", username);

                for (const auto &[key, value]: extraClaims) {
                    obj.payload().set_claim(key, value);
                }

                obj.signature().sign(jwt::algorithm::hs256{secretKey_});

                return JwtTokenResult::ok(obj.signature().get());
#else
                return JwtTokenResult::fail("JWT support not compiled");
#endif
            } catch (const std::exception &e) {
                return JwtTokenResult::fail(std::string("Token generation failed: ") + e.what());
            }
        }

        JwtTokenResult validateToken(const std::string &token) {
            try {
#ifdef HAVE_JWT_CPP
                auto decoded = jwt::decode(token);

                auto verifier = jwt::verify()
                        .allow_algorithm(jwt::algorithm::hs256{secretKey_})
                        .with_issuer(issuer_);

                verifier.verify(decoded);

                JwtClaims claims;
                claims.subject = decoded.get_subject();
                claims.issuer = decoded.get_issuer();

                if (decoded.has_payload_claim("username")) {
                    claims.customClaims["username"] = decoded.get_payload_claim("username").as_string();
                }

                claims.issuedAt = decoded.get_issued_at();
                claims.expiresAt = decoded.get_expires_at();

                return {true, token, "", claims};
#else
                return JwtTokenResult::fail("JWT support not compiled");
#endif
            } catch (const std::exception &e) {
                return JwtTokenResult::fail(std::string("Token validation failed: ") + e.what());
            }
        }

        std::optional<std::string> extractUserId(const std::string &token) {
            auto result = validateToken(token);
            if (result.success && result.claims) {
                return result.claims->subject;
            }
            return std::nullopt;
        }

        void setSecretKey(const std::string &key) {
            secretKey_ = key;
        }

        void setTokenDuration(std::chrono::minutes duration) {
            tokenDuration_ = duration;
        }
    };
} // namespace qornix_auth
