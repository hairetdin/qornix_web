#include "dynamic_query_service.h"
#include "async_database_interface.h"
#include "db/mock_async_driver.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/json.hpp>

#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace net = boost::asio;
using namespace std::chrono_literals;

namespace {

template <typename T>
T run_async(net::io_context& ioc, net::awaitable<T> operation) {
    auto future = net::co_spawn(ioc, std::move(operation), net::use_future);
    ioc.run();
    ioc.restart();
    return future.get();
}

qornix_dynamic_api::DynamicSchemaAllowlist make_allowlist() {
    qornix_dynamic_api::DynamicSchemaAllowlist allowlist;

    qornix_dynamic_api::DynamicTablePolicy users;
    users.name = "users";
    users.entityName = "User";

    qornix_dynamic_api::DynamicFieldPolicy id;
    id.name = "id";
    id.type = "integer";
    id.readable = true;
    id.writable = false;
    id.filterable = true;
    id.sortable = true;
    users.fields[id.name] = id;

    qornix_dynamic_api::DynamicFieldPolicy name;
    name.name = "name";
    name.type = "string";
    name.readable = true;
    name.writable = true;
    name.filterable = true;
    name.sortable = true;
    users.fields[name.name] = name;

    allowlist.addTable(std::move(users));
    return allowlist;
}

std::shared_ptr<AsyncDatabaseInterface> make_async_db(
    net::io_context& ioc,
    std::string driver_name,
    std::shared_ptr<qornix::db::MockAsyncDriverStats> stats) {
    qornix::db::AsyncPoolOptions options;
    options.pool_name = "dynamic-test";
    options.driver_name = std::move(driver_name);
    options.min_connections = 0;
    options.max_connections = 2;
    options.max_waiters = 8;
    options.query_timeout = 200ms;
    return std::make_shared<AsyncDatabaseInterface>(
        ioc.get_executor(),
        qornix::db::make_mock_async_driver_factory(1ms, std::move(stats)),
        options);
}

void test_async_get_uses_async_query_builder() {
    net::io_context ioc;
    auto stats = std::make_shared<qornix::db::MockAsyncDriverStats>();
    auto db = make_async_db(ioc, "postgresql_async", stats);

    qornix_dynamic_api::DynamicApiConfig config;
    config.defaultLimit = 10;
    config.maxLimit = 25;
    config.dynamicQueryTimeout = 200ms;
    config.preparedDynamicQueries = true;

    qornix_dynamic_api::DynamicQueryService service(config, db, make_allowlist());

    qornix_dynamic_api::DynamicQueryRequest request;
    request.method = "GET";
    request.table = "users";
    request.rawBody = R"({"query":{"fields":["id","name"],"where":[{"field":"id","op":"eq","value":"42"}],"order_by":["id ASC"],"limit":5}})";

    auto response = run_async(ioc, service.executeAsync(request));
    assert(response.status == boost::beast::http::status::ok);
    assert(stats->queries.load() == 1);
    assert(stats->last_sql().find("SELECT id, name FROM users WHERE id=$1 ORDER BY id ASC LIMIT 5") != std::string::npos);
    assert(stats->last_query_param_count == 1);

    const auto& body = response.body.as_object();
    assert(body.at("ok").as_bool());
    assert(body.at("count").as_int64() == 1);
    const auto& row = body.at("data").as_array().front().as_object();
    assert(std::string(row.at("driver").as_string().c_str()) == "mock_async");
    assert(std::string(row.at("param_1").as_string().c_str()) == "42");
    assert(std::string(row.at("prepared").as_string().c_str()) == "true");
}

void test_async_mysql_post_omits_returning() {
    net::io_context ioc;
    auto stats = std::make_shared<qornix::db::MockAsyncDriverStats>();
    auto db = make_async_db(ioc, "mysql_async", stats);

    qornix_dynamic_api::DynamicApiConfig config;
    config.dynamicQueryTimeout = 200ms;
    config.preparedDynamicQueries = false;

    qornix_dynamic_api::DynamicQueryService service(config, db, make_allowlist());

    qornix_dynamic_api::DynamicQueryRequest request;
    request.method = "POST";
    request.table = "users";
    request.rawBody = R"({"data":{"name":"Ada"}})";

    auto response = run_async(ioc, service.executeAsync(request));
    assert(response.status == boost::beast::http::status::created);
    assert(stats->queries.load() == 1);
    assert(stats->last_sql().find("INSERT INTO users (name) VALUES (?)") != std::string::npos);
    assert(stats->last_sql().find("RETURNING") == std::string::npos);
    assert(stats->last_query_param_count == 1);

    const auto& body = response.body.as_object();
    assert(body.at("ok").as_bool());
    assert(body.at("count").as_int64() == 1);
}

void test_async_validation_still_blocks_disallowed_fields() {
    net::io_context ioc;
    auto stats = std::make_shared<qornix::db::MockAsyncDriverStats>();
    auto db = make_async_db(ioc, "postgresql_async", stats);

    qornix_dynamic_api::DynamicApiConfig config;
    qornix_dynamic_api::DynamicQueryService service(config, db, make_allowlist());

    qornix_dynamic_api::DynamicQueryRequest request;
    request.method = "PATCH";
    request.table = "users";
    request.id = "7";
    request.rawBody = R"({"data":{"id":"8"}})";

    auto response = run_async(ioc, service.executeAsync(request));
    assert(response.status == boost::beast::http::status::bad_request);
    assert(stats->queries.load() == 0);
    const auto& body = response.body.as_object();
    assert(std::string(body.at("error").as_string().c_str()) == "field_not_writable");
}

} // namespace

int main() {
#if QORNIX_ENABLE_ASYNC_DB
    test_async_get_uses_async_query_builder();
    test_async_mysql_post_omits_returning();
    test_async_validation_still_blocks_disallowed_fields();
    std::cout << "dynamic_async_query_service_test passed" << std::endl;
    return 0;
#else
    std::cout << "dynamic_async_query_service_test skipped: QORNIX_ENABLE_ASYNC_DB=0" << std::endl;
    return 0;
#endif
}
