/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <boost/asio.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <stdexcept>
#include <utility>
#include <vector>

#include "async_db_driver.h"
#include "async_db_errors.h"
#include "../../qornix_orm/database/IDatabase.h"

namespace qornix::db {

namespace net = boost::asio;

class SyncOffloadedAsyncDriver final : public IAsyncDatabaseDriver {
public:
    SyncOffloadedAsyncDriver(net::any_io_executor executor,
                             std::shared_ptr<IDatabase> sync_driver,
                             std::string connection_string,
                             std::size_t worker_threads = 1)
        : executor_(std::move(executor)),
          sync_driver_(std::move(sync_driver)),
          connection_string_(std::move(connection_string)),
          workers_(std::max<std::size_t>(1, worker_threads)) {
        if (!sync_driver_) {
            throw std::invalid_argument("sync DB driver is required for offloaded async driver");
        }
    }

    ~SyncOffloadedAsyncDriver() override {
        workers_.join();
    }

    net::awaitable<void> connect(const CancellationToken& token = {}) override {
        co_await run_blocking([this] {
            std::lock_guard<std::mutex> lock(driver_mutex_);
            sync_driver_->connect(connection_string_);
        }, std::chrono::milliseconds{5000}, token);
    }

    net::awaitable<void> close() override {
        co_await run_blocking([this] {
            std::lock_guard<std::mutex> lock(driver_mutex_);
            if (sync_driver_->isConnected()) {
                sync_driver_->disconnect();
            }
        }, std::chrono::milliseconds{5000}, CancellationToken{});
    }

    bool is_open() const override {
        std::lock_guard<std::mutex> lock(driver_mutex_);
        return sync_driver_->isConnected();
    }

    std::string driver_name() const override { return "sync_offloaded"; }

    net::awaitable<QueryResult> query(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {}) override {
        const auto timeout = effective_timeout(options, token);
        co_return co_await run_blocking([this, sql = std::move(sql), params = std::move(params)] {
            std::lock_guard<std::mutex> lock(driver_mutex_);
            std::vector<std::string> string_params;
            string_params.reserve(params.size());
            for (const auto& param : params) {
                string_params.push_back(param.is_null ? std::string{} : param.value);
            }
            const auto response = sync_driver_->exec(sql, string_params);
            QueryResult result;
            result.affected_rows = static_cast<std::uint64_t>(std::max(0, response.affected_rows));
            result.command_tag = "SYNC_OFFLOADED";
            result.rows.reserve(response.data.size());
            for (const auto& source_row : response.data) {
                Row row;
                for (const auto& [key, value] : source_row) {
                    row.columns[key] = value;
                }
                result.rows.push_back(std::move(row));
            }
            return result;
        }, timeout, token);
    }


    net::awaitable<QueryResult> execute(
        std::string sql,
        QueryParams params = {},
        QueryOptions options = {},
        CancellationToken token = {}) override {
        co_return co_await query(std::move(sql), std::move(params), std::move(options), std::move(token));
    }

    net::awaitable<void> prepare(std::string, std::string, CancellationToken = {}) override {
        throw db_not_supported("prepared statements are not supported by sync_offloaded driver");
    }

    net::awaitable<void> begin(CancellationToken token = {}) override {
        (void)co_await execute("BEGIN", {}, {}, std::move(token));
    }

    net::awaitable<void> commit(CancellationToken token = {}) override {
        (void)co_await execute("COMMIT", {}, {}, std::move(token));
    }

    net::awaitable<void> rollback(CancellationToken token = {}) override {
        (void)co_await execute("ROLLBACK", {}, {}, std::move(token));
    }

private:
    static std::chrono::milliseconds effective_timeout(const QueryOptions& options, const CancellationToken& token) {
        const auto remaining = token.remaining();
        return remaining ? std::min(options.timeout, *remaining) : options.timeout;
    }

    template <typename Fn>
    net::awaitable<std::invoke_result_t<Fn>> run_blocking(Fn fn,
                                                          std::chrono::milliseconds timeout,
                                                          CancellationToken token) {
        using Result = std::invoke_result_t<Fn>;
        struct State {
            std::atomic<bool> done{false};
            std::exception_ptr error;
            std::conditional_t<std::is_void_v<Result>, std::monostate, std::optional<Result>> value;
        };

        auto state = std::make_shared<State>();
        net::post(workers_, [state, fn = std::move(fn)]() mutable {
            try {
                if constexpr (std::is_void_v<Result>) {
                    fn();
                } else {
                    state->value = fn();
                }
            } catch (...) {
                state->error = std::current_exception();
            }
            state->done.store(true, std::memory_order_release);
        });

        net::steady_timer timer(executor_);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!state->done.load(std::memory_order_acquire)) {
            if (token.is_cancelled()) {
                throw db_cancelled();
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                throw db_timeout("offloaded sync database operation timed out");
            }
            timer.expires_after(std::chrono::milliseconds{1});
            co_await timer.async_wait(net::use_awaitable);
        }

        if (state->error) {
            std::rethrow_exception(state->error);
        }
        if constexpr (std::is_void_v<Result>) {
            co_return;
        } else {
            co_return std::move(*state->value);
        }
    }

    net::any_io_executor executor_;
    std::shared_ptr<IDatabase> sync_driver_;
    std::string connection_string_;
    net::thread_pool workers_;
    mutable std::mutex driver_mutex_;
};

} // namespace qornix::db
