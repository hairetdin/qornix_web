# Qornix Web default application: getting started

This document belongs to the default application generated with:

```bash
./create_new_project.sh ../my_app
```

Use this template when you want a clean custom C++ web application based on Qornix Web. It includes routing, handlers, configuration, static assets, HTML pages and a portable deployment layout.

Use the Dynamic API template instead when you want Schema Manager, Query Builder, Table Browser and schema-driven CRUD out of the box:

```bash
./create_new_project.sh ../my_api --with-dynamic-api
```

## Build

```bash
cd my_app
mkdir -p build
cd build
cmake ..
cmake --build .
```

The build creates both the raw executable and a portable deploy bundle.

## Run for development

From the build directory:

```bash
./my_app
```

From the deploy bundle:

```bash
cd build/deploy/my_app
./my_app
```

Open:

```text
http://127.0.0.1:8008/
http://127.0.0.1:8008/health
http://127.0.0.1:8008/docs
```

## Generated project structure

```text
my_app/
├── CMakeLists.txt
├── README.md
├── config.yaml
├── main.cpp
├── routes.h
├── app_paths.h
├── handlers/
│   ├── health_handler.h
│   └── page_handlers.h
├── templates/
├── static/
├── doc/
├── route_extensions/
└── logs/
```

## What to edit first

1. Add a handler under `handlers/`.
2. Register a route in `routes.h`.
3. Add static assets under `static/` if needed.
4. Add HTML templates under `templates/` if serving pages.
5. Rebuild and open the route in the browser.

## Important concept

The generated app is intentionally small. It is not a database framework by default. It is a web application skeleton. You opt into ORM or Dynamic API features when you need them.
