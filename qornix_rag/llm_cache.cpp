/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of Qornix project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "llm_cache.h"


#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netdb.h>
#include <sstream>
#include <stdexcept>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr int kRedisConnectTimeoutMs = 750;
constexpr int kRedisIoTimeoutMs = 1500;

std::string with_prefix(const CacheConfig& config, const std::string& key) {
    return config.key_prefix + key;
}

std::chrono::seconds effective_redis_ttl(const CacheConfig& config, const CacheEntry& entry) {
    if (entry.ttl.count() > 0) {
        return entry.ttl;
    }
    if (config.redis_ttl.count() > 0) {
        return config.redis_ttl;
    }
    return config.ttl.count() > 0 ? config.ttl : std::chrono::hours(1);
}

std::string serialize_cache_entry(const CacheEntry& entry) {
    std::ostringstream oss;
    oss << "qornix_llm_cache_entry_v1\n"
        << entry.tokens_used << "\n"
        << entry.answer.size() << "\n"
        << entry.answer;
    return oss.str();
}

std::optional<CacheEntry> deserialize_cache_entry(const std::string& raw, std::chrono::seconds ttl) {
    const std::string magic = "qornix_llm_cache_entry_v1\n";
    if (raw.rfind(magic, 0) != 0) {
        return std::nullopt;
    }

    size_t pos = magic.size();
    const size_t tokens_end = raw.find('\n', pos);
    if (tokens_end == std::string::npos) {
        return std::nullopt;
    }
    const std::string tokens_text = raw.substr(pos, tokens_end - pos);
    pos = tokens_end + 1;

    const size_t size_end = raw.find('\n', pos);
    if (size_end == std::string::npos) {
        return std::nullopt;
    }
    const std::string size_text = raw.substr(pos, size_end - pos);
    pos = size_end + 1;

    size_t tokens = 0;
    size_t answer_size = 0;
    try {
        tokens = static_cast<size_t>(std::stoull(tokens_text));
        answer_size = static_cast<size_t>(std::stoull(size_text));
    } catch (...) {
        return std::nullopt;
    }

    if (raw.size() - pos != answer_size) {
        return std::nullopt;
    }

    CacheEntry entry;
    entry.answer = raw.substr(pos, answer_size);
    entry.tokens_used = tokens;
    entry.created_at = std::chrono::steady_clock::now();
    entry.ttl = ttl.count() > 0 ? ttl : std::chrono::hours(1);
    return entry;
}

bool set_socket_timeout(int fd, int timeout_ms) {
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0
        && setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
}

bool send_all(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool recv_exact(int fd, std::string& out, size_t count) {
    out.clear();
    out.reserve(count);
    while (out.size() < count) {
        char buffer[4096];
        const size_t remaining = count - out.size();
        const size_t to_read = std::min(sizeof(buffer), remaining);
        const ssize_t n = ::recv(fd, buffer, to_read, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        out.append(buffer, static_cast<size_t>(n));
    }
    return true;
}

bool recv_line(int fd, std::string& line) {
    line.clear();
    char ch = '\0';
    char prev = '\0';
    while (true) {
        const ssize_t n = ::recv(fd, &ch, 1, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        if (prev == '\r' && ch == '\n') {
            line.pop_back();
            return true;
        }
        line.push_back(ch);
        prev = ch;
    }
}

std::string build_resp_command(const std::vector<std::string>& parts) {
    std::ostringstream oss;
    oss << "*" << parts.size() << "\r\n";
    for (const auto& part : parts) {
        oss << "$" << part.size() << "\r\n" << part << "\r\n";
    }
    return oss.str();
}

} // namespace

// ============================================================================
// Memory cache implementation
// ============================================================================

MemoryCache::MemoryCache(const CacheConfig& config)
    : max_size_(config.max_size) {
    stats_.max_size = max_size_;
}

std::optional<CacheEntry> MemoryCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = map_.find(key);
    if (it == map_.end()) {
        stats_.misses++;
        return std::nullopt;
    }

    // Check if entry is expired
    if (it->second->entry.is_expired()) {
        lru_list_.erase(it->second);
        map_.erase(it);
        stats_.size--;
        stats_.expired++;
        stats_.misses++;
        return std::nullopt;
    }

    // Move to front (most recently used)
    touch(it->second);
    stats_.hits++;
    return it->second->entry;
}

void MemoryCache::put(const std::string& key, const CacheEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = map_.find(key);
    if (it != map_.end()) {
        // Update existing entry
        it->second->entry = entry;
        touch(it->second);
        return;
    }

    // Evict if at capacity
    while (stats_.size >= max_size_) {
        evict_lru();
    }

    // Insert new entry at front
    lru_list_.emplace_front(key, entry);
    map_[key] = lru_list_.begin();
    stats_.size++;
}

void MemoryCache::invalidate(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = map_.find(key);
    if (it != map_.end()) {
        lru_list_.erase(it->second);
        map_.erase(it);
        stats_.size--;
    }
}

void MemoryCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);

    lru_list_.clear();
    map_.clear();
    stats_.size = 0;
}

CacheStats MemoryCache::get_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

// ============================================================================
// Redis cache implementation
// ============================================================================

struct RedisCache::RedisConnection {
    enum class Type {
        SimpleString,
        Error,
        Integer,
        BulkString,
        Array,
        Nil,
        Invalid
    };

    struct RespValue {
        Type type = Type::Invalid;
        std::string str;
        long long integer = 0;
        std::vector<RespValue> array;

        bool ok() const {
            return type != Type::Invalid && type != Type::Error;
        }
    };

    int fd = -1;

    ~RedisConnection() {
        close();
    }

    void close() {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

    bool connect_to(const std::string& host, int port, int timeout_ms) {
        close();

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* result = nullptr;
        const std::string port_str = std::to_string(port);
        if (::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result) != 0) {
            return false;
        }

        bool connected = false;
        for (addrinfo* rp = result; rp != nullptr && !connected; rp = rp->ai_next) {
            int candidate = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (candidate < 0) {
                continue;
            }

            const int original_flags = ::fcntl(candidate, F_GETFL, 0);
            if (original_flags >= 0) {
                ::fcntl(candidate, F_SETFL, original_flags | O_NONBLOCK);
            }

            int rc = ::connect(candidate, rp->ai_addr, rp->ai_addrlen);
            if (rc < 0 && errno == EINPROGRESS) {
                fd_set write_fds;
                FD_ZERO(&write_fds);
                FD_SET(candidate, &write_fds);

                timeval tv{};
                tv.tv_sec = timeout_ms / 1000;
                tv.tv_usec = (timeout_ms % 1000) * 1000;
                rc = ::select(candidate + 1, nullptr, &write_fds, nullptr, &tv);
                if (rc > 0 && FD_ISSET(candidate, &write_fds)) {
                    int socket_error = 0;
                    socklen_t len = sizeof(socket_error);
                    if (::getsockopt(candidate, SOL_SOCKET, SO_ERROR, &socket_error, &len) == 0 && socket_error == 0) {
                        connected = true;
                    }
                }
            } else if (rc == 0) {
                connected = true;
            }

            if (connected) {
                if (original_flags >= 0) {
                    ::fcntl(candidate, F_SETFL, original_flags);
                }
                fd = candidate;
                set_socket_timeout(fd, kRedisIoTimeoutMs);
            } else {
                ::close(candidate);
            }
        }

        ::freeaddrinfo(result);
        return connected;
    }

    std::optional<RespValue> read_value() {
        if (fd < 0) {
            return std::nullopt;
        }

        char prefix = '\0';
        const ssize_t n = ::recv(fd, &prefix, 1, 0);
        if (n <= 0) {
            return std::nullopt;
        }

        std::string line;
        RespValue value;

        switch (prefix) {
        case '+':
            if (!recv_line(fd, line)) return std::nullopt;
            value.type = Type::SimpleString;
            value.str = line;
            return value;
        case '-':
            if (!recv_line(fd, line)) return std::nullopt;
            value.type = Type::Error;
            value.str = line;
            return value;
        case ':':
            if (!recv_line(fd, line)) return std::nullopt;
            try {
                value.type = Type::Integer;
                value.integer = std::stoll(line);
                return value;
            } catch (...) {
                return std::nullopt;
            }
        case '$': {
            if (!recv_line(fd, line)) return std::nullopt;
            long long length = -1;
            try {
                length = std::stoll(line);
            } catch (...) {
                return std::nullopt;
            }
            if (length < 0) {
                value.type = Type::Nil;
                return value;
            }
            std::string body;
            if (!recv_exact(fd, body, static_cast<size_t>(length))) return std::nullopt;
            std::string crlf;
            if (!recv_exact(fd, crlf, 2) || crlf != "\r\n") return std::nullopt;
            value.type = Type::BulkString;
            value.str = std::move(body);
            return value;
        }
        case '*': {
            if (!recv_line(fd, line)) return std::nullopt;
            long long count = -1;
            try {
                count = std::stoll(line);
            } catch (...) {
                return std::nullopt;
            }
            if (count < 0) {
                value.type = Type::Nil;
                return value;
            }
            value.type = Type::Array;
            value.array.reserve(static_cast<size_t>(count));
            for (long long i = 0; i < count; ++i) {
                auto child = read_value();
                if (!child) {
                    return std::nullopt;
                }
                value.array.push_back(std::move(*child));
            }
            return value;
        }
        default:
            return std::nullopt;
        }
    }

    std::optional<RespValue> command(const std::vector<std::string>& parts) {
        if (fd < 0) {
            return std::nullopt;
        }
        const auto request = build_resp_command(parts);
        if (!send_all(fd, request)) {
            close();
            return std::nullopt;
        }
        auto response = read_value();
        if (!response || response->type == Type::Error) {
            return response;
        }
        return response;
    }
};

RedisCache::RedisCache(const CacheConfig& config)
    : connection_(std::make_unique<RedisConnection>()),
      config_(config) {
    stats_.max_size = 0; // Redis capacity is controlled by Redis maxmemory/eviction policy.
    connected_.store(connect());
}

RedisCache::~RedisCache() = default;

bool RedisCache::connect() {
    if (!connection_) {
        connection_ = std::make_unique<RedisConnection>();
    }

    if (!connection_->connect_to(config_.redis_host, config_.redis_port, kRedisConnectTimeoutMs)) {
        connected_.store(false);
        return false;
    }

    if (!config_.redis_password.empty()) {
        auto auth = connection_->command({"AUTH", config_.redis_password});
        if (!auth || !auth->ok()) {
            connection_->close();
            connected_.store(false);
            return false;
        }
    }

    auto select = connection_->command({"SELECT", std::to_string(config_.redis_db)});
    if (!select || !select->ok()) {
        connection_->close();
        connected_.store(false);
        return false;
    }

    auto ping = connection_->command({"PING"});
    const bool pong = ping && ping->ok()
        && (ping->str == "PONG" || ping->type == RedisConnection::Type::SimpleString);
    if (!pong) {
        connection_->close();
        connected_.store(false);
        return false;
    }

    connected_.store(true);
    return true;
}

std::string RedisCache::redis_command(const std::string& cmd) {
    (void)cmd;
    return "";
}

std::optional<CacheEntry> RedisCache::parse_redis_value(const std::string& raw) {
    return deserialize_cache_entry(raw, config_.redis_ttl);
}

std::optional<CacheEntry> RedisCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_.load() && !connect()) {
        stats_.misses++;
        return std::nullopt;
    }

    auto response = connection_->command({"GET", with_prefix(config_, key)});
    if (!response) {
        connected_.store(false);
        stats_.misses++;
        return std::nullopt;
    }
    if (response->type == RedisConnection::Type::Nil) {
        stats_.misses++;
        return std::nullopt;
    }
    if (response->type != RedisConnection::Type::BulkString) {
        stats_.misses++;
        return std::nullopt;
    }

    auto entry = parse_redis_value(response->str);
    if (!entry) {
        stats_.misses++;
        return std::nullopt;
    }

    stats_.hits++;
    return entry;
}

void RedisCache::put(const std::string& key, const CacheEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_.load() && !connect()) {
        return;
    }

    const auto ttl = effective_redis_ttl(config_, entry);
    auto response = connection_->command({
        "SETEX",
        with_prefix(config_, key),
        std::to_string(std::max<long long>(1, ttl.count())),
        serialize_cache_entry(entry)
    });

    if (!response || !response->ok()) {
        connected_.store(false);
        return;
    }

    // Redis does not expose cheap per-prefix size on SETEX. Track approximate writes.
    if (stats_.size == 0) {
        stats_.size = 1;
    }
}

void RedisCache::invalidate(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_.load() && !connect()) {
        return;
    }

    auto response = connection_->command({"DEL", with_prefix(config_, key)});
    if (!response || !response->ok()) {
        connected_.store(false);
        return;
    }
    if (response->type == RedisConnection::Type::Integer && response->integer > 0 && stats_.size > 0) {
        stats_.size--;
    }
}

void RedisCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_.load() && !connect()) {
        return;
    }

    std::string cursor = "0";
    do {
        auto response = connection_->command({"SCAN", cursor, "MATCH", config_.key_prefix + "*", "COUNT", "100"});
        if (!response || response->type != RedisConnection::Type::Array || response->array.size() != 2) {
            connected_.store(false);
            return;
        }
        const auto& next_cursor = response->array[0];
        const auto& keys = response->array[1];
        if (next_cursor.type != RedisConnection::Type::BulkString || keys.type != RedisConnection::Type::Array) {
            connected_.store(false);
            return;
        }
        cursor = next_cursor.str;
        for (const auto& key_value : keys.array) {
            if (key_value.type == RedisConnection::Type::BulkString) {
                connection_->command({"DEL", key_value.str});
            }
        }
    } while (cursor != "0");

    stats_.size = 0;
}

CacheStats RedisCache::get_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

// ============================================================================
// Cache factory
// ============================================================================

std::shared_ptr<ICache> create_cache(const CacheConfig& config) {
    if (!config.enabled) {
        return nullptr;
    }

    if (config.backend == "redis") {
        auto redis = std::make_shared<RedisCache>(config);
        if (redis->is_available()) {
            std::cout << "  ✅ Cache: Redis backend connected ("
                      << config.redis_host << ":" << config.redis_port
                      << ", db=" << config.redis_db
                      << ", ttl=" << config.redis_ttl.count() << "s)" << std::endl;
            return redis;
        }
        std::cerr << "  ⚠️  Redis backend unavailable, falling back to memory cache" << std::endl;
    }

    // Default: memory cache
    auto memory = std::make_shared<MemoryCache>(config);
    std::cout << "  ✅ Cache: Memory backend (max=" << config.max_size
              << ", ttl=" << std::chrono::duration_cast<std::chrono::minutes>(config.ttl).count()
              << " min)" << std::endl;
    return memory;
}

// ============================================================================
// Hashing utilities
// ============================================================================

size_t hash_cache_key(const std::string& question,
                      const std::string& context,
                      const std::string& model,
                      float temperature) {
    // Combine all inputs into a single hash
    std::hash<std::string> string_hash;
    std::hash<float> float_hash;

    size_t h1 = string_hash(question);
    size_t h2 = string_hash(context);
    size_t h3 = string_hash(model);
    size_t h4 = float_hash(temperature);

    // Combine using boost::hash_combine style
    h1 ^= h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
    h1 ^= h3 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
    h1 ^= h4 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);

    return h1;
}

std::string make_cache_key(const std::string& question,
                           const std::string& context,
                           const std::string& model,
                           float temperature) {
    // Create a hex-encoded cache key for better readability in Redis
    size_t hash = hash_cache_key(question, context, model, temperature);

    std::ostringstream oss;
    oss << std::hex << hash;
    return oss.str();
}
