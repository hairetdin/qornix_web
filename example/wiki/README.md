# Qornix Wiki example

A full-featured wiki application example built on the current C++ HTTP framework.

## Features

- SQLite storage at `example/wiki/data/wiki.sqlite`;
- database schema applied through `qornix_orm` from `example/wiki/wiki_schema.xml`;
- create, read, edit and delete articles;
- search by articles, authors and tags;
- filtering by category, status, favorite flag and recent updates;
- statistics, categories and activity log.

## Build and run

```bash
cd qornix_web
mkdir -p build
cd build
cmake .. -DQORNIX_BUILD_EXAMPLES=ON
cmake --build . --target qornix_wiki_example
./example/wiki/qornix_wiki_example
```

After startup the site is available at `http://127.0.0.1:8008/`.

## Notes

- The SQLite database is created automatically on first start.
- Generated runtime data is stored under `example/wiki/data/`.
- The application demonstrates static templates, ORM-backed storage and a small admin-style UI.
