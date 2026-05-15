/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace qornix::async {

namespace net = boost::asio;
namespace http = boost::beast::http;
using Clock = std::chrono::steady_clock;

class TimeoutError : public std::runtime_error {
public:
    explicit TimeoutError(const std::string& message) : std::runtime_error(message) {}
};

class OverloadError : public std::runtime_error {
public:
    explicit OverloadError(const std::string& message) : std::runtime_error(message) {}
};

struct HttpMetricsSnapshot {
    std::uint64_t total_requests{0};
    std::uint64_t active_requests{0};
    std::uint64_t completed_requests{0};
    std::uint64_t failed_requests{0};
    std::uint64_t timed_out_requests{0};
    std::uint64_t cancelled_requests{0};
    std::uint64_t rejected_requests{0};
    std::uint64_t queued_requests{0};
    std::uint64_t active_blocking_tasks{0};
    std::uint64_t queued_blocking_tasks{0};
    std::uint64_t rejected_blocking_tasks{0};
    std::uint64_t active_db_operations{0};
    std::uint64_t queued_db_waiters{0};
    std::uint64_t rejected_db_waiters{0};
    double p50_latency_ms{0.0};
    double p95_latency_ms{0.0};
    double p99_latency_ms{0.0};
};

class HttpMetrics {
public:
    void record_request_started() {
        total_requests_.fetch_add(1, std::memory_order_relaxed);
        active_requests_.fetch_add(1, std::memory_order_relaxed);
    }

    void record_request_finished(http::status status,
                                 std::chrono::steady_clock::duration latency) {
        active_requests_.fetch_sub(1, std::memory_order_relaxed);
        if (static_cast<unsigned>(status) >= 500) {
            failed_requests_.fetch_add(1, std::memory_order_relaxed);
        } else {
            completed_requests_.fetch_add(1, std::memory_order_relaxed);
        }
        observe_latency(latency);
    }

    void record_request_rejected() { rejected_requests_.fetch_add(1, std::memory_order_relaxed); }
    void record_request_timed_out() { timed_out_requests_.fetch_add(1, std::memory_order_relaxed); }
    void record_request_cancelled() { cancelled_requests_.fetch_add(1, std::memory_order_relaxed); }

    void set_queued_requests(std::uint64_t value) { queued_requests_.store(value, std::memory_order_relaxed); }
    void set_active_blocking_tasks(std::uint64_t value) { active_blocking_tasks_.store(value, std::memory_order_relaxed); }
    void set_queued_blocking_tasks(std::uint64_t value) { queued_blocking_tasks_.store(value, std::memory_order_relaxed); }
    void record_blocking_task_rejected() { rejected_blocking_tasks_.fetch_add(1, std::memory_order_relaxed); }
    void set_active_db_operations(std::uint64_t value) { active_db_operations_.store(value, std::memory_order_relaxed); }
    void set_queued_db_waiters(std::uint64_t value) { queued_db_waiters_.store(value, std::memory_order_relaxed); }
    void record_db_waiter_rejected() { rejected_db_waiters_.fetch_add(1, std::memory_order_relaxed); }

    HttpMetricsSnapshot snapshot() const {
        HttpMetricsSnapshot result;
        result.total_requests = total_requests_.load(std::memory_order_relaxed);
        result.active_requests = active_requests_.load(std::memory_order_relaxed);
        result.completed_requests = completed_requests_.load(std::memory_order_relaxed);
        result.failed_requests = failed_requests_.load(std::memory_order_relaxed);
        result.timed_out_requests = timed_out_requests_.load(std::memory_order_relaxed);
        result.cancelled_requests = cancelled_requests_.load(std::memory_order_relaxed);
        result.rejected_requests = rejected_requests_.load(std::memory_order_relaxed);
        result.queued_requests = queued_requests_.load(std::memory_order_relaxed);
        result.active_blocking_tasks = active_blocking_tasks_.load(std::memory_order_relaxed);
        result.queued_blocking_tasks = queued_blocking_tasks_.load(std::memory_order_relaxed);
        result.rejected_blocking_tasks = rejected_blocking_tasks_.load(std::memory_order_relaxed);
        result.active_db_operations = active_db_operations_.load(std::memory_order_relaxed);
        result.queued_db_waiters = queued_db_waiters_.load(std::memory_order_relaxed);
        result.rejected_db_waiters = rejected_db_waiters_.load(std::memory_order_relaxed);

        std::vector<double> samples;
        {
            std::lock_guard<std::mutex> lock(latency_mutex_);
            samples = latency_ms_;
        }
        if (!samples.empty()) {
            std::sort(samples.begin(), samples.end());
            result.p50_latency_ms = percentile(samples, 0.50);
            result.p95_latency_ms = percentile(samples, 0.95);
            result.p99_latency_ms = percentile(samples, 0.99);
        }
        return result;
    }

    std::string to_json() const {
        const auto s = snapshot();
        std::ostringstream out;
        out << "{"
            << "\"total_requests\":" << s.total_requests << ","
            << "\"active_requests\":" << s.active_requests << ","
            << "\"completed_requests\":" << s.completed_requests << ","
            << "\"failed_requests\":" << s.failed_requests << ","
            << "\"timed_out_requests\":" << s.timed_out_requests << ","
            << "\"cancelled_requests\":" << s.cancelled_requests << ","
            << "\"rejected_requests\":" << s.rejected_requests << ","
            << "\"queued_requests\":" << s.queued_requests << ","
            << "\"active_blocking_tasks\":" << s.active_blocking_tasks << ","
            << "\"queued_blocking_tasks\":" << s.queued_blocking_tasks << ","
            << "\"rejected_blocking_tasks\":" << s.rejected_blocking_tasks << ","
            << "\"active_db_operations\":" << s.active_db_operations << ","
            << "\"queued_db_waiters\":" << s.queued_db_waiters << ","
            << "\"rejected_db_waiters\":" << s.rejected_db_waiters << ","
            << "\"p50_latency_ms\":" << s.p50_latency_ms << ","
            << "\"p95_latency_ms\":" << s.p95_latency_ms << ","
            << "\"p99_latency_ms\":" << s.p99_latency_ms
            << "}";
        return out.str();
    }

    std::string to_prometheus() const {
        const auto s = snapshot();
        std::ostringstream out;
        out << "qornix_http_requests_total " << s.total_requests << "\n"
            << "qornix_http_requests_active " << s.active_requests << "\n"
            << "qornix_http_requests_completed_total " << s.completed_requests << "\n"
            << "qornix_http_requests_failed_total " << s.failed_requests << "\n"
            << "qornix_http_requests_timed_out_total " << s.timed_out_requests << "\n"
            << "qornix_http_requests_cancelled_total " << s.cancelled_requests << "\n"
            << "qornix_http_requests_rejected_total " << s.rejected_requests << "\n"
            << "qornix_http_latency_p50_ms " << s.p50_latency_ms << "\n"
            << "qornix_http_latency_p95_ms " << s.p95_latency_ms << "\n"
            << "qornix_http_latency_p99_ms " << s.p99_latency_ms << "\n"
            << "qornix_blocking_tasks_active " << s.active_blocking_tasks << "\n"
            << "qornix_blocking_tasks_queued " << s.queued_blocking_tasks << "\n"
            << "qornix_db_operations_active " << s.active_db_operations << "\n"
            << "qornix_db_waiters_queued " << s.queued_db_waiters << "\n";
        return out.str();
    }

private:
    static double percentile(const std::vector<double>& sorted, double p) {
        if (sorted.empty()) {
            return 0.0;
        }
        const auto index = static_cast<std::size_t>((sorted.size() - 1) * p);
        return sorted[index];
    }

    void observe_latency(std::chrono::steady_clock::duration latency) {
        const auto ms = std::chrono::duration<double, std::milli>(latency).count();
        std::lock_guard<std::mutex> lock(latency_mutex_);
        if (latency_ms_.size() >= max_latency_samples_) {
            latency_ms_.erase(latency_ms_.begin(), latency_ms_.begin() + max_latency_samples_ / 4);
        }
        latency_ms_.push_back(ms);
    }

    static constexpr std::size_t max_latency_samples_ = 16384;
    std::atomic<std::uint64_t> total_requests_{0};
    std::atomic<std::uint64_t> active_requests_{0};
    std::atomic<std::uint64_t> completed_requests_{0};
    std::atomic<std::uint64_t> failed_requests_{0};
    std::atomic<std::uint64_t> timed_out_requests_{0};
    std::atomic<std::uint64_t> cancelled_requests_{0};
    std::atomic<std::uint64_t> rejected_requests_{0};
    std::atomic<std::uint64_t> queued_requests_{0};
    std::atomic<std::uint64_t> active_blocking_tasks_{0};
    std::atomic<std::uint64_t> queued_blocking_tasks_{0};
    std::atomic<std::uint64_t> rejected_blocking_tasks_{0};
    std::atomic<std::uint64_t> active_db_operations_{0};
    std::atomic<std::uint64_t> queued_db_waiters_{0};
    std::atomic<std::uint64_t> rejected_db_waiters_{0};
    mutable std::mutex latency_mutex_;
    std::vector<double> latency_ms_;
};

class AsyncSemaphore {
    struct State {
        explicit State(std::size_t max_active, std::size_t max_waiters)
            : max_active(max_active), max_waiters(max_waiters) {}

        std::mutex mutex;
        std::size_t max_active;
        std::size_t max_waiters;
        std::size_t active{0};
        std::size_t waiters{0};

        void release() {
            std::lock_guard<std::mutex> lock(mutex);
            if (active > 0) {
                --active;
            }
        }
    };

public:
    class Permit {
    public:
        Permit() = default;
        explicit Permit(std::shared_ptr<State> state) : state_(std::move(state)) {}
        Permit(const Permit&) = delete;
        Permit& operator=(const Permit&) = delete;
        Permit(Permit&& other) noexcept : state_(std::move(other.state_)) {}
        Permit& operator=(Permit&& other) noexcept {
            if (this != &other) {
                reset();
                state_ = std::move(other.state_);
            }
            return *this;
        }
        ~Permit() { reset(); }
        explicit operator bool() const { return static_cast<bool>(state_); }
        void reset() {
            if (state_) {
                state_->release();
                state_.reset();
            }
        }
    private:
        std::shared_ptr<State> state_;
    };

    AsyncSemaphore(std::size_t max_active, std::size_t max_waiters)
        : state_(std::make_shared<State>(max_active, max_waiters)) {}

    std::optional<Permit> try_acquire() {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->active >= state_->max_active) {
            return std::nullopt;
        }
        ++state_->active;
        return Permit{state_};
    }

    net::awaitable<Permit> acquire(std::chrono::milliseconds timeout = std::chrono::seconds{30}) {
        if (auto permit = try_acquire()) {
            co_return std::move(*permit);
        }

        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (state_->waiters >= state_->max_waiters) {
                throw OverloadError("async semaphore waiter limit exceeded");
            }
            ++state_->waiters;
        }

        struct WaiterGuard {
            std::shared_ptr<State> state;
            ~WaiterGuard() {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->waiters > 0) {
                    --state->waiters;
                }
            }
        } guard{state_};

        auto executor = co_await net::this_coro::executor;
        net::steady_timer timer(executor);
        const auto deadline = Clock::now() + timeout;
        while (Clock::now() < deadline) {
            if (auto permit = try_acquire()) {
                co_return std::move(*permit);
            }
            timer.expires_after(std::chrono::milliseconds{1});
            co_await timer.async_wait(net::use_awaitable);
        }
        throw TimeoutError("async semaphore acquisition timed out");
    }

    std::size_t active() const {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->active;
    }

    std::size_t queued() const {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->waiters;
    }

private:
    std::shared_ptr<State> state_;
};

class BlockingTaskPool {
public:
    explicit BlockingTaskPool(std::size_t threads = std::max(1u, std::thread::hardware_concurrency()),
                              std::size_t max_queue = 1024,
                              std::shared_ptr<HttpMetrics> metrics = {})
        : pool_(threads), max_queue_(max_queue), metrics_(std::move(metrics)) {}

    ~BlockingTaskPool() {
        pool_.join();
    }

    template <typename F>
    net::awaitable<std::invoke_result_t<F>> submit(
        F fn,
        std::chrono::milliseconds timeout = std::chrono::seconds{30}) {
        using Result = std::invoke_result_t<F>;
        struct State {
            std::atomic<bool> done{false};
            std::exception_ptr error;
            std::conditional_t<std::is_void_v<Result>, std::monostate, std::optional<Result>> value;
        };

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queued_tasks_ >= max_queue_) {
                if (metrics_) {
                    metrics_->record_blocking_task_rejected();
                }
                throw OverloadError("blocking task queue limit exceeded");
            }
            ++queued_tasks_;
            publish_metrics_locked();
        }

        auto state = std::make_shared<State>();
        net::post(pool_, [this, state, fn = std::move(fn)]() mutable {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (queued_tasks_ > 0) {
                    --queued_tasks_;
                }
                ++active_tasks_;
                publish_metrics_locked();
            }

            try {
                if constexpr (std::is_void_v<Result>) {
                    fn();
                } else {
                    state->value = fn();
                }
            } catch (...) {
                state->error = std::current_exception();
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (active_tasks_ > 0) {
                    --active_tasks_;
                }
                publish_metrics_locked();
            }
            state->done.store(true, std::memory_order_release);
        });

        auto executor = co_await net::this_coro::executor;
        net::steady_timer poll_timer(executor);
        const auto deadline = Clock::now() + timeout;
        while (!state->done.load(std::memory_order_acquire)) {
            if (Clock::now() >= deadline) {
                throw TimeoutError("blocking task timed out");
            }
            poll_timer.expires_after(std::chrono::milliseconds{1});
            co_await poll_timer.async_wait(net::use_awaitable);
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

    std::size_t active_tasks() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_tasks_;
    }

    std::size_t queued_tasks() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queued_tasks_;
    }

private:
    void publish_metrics_locked() {
        if (metrics_) {
            metrics_->set_active_blocking_tasks(active_tasks_);
            metrics_->set_queued_blocking_tasks(queued_tasks_);
        }
    }

    net::thread_pool pool_;
    std::size_t max_queue_;
    std::shared_ptr<HttpMetrics> metrics_;
    mutable std::mutex mutex_;
    std::size_t active_tasks_{0};
    std::size_t queued_tasks_{0};
};

class AsyncDbConnection {
public:
    explicit AsyncDbConnection(AsyncSemaphore::Permit permit,
                               std::shared_ptr<HttpMetrics> metrics = {})
        : permit_(std::move(permit)), metrics_(std::move(metrics)) {
        if (metrics_) {
            metrics_->set_active_db_operations(1);
        }
    }

    AsyncDbConnection(AsyncDbConnection&&) noexcept = default;
    AsyncDbConnection& operator=(AsyncDbConnection&&) noexcept = default;
    AsyncDbConnection(const AsyncDbConnection&) = delete;
    AsyncDbConnection& operator=(const AsyncDbConnection&) = delete;

    ~AsyncDbConnection() {
        if (metrics_) {
            metrics_->set_active_db_operations(0);
        }
    }

    net::awaitable<std::string> fetch_user(std::string id,
                                           std::chrono::milliseconds artificial_delay = std::chrono::milliseconds{1}) {
        auto executor = co_await net::this_coro::executor;
        net::steady_timer timer(executor);
        timer.expires_after(artificial_delay);
        co_await timer.async_wait(net::use_awaitable);
        co_return std::string{"{\"id\":\""} + id + "\",\"name\":\"demo-user\"}";
    }

    net::awaitable<void> commit() { co_return; }
    net::awaitable<void> rollback() { co_return; }

private:
    AsyncSemaphore::Permit permit_;
    std::shared_ptr<HttpMetrics> metrics_;
};

class AsyncDbPool {
public:
    AsyncDbPool(std::size_t max_connections,
                std::size_t max_waiters,
                std::shared_ptr<HttpMetrics> metrics = {})
        : semaphore_(max_connections, max_waiters), metrics_(std::move(metrics)) {}

    net::awaitable<AsyncDbConnection> acquire(
        std::chrono::milliseconds timeout = std::chrono::seconds{3}) {
        if (metrics_) {
            metrics_->set_queued_db_waiters(semaphore_.queued());
        }
        try {
            auto permit = co_await semaphore_.acquire(timeout);
            if (metrics_) {
                metrics_->set_queued_db_waiters(semaphore_.queued());
                metrics_->set_active_db_operations(semaphore_.active());
            }
            co_return AsyncDbConnection{std::move(permit), metrics_};
        } catch (const OverloadError&) {
            if (metrics_) {
                metrics_->record_db_waiter_rejected();
            }
            throw;
        }
    }

    std::size_t active() const { return semaphore_.active(); }
    std::size_t queued() const { return semaphore_.queued(); }

private:
    AsyncSemaphore semaphore_;
    std::shared_ptr<HttpMetrics> metrics_;
};

} // namespace qornix::async
