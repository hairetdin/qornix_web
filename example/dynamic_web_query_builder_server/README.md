# Dynamic Web Query Builder Server

`dynamic_web_query_builder_server` - пример приложения на Qornix Web Server, который показывает, как поднять универсальный веб-интерфейс для динамических запросов к базе данных.

Пример дает:

- главную страницу с интерактивными API-примерами;
- страницу `/query-builder` для сборки запросов из формы;
- страницу `/table` для браузеринга таблиц с пагинацией;
- страницу `/table/{table}/{id}` для просмотра и редактирования отдельных записей с inline-редактированием полей;
- страницу `/schema-manager` для экспорта XML-схемы из БД и применения XML-схемы к БД;
- универсальный endpoint `/api/dynamic` для SELECT/INSERT/UPDATE/DELETE через `QueryBuilder`;
- metadata endpoint-ы для списка таблиц, полей и универсального autocomplete;
- endpoint `/api/dynamic/schema.xml` для экспорта XML-схемы текущей demo-базы;
- endpoint `/api/dynamic/schema/apply` для применения XML-схемы к demo-базе;
- автоматически создаваемую SQLite demo-базу с таблицами `categories`, `products`, `customers`, `orders`;
- Python performance test suite для проверки производительности API.

## Что Делает Сервер

При запуске сервер:

1. Создает или открывает SQLite базу `dynamic_web_query_builder_demo.sqlite3`.
2. Создает demo-схему, если ее еще нет.
3. Заполняет demo-данные через `INSERT OR IGNORE`.
4. Регистрирует HTML-страницы (`home`, `query-builder`, `table`, `row_view`, `schema-manager`), static-файлы и API routes.
5. Позволяет сгенерировать XML-схему текущей demo-базы через `/api/dynamic/schema.xml`.
6. Позволяет применить XML-схему к demo-базе через `/api/dynamic/schema/apply`.
7. При `QORNIX_ENABLE_ASYNC_DB=ON` регистрирует CRUD routes через async Dynamic API path; для demo SQLite используется явный `sqlite_sync_offloaded` adapter, а PostgreSQL/MySQL могут использовать real async drivers при включенных backend flags.
8. Запускает HTTP server на настройках фреймворка, по умолчанию `127.0.0.1:8008`.

Demo-база создается в каталоге примера:

```text
example/dynamic_web_query_builder_server/dynamic_web_query_builder_demo.sqlite3
```

Файл базы не коммитится. Он игнорируется локальным `.gitignore`.

XML-схема demo-базы генерируется по запросу и сохраняется рядом с базой:

```text
example/dynamic_web_query_builder_server/dynamic_web_query_builder_demo.schema.xml
```

Файл XML-схемы также не коммитится.

## Основные URL

- `/` - домашняя страница с API-примерами.
- `/query-builder` - интерактивный Query Builder.
- `/table` - браузер таблиц с пагинацией и autocomplete.
- `/table/{table}/{id}` - просмотр и редактирование отдельной записи.
- `/schema-manager` - управление XML-схемой.
- `/static/{filename}` - static-файлы из `templates`.
- `/api/dynamic` - динамические запросы, где таблица и метод передаются в JSON.
- `/api/dynamic/{table}` - динамические запросы с таблицей в path.
- `/api/dynamic/{table}/{id}` - динамические запросы с таблицей и id в path.
- `/api/dynamic/meta/tables` - список таблиц.
- `/api/dynamic/meta/{table}/fields` - список полей таблицы.
- `/api/dynamic/meta/autocomplete` - универсальный autocomplete.
- `/api/dynamic/schema.xml` - экспорт XML-схемы текущей demo SQLite базы.
- `/api/dynamic/schema/apply` - применение XML-схемы к текущей demo SQLite базе.

## Demo-Схема

Автоматически создаются таблицы:

- `categories` - категории товаров.
- `products` - товары, привязанные к категориям.
- `customers` - покупатели.
- `orders` - заказы, привязанные к покупателям и товарам.

Связи:

```text
products.category_id -> categories.id
orders.customer_id   -> customers.id
orders.product_id    -> products.id
```

## Формат Запросов

Основной формат для UI - JSON body:

```json
{
  "method": "GET",
  "table": "orders",
  "query": {
    "fields": [
      "orders.id",
      "orders.order_date",
      "products.name",
      "customers.city",
      "orders.quantity",
      "orders.status",
      "orders.total_amount"
    ],
    "join": [
      "JOIN products ON orders.product_id=products.id",
      "JOIN customers ON orders.customer_id=customers.id"
    ],
    "filter": ["orders.status='paid'"],
    "order_by": ["orders.order_date DESC"],
    "limit": 10
  }
}
```

HTTP transport на домашней странице использует `POST /api/dynamic/`, а реальная операция задается полем `"method"`. Поэтому JSON с `"method": "GET"` выполняется как SELECT через `QueryBuilder`.

URL DSL вида `?query=(method:GET;table:...)` также поддерживается backend-ом, но для сложных JOIN/фильтров в UI используется JSON, чтобы не зависеть от URL encoding.

## Запуск

Из корня репозитория:

```bash
cmake --build build --target dynamic_web_query_builder_server
./build/example/dynamic_web_query_builder_server/dynamic_web_query_builder_server
```

После запуска открыть:

```text
http://localhost:8008/
http://localhost:8008/query-builder
http://localhost:8008/table
http://localhost:8008/schema-manager
```

Если build-директория еще не создана:

```bash
cmake -S . -B build
cmake --build build --target dynamic_web_query_builder_server
```

## Важные Файлы

- `main.cpp` - точка входа примера.
- `routes.h` - регистрация routes.
- `handlers.h` - HTML/API handlers.
- `demo_database.h` - создание SQLite demo-базы, схемы и seed-данных.
- `example_paths.h` - вычисление путей к templates и demo-базе.
- `config.yaml` - YAML конфигурация demo SQLite подключения.
- `schema_roundtrip_test.cpp` - C++ тест round-trip генерации XML-схемы.
- `templates/home.html` - домашняя страница с API-примерами.
- `templates/query_builder.html` - интерактивный Query Builder.
- `templates/table.html` - браузер таблиц с пагинацией.
- `templates/row_view.html` - просмотр и редактирование отдельной записи.
- `templates/schema_manager.html` - управление XML-схемой.
- `templates/app.js` - JS для домашней страницы.
- `templates/schema_manager.js` - JS для страницы управления схемой.
- `templates/styles.css` - общие стили.
- `perf_test/` - Python performance test suite.
- `CMakeLists.txt` - сборка standalone target примера.

## Как Был Создан Этот Пример

1. Создан каталог примера:

```text
example/dynamic_web_query_builder_server
```

2. Добавлена точка входа `main.cpp`.

В ней создается `ServerManager`, затем регистрируется пользовательская функция маршрутов:

```cpp
auto server_manager = create_server_manager(argc, argv);
server_manager->addRouteFunction(setupDynamicRoutes);
server_manager->run();
```

3. Добавлен `routes.h`.

В `setupDynamicRoutes(HttpServer& server)` зарегистрированы:

- `/` для домашней страницы;
- `/query-builder` для интерфейса Query Builder;
- `/table` для браузеринга таблиц;
- `/table/{table}/{id}` для просмотра записей;
- `/schema-manager` для управления XML-схемой;
- `/static/{filename}` для CSS/JS;
- `/api/dynamic...` для динамических запросов;
- `/api/dynamic/meta...` для metadata и autocomplete;
- `/api/dynamic/schema.xml` и `/api/dynamic/schema/apply` для экспорта/импорта схемы.

4. Добавлен `handlers.h`.

В нем созданы handlers:

- `ApiDescriptionHandler` - отдает `home.html`;
- `QueryBuilderInterfaceHandler` - отдает `query_builder.html`;
- `TableInterfaceHandler` - отдает `table.html` для браузеринга таблиц;
- `RowViewInterfaceHandler` - отдает `row_view.html` для редактирования записей;
- `SchemaManagerPageHandler` - отдает `schema_manager.html`;
- `QueryBuilderHandler` - принимает JSON/query DSL, вызывает ORM `QueryBuilder`;
- `SchemaExportHandler` - экспортирует XML-схему из demo-базы;
- `SchemaApplyHandler` - применяет XML-схему к demo-базе;
- `QueryMetadataHandler` - возвращает таблицы, поля и autocomplete для UI.

5. Подключен ORM `QueryBuilder`.

`QueryBuilderHandler` передает входной запрос в `QueryBuilder::parseRequest(...)`, затем вызывает `exec()`. `QueryBuilder` валидирует имена таблиц/полей, строит SQL и выполняет его через `DatabaseInterface`.

6. Добавлена demo SQLite база.

В `demo_database.h` реализованы:

- `ensureDemoDatabase()` - создает файл SQLite, таблицы и данные;
- `demoDatabaseConfig()` - возвращает `DatabaseConfig` для SQLite.

Это сделано программно, чтобы пример запускался без внешнего PostgreSQL/MySQL и без ручной подготовки базы.

7. Добавлен переносимый расчет путей.

В `example_paths.h` путь к каталогу примера берется из CMake definition:

```cpp
DYNAMIC_WEB_QUERY_BUILDER_SERVER_SOURCE_DIR
```

Это убрало абсолютные локальные пути из `routes.h` и `handlers.h`.

8. Настроена сборка в `CMakeLists.txt`.

Определяются два target-а:

- `dynamic_web_query_builder_server` - основное приложение;
- `dynamic_web_query_builder_schema_roundtrip_test` - тест round-trip генерации XML-схемы.

Target `dynamic_web_query_builder_server` собирает:

- server-файлы Qornix Web;
- ORM core/database файлы;
- PostgreSQL/SQLite/MySQL драйверы;
- сам пример.

Для примера также передается compile definition с source dir:

```cmake
target_compile_definitions(dynamic_web_query_builder_server PRIVATE
    DYNAMIC_WEB_QUERY_BUILDER_SERVER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    QORNIX_ENABLE_SQLITE=1
)
```

9. Добавлены HTML-шаблоны.

`home.html` содержит интерактивные формы для CRUD-примеров. `query_builder.html` содержит полноценный UI для построения запросов, autocomplete таблиц/полей и отображение результата. `table.html` обеспечивает браузеринг таблиц с пагинацией и autocomplete. `row_view.html` позволяет просматривать и редактировать отдельные записи с inline-редактированием полей. `schema_manager.html` предоставляет UI для экспорта и импорта XML-схемы.

10. Актуализированы demo-запросы.

Начальные примеры на главной странице используют реальные таблицы demo SQLite базы:

- GET: JOIN `orders`, `products`, `customers`;
- POST: создание товара в `products`;
- PUT: обновление товара `products.id=2`;
- PATCH: обновление заказа `orders.id=4`;
- DELETE: удаление заказа `orders.id=6`.

11. Добавлен локальный `.gitignore`.

Он исключает созданную SQLite базу, XML-схемы и sidecar-файлы:

```text
dynamic_web_query_builder_demo.sqlite3
dynamic_web_query_builder_demo.sqlite3-*
dynamic_web_query_builder_demo.schema.xml
dynamic_web_query_builder_uploaded.schema.xml
/perf_test/report.json
```

## Проверка

Сборка target:

```bash
cmake --build build --target dynamic_web_query_builder_server
```

Проверка metadata после запуска:

```bash
curl http://localhost:8008/api/dynamic/meta/tables
curl http://localhost:8008/api/dynamic/meta/products/fields
```

Проверка dynamic query:

```bash
curl -X POST http://localhost:8008/api/dynamic/ \
  -H "Content-Type: application/json" \
  -d '{"method":"GET","table":"products","query":{"fields":["id","name","price","stock"],"limit":10}}'
```

Проверка экспорта XML-схемы:

```bash
curl http://localhost:8008/api/dynamic/schema.xml
```

Также XML-схему можно сгенерировать со страницы `/schema-manager`. На этой странице можно открыть XML-файл с диска и затем применить его к текущей demo SQLite базе.

После запроса будет создан файл:

```text
example/dynamic_web_query_builder_server/dynamic_web_query_builder_demo.schema.xml
```

Проверка round-trip генерации схемы без запуска HTTP server:

```bash
cmake --build build --target dynamic_web_query_builder_schema_roundtrip_test
./build/example/dynamic_web_query_builder_server/dynamic_web_query_builder_schema_roundtrip_test
```

Тест выполняет сценарий `SQLite DB -> XML -> SchemaDefinition -> DB apply` и проверяет, что в схеме есть таблицы `categories`, `products`, `customers`, `orders`, ключевые поля, индексы, foreign keys, CHECK constraint, view и trigger.

Проверка применения XML-схемы через HTTP:

```bash
curl -X POST http://localhost:8008/api/dynamic/schema/apply \
  -H "Content-Type: application/xml" \
  --data-binary @example/dynamic_web_query_builder_server/dynamic_web_query_builder_demo.schema.xml
```

Матрица поддержки драйверов для XML export описана в:

```text
doc/xml_schema_export_driver_support.md
```

## Performance Tests

В каталоге `perf_test/` находится Python performance test suite для проверки производительности API.

Тесты включают 6 фаз:

1. **simple_reads** - простые запросы GET по имени таблицы и по ID (7 endpoint patterns);
2. **complex_queries** - JOINs, фильтры, сортировка, пагинация (4 query patterns);
3. **metadata_endpoints** - таблицы, поля, autocomplete, schema.xml (9 endpoint patterns);
4. **write_operations** - POST create, PUT update, PATCH partial, DELETE (создает и очищает тестовые данные);
5. **concurrent_load** - тестирование на конкурентность уровнях 1, 5, 10, 20, 50, 100;
6. **stress_test** - sustained high-load across 6 endpoints.

Запуск:

```bash
cd perf_test
./run_perf_test.sh
```

Скрипт автоматически собирает target, запускает сервер, выполняет тесты и останавливает сервер. Результат сохраняется в `perf_test/report.json`.

## License

This project is dual-licensed under:

1. **GNU General Public License v3.0** (Open Source)
  - You may use, modify, and distribute this software freely under the terms of the GPL v3.0, provided that you also distribute your modifications under the same license.
  - For the full text of the GPL, visit: [GPLv3](https://www.gnu.org/licenses/gpl-3.0.txt).

2. **Commercial License**
  - If you want to use this software in a proprietary or closed-source project, you need to acquire a commercial license.
  - For commercial licensing inquiries, please contact us via [GitHub](https://github.com/hairetdin).

See the [LICENSE](../../LICENSE.txt) file for more details.
