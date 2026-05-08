# Creating a new application based on Qornix Web

This guide describes the two main scenarios for generating standalone applications based on Qornix Web.

## 1. Two application templates

### Default application

Generate it with:

```bash
./create_new_project.sh ../my_app
```

It uses the template:

```text
templates/app
```

This is a minimal but already user-friendly C++ web application skeleton. Use it when you want to add routes, handlers, services, HTML pages and API endpoints yourself.

Generated default application preview:

![Default Qornix generated app](assets/qornix_app_snap.png)

The generated application includes:

- a landing page at `/`;
- a health endpoint at `/health`;
- an in-app documentation hub at `/docs`;
- getting started, routing, configuration and deployment documents;
- static asset serving through `/static/{filename}`;
- markdown document preview through `/docs/raw/{filename}`;
- a portable deploy bundle under `build/deploy/<app>`.

### Schema-driven Dynamic API application

Generate it with:

```bash
./create_new_project.sh ../my_api --with-dynamic-api
```

It uses the template:

```text
templates/dynamic_api_app
```

Use this variant when you need Schema Manager, Dynamic API, Query Builder, Table Browser, API Playground and a demo database out of the box.

Generated Dynamic API application preview:

![Qornix dynamic API generated app](assets/qornix_dynamyc_api_app_snap.png)

## 2. Recommended workspace structure

A new application does not copy framework sources into itself. It links `qornix_web` through CMake.

```text
workspace/
├── qornix_web/
└── my_app/
```

## 3. Creating a default application

```bash
cd qornix_web
./create_new_project.sh ../my_app
```

After generation:

```bash
cd ../my_app
mkdir -p build
cd build
cmake ..
cmake --build .
```

Run from the deploy bundle:

```bash
cd deploy/my_app
./my_app
```

Open:

```text
http://127.0.0.1:8008/
http://127.0.0.1:8008/docs
http://127.0.0.1:8008/health
```

## 4. Default application structure

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
│   ├── index.html
│   ├── docs.html
│   └── 404.html
├── static/
│   └── style.css
├── doc/
│   ├── getting_started.md
│   ├── routing_and_handlers.md
│   ├── configuration.md
│   └── deployment.md
├── route_extensions/
└── logs/
```

## 5. What `create_new_project.sh` does

The script:

1. validates the project name;
2. chooses the template: `templates/app` or `templates/dynamic_api_app`;
3. copies the template to the new directory;
4. substitutes `@PROJECT_NAME@`, `@PROJECT_NAME_UPPER@` and `@QORNIX_WEB_ROOT@`;
5. creates the `logs` directory;
6. does not copy framework sources into the application;
7. prints build commands, run commands and the deploy bundle path.

## 6. Portable deploy bundle

The default template creates the deploy bundle automatically after building the executable:

```text
build/deploy/my_app/
├── my_app
├── config.yaml
├── templates/
├── static/
├── doc/
└── logs/
```

To move the application to another directory, copy the entire bundle:

```bash
cp -a build/deploy/my_app /opt/my_app
cd /opt/my_app
./my_app
```

If the binary is not launched from the bundle root, pass the root explicitly:

```bash
./my_app --root /opt/my_app
```

## 7. Runtime dependencies

The deploy bundle contains the binary, config, templates, static assets and documentation. It does not include system shared libraries.

Check dependencies:

```bash
ldd build/deploy/my_app/my_app
```

A target machine must provide compatible versions of the runtime libraries used during build.

## 8. Adding an endpoint

Create a handler, for example `handlers/hello_handler.h`, inherit it from `HandlerBase`, then register it in `routes.h`:

```cpp
server.add_route("GET", "/hello", std::make_shared<HelloHandler>());
```

See the generated application for details:

```text
handlers/health_handler.h
routes.h
```

## 9. Enabling ORM and JWT

ORM is disabled by default in the default template:

```bash
cmake .. -DMY_APP_ENABLE_ORM=ON
```

JWT is also disabled by default:

```bash
cmake .. -DMY_APP_ENABLE_JWT=ON
```

Auth is enabled by default through the `MY_APP_ENABLE_AUTH` option.

## 10. If the framework was moved

You can override the path to `qornix_web` during configuration:

```bash
cmake .. -DQORNIX_WEB_ROOT=/path/to/qornix_web
```
