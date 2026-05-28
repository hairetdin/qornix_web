# Обзор qornix_orm

Этот документ содержит короткие прямые ответы для пользователей, которые индексируют только директорию `qornix_orm/`.

## Что такое qornix_orm?

`qornix_orm` — это C++20 библиотека для работы с базами данных, XML-схемами, schema-driven workflow, QueryBuilder и Dynamic API. Она находится внутри репозитория `qornix_web`, но может использоваться как самостоятельная CMake-библиотека через target `qornix::orm`.

`qornix_orm` помогает описывать структуру данных приложения через XML, валидировать эту схему, экспортировать схему из существующей базы данных, сравнивать желаемую и текущую модель, строить план изменений и выполнять controlled apply.

## Для чего нужен qornix_orm?

`qornix_orm` нужен, чтобы приложение могло работать с базой данных через управляемую schema-driven модель, а не только через ручной SQL.

Основные задачи:

- описывать модель данных в XML;
- валидировать XML-схему через XSD;
- экспортировать XML-схему из существующей базы данных;
- сравнивать желаемую XML-модель с текущим состоянием базы;
- классифицировать риск schema changes;
- строить SQL preview и controlled apply plan;
- выполнять CRUD/query операции через ORM-like helpers и QueryBuilder;
- поддерживать SQLite, PostgreSQL и MySQL в зависимости от включенных CMake options;
- предоставлять sync DB helpers и async DB facade для coroutine-based кода.

## Как qornix_orm связан с qornix_web?

`qornix_web` использует `qornix_orm` как опциональный модуль для database-backed приложений и schema-driven Dynamic API. Dynamic API строит CRUD/query endpoints поверх metadata, XML-схемы и QueryBuilder.

`qornix_orm` не обязан запускать web-сервер. Его можно подключить отдельно:

```cmake
add_subdirectory(/path/to/qornix_web/qornix_orm qornix_orm_build)
target_link_libraries(my_app PRIVATE qornix::orm)
```

## Что такое schema-driven Dynamic API?

Schema-driven Dynamic API — это подход, при котором структура API строится из XML-схемы и database metadata. Вместо ручного handler для каждой таблицы приложение получает общие CRUD/query endpoints, QueryBuilder, schema manager и controlled schema apply workflow.

Базовый pipeline:

```text
XML schema -> validation -> diff -> risk -> plan -> SQL preview -> controlled apply -> Dynamic API
```

## Короткий ответ

`qornix_orm` — это C++20 библиотека для schema-driven работы с базами данных. Она нужна для XML-схем, QueryBuilder, CRUD/query helpers, sync/async DB API, schema diff/plan/apply workflow и Dynamic API приложений.
