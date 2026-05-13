# Async Roadmap Changelog

## 2026-05-13 - Sprints 6-12: Timeout, backpressure, offload pool, async middleware, observability и load testing

Выполнено по Sprint 6:

- Добавлен `HttpServerOptions` с настройками:
  - `route_timeout`;
  - `read_timeout`;
  - `write_timeout`;
  - `graceful_shutdown_timeout`;
  - `max_active_requests`;
  - `max_queued_requests`;
  - `max_request_body_size`;
  - `structured_access_log`.
- Добавлены per-route настройки через `RouteOptions`:
  - `timeout`;
  - `max_concurrent_requests`;
  - `max_request_body_size`.
- Добавлены runtime setters:
  - `set_global_route_timeout(...)`;
  - `set_read_timeout(...)`;
  - `set_write_timeout(...)`;
  - `set_route_timeout(...)`;
  - `set_route_concurrency_limit(...)`;
  - `set_route_body_limit(...)`.
- `Session` получила read/write/request timers.
- Async route timeout возвращает `504 Gateway Timeout`.
- Late coroutine response после timeout/disconnect подавляется через generation check и cancellation flag.
- Введен `RequestContext` с request id, deadline, route pattern, state и cancellation flag.

Выполнено по Sprint 7:

- Добавлен global active request limit.
- Добавлен per-route concurrent request limit.
- Добавлен `qornix::async::AsyncSemaphore` с RAII `Permit`, max active и max waiters.
- Добавлен max request body size на global и per-route уровнях.
- Overload behavior:
  - global limit -> `503 Service Unavailable`;
  - route limit -> `429 Too Many Requests`;
  - body limit -> `413 Payload Too Large`.
- Метрики учитывают rejected, timed out, cancelled и active requests.

Выполнено по Sprint 8:

- Добавлен header-only `qornix::async::BlockingTaskPool`.
- Pool исполняет blocking callbacks в отдельном `boost::asio::thread_pool`.
- Добавлены ограничения thread pool queue и timeout ожидания результата.
- Добавлена публикация metrics:
  - active blocking tasks;
  - queued blocking tasks;
  - rejected blocking tasks.
- README дополнен примером безопасного offload для blocking DB/API вызовов.

Выполнено по Sprint 9:

- Добавлен thin awaitable `qornix::async::AsyncDbPool` и `AsyncDbConnection` как migration facade для настоящего async DB layer.
- Pool ограничивает max connections и max waiters через `AsyncSemaphore`.
- `acquire()` поддерживает timeout.
- Mock `fetch_user(...)` показывает awaitable DB-style API без блокировки `io_context` thread.
- README описывает назначение facade и переход к реальному Asio-compatible PostgreSQL/MySQL client.

Выполнено по Sprint 10:

- Добавлен `AsyncMiddleware`:
  - `net::awaitable<std::optional<Response>>(RequestContext&)`.
- Добавлен `HttpServer::add_async_middleware(...)`.
- Middleware может:
  - вернуть `std::nullopt` и продолжить pipeline;
  - вернуть `Response` и остановить pipeline.
- Sync middleware API сохранен без breaking changes.
- Exceptions из async middleware переводятся в 500/503/504 в зависимости от типа ошибки.
- Pipeline timeout распространяется на async middleware и route handler.

Выполнено по Sprint 11:

- Добавлен `qornix::async::HttpMetrics` и snapshot/json/prometheus форматирование.
- Добавлен `HttpServer::metrics()` и `metrics_ptr()`.
- Добавлен `HttpServer::add_metrics_route()`; default endpoint `/qornix/metrics`.
- Метрики включают:
  - total/active/completed/failed requests;
  - timed out/cancelled/rejected requests;
  - latency p50/p95/p99;
  - blocking pool queue;
  - DB active/queued/rejected counters.
- Добавлен structured access log с request id, route, status, duration и cancellation reason.
- Добавлен graceful shutdown:
  - stop accepting new connections;
  - wait for active requests until deadline;
  - stop `io_context` after deadline.

Выполнено по Sprint 12:

- `baseline_benchmark_server` расширен endpoint'ами:
  - `/sleep` и `/sleep/{ms}` для async sleep;
  - `/users/{id}` для DB-pool-limited сценария;
  - `/blocking/{ms}` для blocking offload scenario;
  - `/qornix/metrics` для observability checks.
- `scripts/baseline_benchmark.py` расширен:
  - `--extended-load` включает дополнительные high-concurrency сценарии;
  - `--load-concurrency` задает уровни concurrency, default `1000 5000 10000`;
  - `--db-normal-concurrency` задает normal DB pool concurrency, default `128`;
  - `--db-concurrency` задает DB pool overload HTTP concurrency, default `10000`;
  - `--output` задает путь markdown-отчета, default `doc/benchmark.md`;
  - отчет теперь включает CPU %, статус-классы `2xx/4xx/5xx`, client-side errors и per-scenario delta для rejected/timeout metrics.
- Доработка после контрольного прогона:
  - `/bench/delay/10` и `/bench/delay/100` переведены на async timer, чтобы benchmark не измерял blocking sleep на worker threads;
  - DB scenario разделен на `db_pool_normal` и `db_pool_overload`;
  - cumulative counters в отчете заменены на per-scenario delta, чтобы overload/timeout сценарии не загрязняли следующие строки отчета.
- Добавлен пользовательский `changelog.md` с кратким описанием async-фич.
- Пользовательская документация очищена от sprint-терминологии; sprint-детали оставлены только в `doc/project_doc`.
- Smoke-test расширен проверками:
  - route timeout -> 504;
  - per-route body limit -> 413;
  - async middleware stop/pass;
  - blocking offload pool;
  - async DB facade;
  - per-route concurrency limit -> 429;
  - metrics endpoint and counters.

Контрольный benchmark после доработок:

- Команда: `python3 scripts/baseline_benchmark.py --server cmake-build-debug/baseline_benchmark_server --port 18080 --server-threads 32 --duration 10 --concurrency 256 --extended-load --load-concurrency 1000 5000 10000 --db-normal-concurrency 128 --db-concurrency 10000 --idle-connections 10000`.
- Fast endpoint: `8799.87` RPS at 256 concurrency, p50 `30.26 ms`, p95 `40.49 ms`, errors `0`.
- Async delay 10 ms: `8373.80` RPS, p50 `30.43 ms`, p95 `37.14 ms`, errors `0`.
- Async delay 100 ms: `2465.51` RPS, p50 `102.81 ms`, p95 `104.57 ms`, errors `0`; результат соответствует async timer model (`256 / 0.1s ~= 2560 RPS`).
- Fast 10k active load: `6963.31` RPS, p50 `1463.45 ms`, errors `0`.
- Async sleep 10k active load: `3970.39` RPS, p50 `2065.51 ms`, errors `0`.
- DB normal at 128 concurrency: `6430.50` RPS, p50 `17.13 ms`, p95 `33.23 ms`, errors `0`.
- DB overload at 10k concurrency: controlled overload with `33475` DB rejects and `42545` timeouts; сценарий считается защитным stress test, а не normal operation.
- Timeout guard: `/sleep?ms=3000` returns `1280` controlled timeout responses, p50 `2005.98 ms`, `timeout delta = 1280`.
- 10k idle keep-alive: opened `10000/10000`, failed `0`, RSS about `75 MB`, threads `65`, fd `10007`.
- Результат сохранен в пользовательском отчете `doc/benchmark.md`.

Проверка:

- `g++ -std=c++20 -Iinclude -c server/http_server.cpp -o /tmp/http_server.o` - ok.
- `g++ -std=c++20 -Iinclude -fsyntax-only tests/http_server_smoke_test.cpp` - ok.
- `g++ -std=c++20 -Iinclude -fsyntax-only tests/baseline_benchmark_server.cpp` - ok.
- `python3 -m py_compile scripts/baseline_benchmark.py` - ok.

Ограничение проверки в текущем окружении:

- Полный CMake build не запускался до конца, потому что в контейнере отсутствует `yaml-cpp`; root `CMakeLists.txt` останавливает configure с `yaml-cpp not found. Install with: apt-get install libyaml-cpp-dev`.

Не делалось:

- Реальный PostgreSQL/MySQL async driver не подключался; добавлен совместимый facade и DB mock для migration path.
- Жесткая принудительная остановка уже выполняющейся user coroutine не делалась; модель cancellation cooperative, late response suppression уже включен.
- Полный 10k benchmark не прогонялся в контейнере из-за отсутствующего `yaml-cpp` и системных лимитов окружения; контрольный 10k benchmark выполнен пользователем локально и зафиксирован выше.

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
