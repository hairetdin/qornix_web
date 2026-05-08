# Dynamic API application build and deployment

This document describes how to build and deploy an application generated with:

```bash
./create_new_project.sh ../my_app --with-dynamic-api
```

It focuses on the `templates/dynamic_api_app` template. A plain application generated without `--with-dynamic-api` is still a minimal development template and does not assemble the full deploy bundle described below.

## 1. Development build

Create the generated application next to the framework checkout:

```bash
cd qornix_web
./create_new_project.sh ../my_app --with-dynamic-api
```

Build it with CMake:

```bash
cd ../my_app
mkdir -p build
cd build
cmake ..
cmake --build .
```

The build produces two useful outputs:

```text
build/my_app
build/deploy/my_app/
```

`build/my_app` is the raw executable. It is useful during development, but it is not the recommended artifact to copy by itself.

`build/deploy/my_app/` is the portable application directory. Use this directory for local runs, copying to another directory, and packaging for another machine.

## 2. Deploy bundle layout

The generated Dynamic API app assembles this directory during `cmake --build .`:

```text
build/deploy/my_app/
├── my_app
├── config.yaml
├── README.md
├── templates/
├── static/
├── schema/
│   ├── app.schema.xml
│   └── schema_app.xsd
├── doc/
└── logs/
```

Each item is required for the out-of-the-box generated application:

| Path | Purpose |
|------|---------|
| `my_app` | Native application executable |
| `config.yaml` | Server address, port and logging settings |
| `templates/` | HTML pages for landing, Schema Manager, Query Builder, Table Browser, docs and error pages |
| `static/` | CSS, JavaScript, demo XML schema, raw Markdown docs and browser-loadable assets |
| `schema/app.schema.xml` | Editable desired XML schema used by Schema Manager |
| `schema/schema_app.xsd` | Local XML Schema Definition used for validation after deployment |
| `doc/` | Markdown documentation shipped with the generated app |
| `logs/` | Runtime log directory |
| `app.sqlite3` | Runtime SQLite database. It is created when the demo setup or schema workflow is used. |
| `schema/database.schema.xml` | Exported database snapshot. It is created by the Schema Manager export workflow. |
| `schema/schema_history.jsonl` | Schema apply history. It is created by Schema Manager. |

## 3. Running from the deploy bundle

The recommended local run mode is:

```bash
cd build/deploy/my_app
./my_app
```

Open:

```text
http://127.0.0.1:8008/
```

The generated app discovers its runtime root from files next to the executable, so it does not need to be started from the original source directory.

## 4. Copying to another directory

Copy the whole deploy directory, not only the executable:

```bash
cp -a build/deploy/my_app /opt/my_app
cd /opt/my_app
./my_app
```

This works because the application looks for `templates/`, `static/` and `schema/` next to the executable.

Do not deploy only this file:

```text
build/my_app
```

The raw executable alone does not contain HTML templates, CSS, JavaScript, schema files, docs or config. It can still work during development because the template keeps a compile-time fallback to the generated source directory, but that fallback is not a deployment model.

## 5. Copying to another machine

Copy the whole bundle:

```bash
rsync -a build/deploy/my_app user@host:/opt/my_app
```

Then run on the target machine:

```bash
cd /opt/my_app
./my_app
```

The target machine must have compatible runtime shared libraries installed unless the executable was built and linked according to your own static or bundled-library policy.

Inspect runtime dependencies on Linux with:

```bash
ldd ./my_app
```

Typical dependencies include:

```text
Boost
SQLite
 yaml-cpp
pugixml
libxml2
libstdc++
libgcc
libc
```

Package names depend on the Linux distribution. On Debian/Ubuntu, common development packages include:

```bash
sudo apt-get install libboost-all-dev libsqlite3-dev libyaml-cpp-dev libpugixml-dev libxml2-dev
```

For a production package, decide whether to rely on system packages, bundle shared libraries, or build a container image.

## 6. Explicit runtime root: QORNIX_APP_ROOT

If you want to keep the executable in one directory and the application assets in another directory, set `QORNIX_APP_ROOT`:

```bash
QORNIX_APP_ROOT=/opt/my_app /usr/local/bin/my_app
```

The directory pointed to by `QORNIX_APP_ROOT` must contain:

```text
templates/
static/
schema/
config.yaml
```

This is also useful for systemd services because it makes the runtime root explicit and independent of the service working directory.

## 7. Runtime root discovery order

The generated Dynamic API app uses this lookup order:

```text
1. QORNIX_APP_ROOT environment variable
2. directory containing the executable
3. app/ directory inside the executable directory
4. parent of the executable directory
5. current working directory
6. parent of the current working directory
7. compile-time source directory as a development fallback
```

A directory is treated as an app root when it contains:

```text
templates/
static/
schema/
```

The compile-time source fallback is intended for developer convenience only. Deployment should rely on the deploy bundle or `QORNIX_APP_ROOT`.

## 8. Manual deploy target

The default CMake build makes the deploy bundle automatically because the generated template defines an `ALL` deploy target.

You can also rebuild only the deploy bundle explicitly:

```bash
cmake --build . --target my_app_deploy
```

The deploy output path can be changed at configure time:

```bash
cmake -DMY_APP_DEPLOY_DIR=/tmp/my_app_bundle ..
cmake --build . --target my_app_deploy
```

The CMake variable is based on the generated project name in uppercase:

```text
<PROJECT_NAME_UPPER>_DEPLOY_DIR
```

For `my_app`, the variable is:

```text
MY_APP_DEPLOY_DIR
```

## 9. Install-style layout

The generated app also supports `cmake --install`:

```bash
cmake --install . --prefix /opt/my_app
```

This installs the executable and application assets into the prefix. This is useful when integrating with packaging tools, Docker images or CI/CD pipelines.

The install layout contains the same required runtime assets:

```text
/opt/my_app/
├── my_app
├── config.yaml
├── templates/
├── static/
├── schema/
└── doc/
```

## 10. Docker/container deployment pattern

A simple container workflow is:

```Dockerfile
FROM debian:stable-slim
RUN apt-get update && apt-get install -y \
    libboost-system1.83.0 libboost-filesystem1.83.0 libsqlite3-0 \
    libyaml-cpp0.8 libpugixml1v5 libxml2 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY build/deploy/my_app/ /app/
EXPOSE 8008
CMD ["./my_app"]
```

Adjust package names to the distribution and Boost version used by your build environment.

## 11. Configuration in deployment

The deployed app reads `config.yaml` from the app root. The default generated config uses:

```yaml
server:
  address: 127.0.0.1
  port: 8008
```

For a server deployment, you will usually change the address:

```yaml
server:
  address: 0.0.0.0
  port: 8008
```

The SQLite database path is currently derived from the app root:

```text
<app-root>/app.sqlite3
```

Schema Manager runtime files are also kept under the app root:

```text
<app-root>/schema/app.schema.xml
<app-root>/schema/database.schema.xml
<app-root>/schema/schema_history.jsonl
```

Make sure the process has write permissions to the app root or to the directories where runtime files are stored.

## 12. First run after deployment

Open the landing page:

```text
http://127.0.0.1:8008/
```

Then click:

```text
Initialize demo database
```

This calls:

```text
POST /api/demo/setup
```

The demo setup resets only the generated application database and schema runtime files, writes the bundled demo schema, creates demo tables and inserts rows used by Query Builder, Table Browser and API Playground.

For a production app, replace the demo schema with your real `schema/app.schema.xml` and use Schema Manager to validate, diff, plan and apply changes.

## 13. Troubleshooting

### The app starts but pages have no CSS or JavaScript

Run from the deploy bundle or set `QORNIX_APP_ROOT`. The app must be able to find:

```text
static/dynamic_api.css
static/dynamic_api_browser.js
static/schema_manager.js
```

### Schema validation cannot find `schema_app.xsd`

The deploy bundle must include:

```text
schema/schema_app.xsd
```

The generated app first tries the local deploy copy, then the browser-loadable static copy, then the framework compile-time XSD path as a development fallback.

### It works on the build machine but not on another machine

Check shared libraries:

```bash
ldd ./my_app
```

Install matching runtime packages or use a packaging/container strategy that bundles the required shared libraries.

### The app cannot write `app.sqlite3` or schema history

Check permissions for the app root:

```bash
ls -ld /opt/my_app /opt/my_app/schema /opt/my_app/logs
```

The process must be allowed to create or update:

```text
app.sqlite3
schema/app.schema.xml
schema/database.schema.xml
schema/schema_history.jsonl
logs/
```
