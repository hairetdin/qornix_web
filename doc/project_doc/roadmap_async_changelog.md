# Async Roadmap Changelog

## 2026-05-13 - Sprint 5: Полный async HTTP API и examples

Выполнено:

- Добавлены async route helpers:
  - `post_async`;
  - `put_async`;
  - `patch_async`;
  - `delete_async`;
  - `head_async`;
  - `options_async`;
  - `any_async`.
- `add_async_route` теперь использует `any_async` semantics без method filter.
- Внутри `HttpServer` добавлен общий `add_async_route_for_method(...)` для регистрации async routes с optional `http::verb`.
- Добавлены ergonomic response helpers в namespace `response`:
  - `response::text`;
  - `response::json`;
  - `response::status`;
  - `response::redirect`.
- Существующий `Response` оставлен Beast alias для backward compatibility; helper namespace выбран вместо breaking wrapper-типа `Response::text`.
- Добавлен runnable example `example/async_routes_server`:
  - sync `/health`;
  - async `/sleep/{ms}`;
  - async JSON `/api/status`;
  - async POST `/api/echo`;
  - mixed any-method `/api/method`.
- Root examples build подключает `example/async_routes_server`.
- README обновлен:
  - когда использовать sync routes;
  - когда использовать async routes;
  - почему blocking code внутри async handler опасен;
  - lifetime rules для `Request`, `Url`, `Params` после `co_await`;
  - пример сборки и запуска `async_routes_server`.
- Smoke-test расширен:
  - async POST/PUT/PATCH/DELETE/HEAD/OPTIONS;
  - `any_async`;
  - `response::text/json/status/redirect`;
  - OPTIONS `Allow` header.

Проверка:

- `cmake -S . -B build/async_sprint5 -DCMAKE_BUILD_TYPE=Release -DQORNIX_BUILD_EXAMPLES=ON -DQORNIX_BUILD_TESTS=ON -DQORNIX_BUILD_RAG=OFF` - ok.
- `cmake --build build/async_sprint5 --parallel` - ok.
- `ctest --test-dir build/async_sprint5 --output-on-failure` - `19/19` passed.
- `cmake -S route_extensions -B build/async_sprint5_route_extensions -DCMAKE_BUILD_TYPE=Release && cmake --build build/async_sprint5_route_extensions --parallel` - ok.
- `cmake -S . -B build/async_sprint5_rag -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_EXAMPLES=OFF -DQORNIX_BUILD_TESTS=OFF -DQORNIX_BUILD_APP=OFF -DQORNIX_ENABLE_ORM=OFF && cmake --build build/async_sprint5_rag --parallel` - ok. ONNX Runtime не найден, RAG собран с TF-IDF fallback.
- Manual example smoke:
  - `build/async_sprint5/example/async_routes_server/async_routes_server 18091 1` - started;
  - `curl -s http://127.0.0.1:18091/sleep/10` -> `slept:10`;
  - `curl -s http://127.0.0.1:18091/api/status` -> `{"status":"ok","mode":"async"}`;
  - `curl -s -X POST http://127.0.0.1:18091/api/echo -d hello` -> `{"echo":"hello"}`.

Не делалось:

- Timeout, cancellation и backpressure не добавлялись.
- Async middleware API не добавлялся.
- Literal `Response::text` static methods не добавлялись, чтобы не заменять публичный `Response` alias wrapper-типом.

## 2026-05-13 - Sprint 4: Первый async route API

Выполнено:

- Добавлены публичные типы:
  - `Url = urls::url`;
  - `SyncRouteHandler`;
  - `AsyncRouteHandler = std::function<net::awaitable<Response>(Request, Url, Params)>`.
- Route storage переведен на единый `RouteEntry` с `std::variant<SyncRouteHandler, AsyncRouteHandler>`.
- Порядок регистрации sync/async routes сохранен в одной таблице маршрутов.
- Добавлены методы:
  - `HttpServer::add_async_route(pattern, handler)`;
  - `HttpServer::get_async(pattern, handler)`.
- Добавлен convenience overload `make_text_response(body, version = 11)` для простых `200 OK` text responses.
- `get_async` регистрирует GET-only async route через optional method filter.
- `Session::handle_request()` теперь управляет отправкой ответа:
  - sync route продолжает возвращать response через `send_response`;
  - async route запускается и отправляет response после завершения coroutine.
- Добавлен `Session::start_async_route_handler()`:
  - запускает handler через `net::co_spawn(strand_, ...)`;
  - передает `Request`, owning `Url` и `Params` по значению;
  - ловит exceptions из coroutine и переводит их в `500`.
- Async coroutine выполняется на session strand, а итоговый response отправляется через существующий `send_response`.
- Smoke-test расширен:
  - `/async/sleep` проверяет базовый async route;
  - `/async/items/{id}?source=test` проверяет безопасность `Request`, `Url`, `Params` после `co_await`;
  - `/async/boom` проверяет exception -> `500`;
  - отдельный single-thread server проверяет, что pending async timer не блокирует другой connection.

Проверка:

- `cmake -S . -B build/async_sprint4 -DCMAKE_BUILD_TYPE=Release -DQORNIX_BUILD_EXAMPLES=ON -DQORNIX_BUILD_TESTS=ON -DQORNIX_BUILD_RAG=OFF` - ok.
- `cmake --build build/async_sprint4 --parallel` - ok.
- `ctest --test-dir build/async_sprint4 --output-on-failure` - `19/19` passed.
- `cmake -S route_extensions -B build/async_sprint4_route_extensions -DCMAKE_BUILD_TYPE=Release && cmake --build build/async_sprint4_route_extensions --parallel` - ok.
- `cmake -S . -B build/async_sprint4_rag -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_EXAMPLES=OFF -DQORNIX_BUILD_TESTS=OFF -DQORNIX_BUILD_APP=OFF -DQORNIX_ENABLE_ORM=OFF && cmake --build build/async_sprint4_rag --parallel` - ok. ONNX Runtime не найден, RAG собран с TF-IDF fallback.

Не делалось:

- Полные async verb helpers (`post_async`, `put_async`, etc.) не добавлялись.
- Timeout, cancellation и backpressure не добавлялись.
- Middleware API остался sync; async-aware middleware continuation/post-processing не менялись.

## 2026-05-13 - Sprint 3: Session strand

Выполнено:

- Добавлен alias `Strand = boost::asio::strand<boost::asio::io_context::executor_type>`.
- `Session` получила собственный `strand_`, инициализированный от executor `io_context` принятого socket.
- `Session::run()` стартует первый `do_read()` через `net::dispatch(strand_, ...)`.
- Completion callbacks `http::async_read` и `http::async_write` привязаны через `net::bind_executor(strand_, ...)`.
- Lifecycle `shared_from_this()` сохранен: `self` удерживается для dispatch, read и write callbacks.
- `http_server_smoke_test` теперь запускает серверный `io_context` в 4 потоках.
- Добавлен keep-alive regression test на 24 последовательных запроса по одному TCP connection.

Проверка:

- `cmake -S . -B build/async_sprint3 -DCMAKE_BUILD_TYPE=Release -DQORNIX_BUILD_EXAMPLES=ON -DQORNIX_BUILD_TESTS=ON -DQORNIX_BUILD_RAG=OFF` - ok.
- `cmake --build build/async_sprint3 --parallel` - ok.
- `ctest --test-dir build/async_sprint3 --output-on-failure` - `19/19` passed.

Не делалось:

- `get_async`, async route storage и `co_spawn` не добавлялись.
- Timeout, cancellation и backpressure не менялись.

## 2026-05-12 - Sprint 2: Response pipeline refactor

Выполнено:

- Добавлен публичный header `include/http_response.h` с aliases:
  - `Request = http::request<http::string_body>`;
  - `Response = http::response<http::string_body>`;
  - `Params = std::map<std::string, std::string>`.
- Добавлены response helpers:
  - `make_response`;
  - `make_text_response`;
  - `make_json_response`;
  - `make_html_response`;
  - `make_error_response`.
- `RouteHandler`, `Middleware` и `Session` переведены на aliases без изменения sync API.
- В `Session` выделен общий pipeline:
  - `handle_request()`;
  - `execute_route_handler()`;
  - `execute_middlewares()`;
  - `send_response(Response res)`.
- Все успешные sync route responses, 404, 400 и 500 теперь проходят через `send_response`.
- 404 централизован через `make_html_response(... TemplateLoader::load404Template())`.
- 400 для invalid URL теперь возвращается клиенту как response, а не закрывает socket без ответа.
- Exceptions из sync handlers/middleware переводятся в централизованный `500` через `make_error_response`.
- `HandlerBase` использует новые helpers для default method responses и JSON error responses.
- Smoke-test расширен:
  - invalid URL -> `400`;
  - throwing sync route -> `500`;
  - middleware chain сохраняет возможность модифицировать response после `next()`;
  - keep-alive regression coverage оставлен.

Проверка:

- `cmake -S . -B build/async_sprint2 -DCMAKE_BUILD_TYPE=Release -DQORNIX_BUILD_EXAMPLES=ON -DQORNIX_BUILD_TESTS=ON -DQORNIX_BUILD_RAG=OFF` - ok.
- `cmake --build build/async_sprint2 --parallel` - ok.
- `ctest --test-dir build/async_sprint2 --output-on-failure` - `19/19` passed.
- `cmake --build build/async_sprint1_rag --parallel` - ok.
- `cmake --build build/async_sprint1_auth --clean-first --parallel` - ok.
- `cmake --build build/async_sprint1_route_extensions --parallel` - ok.

Benchmark sprint 2:

- Команда: `scripts/baseline_benchmark.py --server build/async_sprint2/baseline_benchmark_server --duration 3 --concurrency 64 --idle-connections 10000 --idle-hold 3 --idle-batch-size 500`.
- Fast endpoint: `84873.71` RPS, p50 `0.72 ms`, p95 `0.95 ms`, p99 `1.18 ms`.
- Delay 10 ms: `3170.21` RPS, p95 `20.40 ms`.
- Delay 100 ms: `319.29` RPS, p95 `200.60 ms`.
- Fast 1k concurrent: `78898.87` RPS, p50 `13.64 ms`, p95 `16.55 ms`, p99 `18.04 ms`.
- 10k idle keep-alive: opened `10000/10000`, failed `0`, RSS `28272 KB`, fd `10007`.

Не делалось:

- `get_async`, `co_spawn`, timeout/cancellation и async handler storage не добавлялись.
- Session lifecycle не менялся за пределами response pipeline.

## 2026-05-12 - Sprint 1: Переход на C++20

Выполнено:

- Root `CMakeLists.txt` переведен на `cmake_minimum_required(VERSION 3.20)`, `CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`.
- `qornix_web_core` переведен с `cxx_std_17` на `cxx_std_20`.
- Модули `qornix_orm`, `qornix_auth`, `qornix_dynamic_api`, `qornix_rag` переведены на C++20.
- Examples и `route_extensions` переведены на C++20, чтобы root examples не оставались на C++17.
- Шаблоны `templates/app/CMakeLists.txt.in` и `templates/dynamic_api_app/CMakeLists.txt.in` теперь создают C++20-проекты.
- Документация и отображаемые строки с `C++17` обновлены на `C++20`.
- Убраны build warnings, проявлявшиеся при sprint 1 проверках:
  - OpenSSL SHA256 deprecated API заменен на EVP API в `qornix_auth/include/password_hasher.h`;
  - duplicate `QORNIX_ORM_SCHEMA_APP_XSD_PATH` removed from `example/dynamic_web_query_builder_server`;
  - unused-parameter warnings в RAG build погашены без изменения поведения.

Проверка:

- `cmake -S . -B build/async_sprint1 -DCMAKE_BUILD_TYPE=Release -DQORNIX_BUILD_EXAMPLES=ON -DQORNIX_BUILD_TESTS=ON -DQORNIX_BUILD_RAG=OFF` - ok.
- `cmake --build build/async_sprint1 --parallel` - ok.
- `ctest --test-dir build/async_sprint1 --output-on-failure` - `19/19` passed.
- `cmake -S qornix_auth -B build/async_sprint1_auth -DCMAKE_BUILD_TYPE=Release && cmake --build build/async_sprint1_auth --parallel` - ok.
- `cmake -S . -B build/async_sprint1_rag -DQORNIX_BUILD_RAG=ON -DQORNIX_BUILD_EXAMPLES=OFF -DQORNIX_BUILD_TESTS=OFF -DQORNIX_BUILD_APP=OFF -DQORNIX_ENABLE_ORM=OFF && cmake --build build/async_sprint1_rag --parallel` - ok. ONNX Runtime не найден, RAG собран с TF-IDF fallback.
- `cmake -S route_extensions -B build/async_sprint1_route_extensions -DCMAKE_BUILD_TYPE=Release && cmake --build build/async_sprint1_route_extensions --parallel` - ok.

Benchmark sprint 1:

- Команда: `scripts/baseline_benchmark.py --server build/async_sprint1/baseline_benchmark_server --duration 3 --concurrency 64 --idle-connections 10000 --idle-hold 3 --idle-batch-size 500`.
- Fast endpoint: `84708.27` RPS, p50 `0.72 ms`, p95 `0.93 ms`, p99 `1.20 ms`.
- Fast 1k concurrent: `85980.04` RPS, p50 `11.42 ms`, p95 `15.27 ms`, p99 `18.60 ms`.
- 10k idle keep-alive: opened `10000/10000`, failed `0`, RSS `28368 KB`, fd `10007`.
- По сравнению со sprint 0 fast endpoint просел примерно на `4.6%`, а fast 1k concurrent слегка вырос; заметной просадки на быстрых endpoint'ах не зафиксировано.

Не делалось:

- Async handlers, `get_async`, coroutine route API и Session lifecycle не менялись.

## 2026-05-12 - Sprint 0: Baseline и аудит

Выполнено:

- Добавлен CTest smoke-test `http_server_smoke_test` для `/health`, обычного route, route с path params, `404` и keep-alive на одном TCP connection.
- Добавлен benchmark server `baseline_benchmark_server` с endpoint'ами `/bench/fast`, `/bench/delay/10`, `/bench/delay/100`.
- Добавлен `scripts/baseline_benchmark.py` без внешних Python-зависимостей; собирает RPS, p50/p95/p99 latency, RSS, threads и open fd.
- Existing `dynamic_openapi_service_test` подключен к CTest, чтобы baseline проверял `qornix_dynamic_api`.
- Создан baseline report: `doc/project_doc/baseline_async_sprint0_report.md`.

Проверка:

- `cmake -S . -B build/async_baseline -DCMAKE_BUILD_TYPE=Release -DQORNIX_BUILD_EXAMPLES=ON -DQORNIX_BUILD_TESTS=ON -DQORNIX_BUILD_RAG=OFF` - ok.
- `cmake --build build/async_baseline --parallel` - ok.
- `ctest --test-dir build/async_baseline --output-on-failure` - `19/19` passed.
- `cmake -S qornix_auth -B build/async_baseline_auth -DCMAKE_BUILD_TYPE=Release && cmake --build build/async_baseline_auth --parallel` - ok.
- Benchmark baseline: fast endpoint ~88793 RPS at 64 concurrent; 1k concurrent fast endpoint ~84528 RPS; 10k idle keep-alive opened 10000/10000 connections.

Зафиксированные baseline warnings:

- OpenSSL 3.0 deprecation warnings в `qornix_auth/include/password_hasher.h` для SHA256 API.
- Повторное определение `QORNIX_ORM_SCHEMA_APP_XSD_PATH` в части ORM/example build targets.

Не делалось:

- Runtime-логика `HttpServer` не менялась.
- `qornix_rag` не собирался, так как root build держит его опциональным (`QORNIX_BUILD_RAG=OFF` по умолчанию), и он не входит в обязательный baseline.
