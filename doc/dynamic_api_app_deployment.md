# Dynamic API application build and deployment

This document describes how to build and deploy an application generated with:

```bash
./create_new_project.sh ../my_app --with-dynamic-api
```

It focuses on the `templates/dynamic_api_app` template. A plain application generated without `--with-dynamic-api` also has a deploy bundle and `runtime-Dockerfile`, but it does not include Dynamic API schema, database and Schema Manager runtime files.

For the Dynamic API backend plus Vue/Vite frontend template, generate with `--with-dynamic-api-vue` and see `dynamic_api_vue_app_template.md`. That template uses the same backend Dynamic API idea, moves backend pages under `/backend/*` and additionally builds `frontend/dist` into the deploy bundle.

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

The generated template includes `runtime-Dockerfile`. It builds a runtime image around the existing deploy bundle rather than rebuilding the C++ application inside Docker.

Build the app first, then build the image from the project root:

```bash
cmake --build build --target my_app_deploy
docker build -f runtime-Dockerfile -t my_app:runtime .
```

Run with the config embedded in the image:

```bash
docker run --rm -p 8008:8008 my_app:runtime
```

The image keeps application files in `/app`, SQLite/schema state in `/data` and file logs in `/logs`. For persistent state, use named volumes:

```bash
docker volume create my_app_data
docker volume create my_app_logs
docker run -d --name my_app \
  -p 8008:8008 \
  -v my_app_data:/data \
  -v my_app_logs:/logs \
  my_app:runtime
```

For editable configuration, copy the generated config and mount it over `/app/config.yaml`:

```bash
mkdir -p docker/config data logs
cp build/deploy/my_app/config.yaml docker/config/config.yaml
```

Use container paths for runtime files:

```yaml
server:
  address: 0.0.0.0
  port: 8008
logging:
  enabled: true
  level: info
  to_file: true
  file_path: /logs/server
database:
  driver: sqlite
  path: /data/app.sqlite3
dynamic_api:
  schema:
    file: /data/schema/app.schema.xml
    exported_file: /data/schema/database.schema.xml
    history_file: /data/schema/schema_history.jsonl
```

Run with bind mounts:

```bash
sudo chown -R 10001:10001 data logs
docker run -d --name my_app \
  -p 8008:8008 \
  -v "$PWD/docker/config/config.yaml:/app/config.yaml:ro" \
  -v "$PWD/data:/data" \
  -v "$PWD/logs:/logs" \
  my_app:runtime
```

The container user is UID/GID `10001` by default. You can change it while building:

```bash
docker build -f runtime-Dockerfile \
  --build-arg QORNIX_UID="$(id -u)" \
  --build-arg QORNIX_GID="$(id -g)" \
  -t my_app:runtime .
```

To move the app to another computer, either use a registry or export the image:

```bash
docker save my_app:runtime -o my_app-runtime.tar
```

Copy `my_app-runtime.tar`, external `docker/config/config.yaml`, and the `data/` directory if you need to move the SQLite database and schema history. On the target machine:

```bash
docker load -i my_app-runtime.tar
docker run -d --name my_app -p 8008:8008 \
  -v "$PWD/docker/config/config.yaml:/app/config.yaml:ro" \
  -v "$PWD/data:/data" \
  -v "$PWD/logs:/logs" \
  my_app:runtime
```

The Dockerfile copies `build/deploy/my_app/` into `/app`, sets `QORNIX_APP_ROOT=/app`, makes the binary executable and changes the container config to listen on `0.0.0.0` so published ports work. It uses Ubuntu 24.04 runtime package names, including Boost.URL, Boost.JSON, Boost.Log, yaml-cpp, OpenSSL, SQLite, pugixml and libxml2. If you change the build distribution or enable PostgreSQL/MySQL drivers, inspect runtime dependencies with `ldd build/deploy/my_app/my_app` and adjust the package list.

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

The SQLite database path is read from `database.path` in `config.yaml`. A relative path is resolved from the app root:

```text
<app-root>/app.sqlite3
```

Schema Manager runtime files are configured under `dynamic_api.schema`. Relative paths are resolved from the app root:

```text
<app-root>/schema/app.schema.xml
<app-root>/schema/database.schema.xml
<app-root>/schema/schema_history.jsonl
```

For containers, prefer absolute writable paths under mounted volumes:

```yaml
database:
  driver: sqlite
  path: /data/app.sqlite3
dynamic_api:
  schema:
    file: /data/schema/app.schema.xml
    exported_file: /data/schema/database.schema.xml
    history_file: /data/schema/schema_history.jsonl
logging:
  to_file: true
  file_path: /logs/server
```

Make sure the process has write permissions to the app root or to the mounted directories where runtime files are stored.

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

Check permissions for the configured writable paths. For the default non-container layout:

```bash
ls -ld /opt/my_app /opt/my_app/schema /opt/my_app/logs
```

For the container layout with bind mounts:

```bash
ls -ld data data/schema logs
sudo chown -R 10001:10001 data logs
```

The process must be allowed to create or update:

```text
app.sqlite3
schema/app.schema.xml
schema/database.schema.xml
schema/schema_history.jsonl
logs/
```


## Dynamic API + React template

For the same Dynamic API backend with a React/Vite frontend scaffold, use:

```bash
./create_new_project.sh ../my_react --with-dynamic-api-react
```

See `dynamic_api_react_app_template.md` for the React template guide.
