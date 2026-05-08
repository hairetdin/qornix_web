# Deployment guide

The default generated app creates a portable deployment bundle during build.

## Build

```bash
cd my_app
mkdir -p build
cd build
cmake ..
cmake --build .
```

## Deploy bundle

The bundle is created at:

```text
build/deploy/my_app/
```

Layout:

```text
my_app/
├── my_app
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
cp -R build/deploy/my_app /opt/my_app
cd /opt/my_app
./my_app
```

## Run with explicit root

```bash
QORNIX_APP_ROOT=/opt/my_app /opt/my_app/my_app
```

## Install target

You can also install the application layout:

```bash
cmake --install . --prefix /opt/my_app
```

## Runtime shared libraries

The deploy bundle contains the application binary and runtime assets. It does not bundle system shared libraries. Check runtime dependencies with:

```bash
ldd ./my_app
```

On another machine, install compatible versions of required libraries such as Boost, yaml-cpp, sqlite3, libxml2, pugixml and libstdc++ as required by your build.

## Container pattern

A simple container image can copy the deploy bundle into `/opt/my_app` and run the binary with `WORKDIR /opt/my_app`.

## Troubleshooting

If pages load without CSS, the app cannot find `static/`. Run from the deploy root or set `QORNIX_APP_ROOT`.

If `config.yaml` is ignored, ensure the file is next to the binary or in a parent directory searched by the server manager.
