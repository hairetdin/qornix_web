#pragma once

#include "http_server.h"

#include <boost/asio/steady_timer.hpp>
#include <boost/json/object.hpp>
#include <boost/json/serialize.hpp>

#include <algorithm>
#include <chrono>
#include <string>

inline AsyncRouteHandler makeAsyncPingHandler() {
    return [](Request req, Url, Params) -> net::awaitable<Response> {
        boost::json::object payload;
        payload["status"] = "ok";
        payload["mode"] = "async";
        co_return response::json(boost::json::serialize(payload), req.version());
    };
}

inline AsyncRouteHandler makeAsyncSleepHandler() {
    return [](Request req, Url, Params params) -> net::awaitable<Response> {
        int requested_ms = 25;
        try {
            const auto it = params.find("ms");
            if (it != params.end()) {
                requested_ms = std::stoi(it->second);
            }
        } catch (const std::exception&) {
            co_return response::text(http::status::bad_request,
                                  "invalid sleep duration",
                                  req.version());
        }

        const auto delay_ms = std::clamp(requested_ms, 0, 2000);
        auto executor = co_await net::this_coro::executor;
        net::steady_timer timer(executor);
        timer.expires_after(std::chrono::milliseconds{delay_ms});
        co_await timer.async_wait(net::use_awaitable);

        co_return response::text("slept:" + std::to_string(delay_ms), req.version());
    };
}
