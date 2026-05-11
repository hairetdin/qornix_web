# Deployment guide

The default generated app creates a portable deployment bundle during build.

## Build

```bash
cd @PROJECT_NAME@
mkdir -p build
cd build
cmake ..
cmake --build .
```

## Deploy bundle

The bundle is created at:

```text
build/deploy/@PROJECT_NAME@/
```

Layout:

```text
@PROJECT_NAME@/
├── @PROJECT_NAME@
├── config.yaml
├── README.md
├── templates/
├── static/
├── doc/
├── route_extensions/
└── logs/
```

Copy the entire directory, not only the executable.

## Run from copied directory

```bash
cp -R build/deploy/@PROJECT_NAME@ /opt/@PROJECT_NAME@
cd /opt/@PROJECT_NAME@
./@PROJECT_NAME@
```

## Run with explicit root

```bash
QORNIX_APP_ROOT=/opt/@PROJECT_NAME@ /opt/@PROJECT_NAME@/@PROJECT_NAME@
```

## Install target

You can also install the application layout:

```bash
cmake --install . --prefix /opt/@PROJECT_NAME@
```

## Runtime shared libraries

The deploy bundle contains the application binary and runtime assets. It does not bundle system shared libraries. Check runtime dependencies with:

```bash
ldd ./@PROJECT_NAME@
```

On another machine, install compatible versions of required libraries such as Boost, yaml-cpp, sqlite3, libxml2, pugixml and libstdc++ as required by your build.

## Container pattern

The generated template includes `runtime-Dockerfile`. Build the app first, then build the runtime image from the project root:

```bash
cmake --build build --target @PROJECT_NAME@_deploy
docker build -f runtime-Dockerfile -t @PROJECT_NAME@:runtime .
```

Run with the embedded config:

```bash
docker run --rm -p 8008:8008 @PROJECT_NAME@:runtime
```

Run with external config and persistent file logs:

```bash
mkdir -p docker/config logs
cp build/deploy/@PROJECT_NAME@/config.yaml docker/config/config.yaml
sudo chown -R 10001:10001 logs
docker run -d --name @PROJECT_NAME@ \
  -p 8008:8008 \
  -v "$PWD/docker/config/config.yaml:/app/config.yaml:ro" \
  -v "$PWD/logs:/logs" \
  @PROJECT_NAME@:runtime
```

Use `logging.to_file: true` and `logging.file_path: /logs/server` in the mounted config. To move the app to another machine, run `docker save @PROJECT_NAME@:runtime -o @PROJECT_NAME@-runtime.tar`, copy the tar file and external config/log directories, then run `docker load -i @PROJECT_NAME@-runtime.tar` on the target.

The Dockerfile copies `build/deploy/@PROJECT_NAME@/` into `/app`, sets `QORNIX_APP_ROOT=/app` and changes the container config to listen on `0.0.0.0` so published ports work. If you change enabled modules or database drivers, check `ldd build/deploy/@PROJECT_NAME@/@PROJECT_NAME@` and adjust the package list.

## Troubleshooting

If pages load without CSS, the app cannot find `static/`. Run from the deploy root or set `QORNIX_APP_ROOT`.

If `config.yaml` is ignored, ensure the file is next to the binary or in a parent directory searched by the server manager.
