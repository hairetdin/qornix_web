# Configuration

The generated application reads runtime settings from `config.yaml`.

## Default location

During development, `config.yaml` lives in the project root. During deployment, it lives next to the binary in the deploy bundle.

The server manager searches for config near the executable, so this works:

```bash
cd build/deploy/my_app
./my_app
```

## Typical config

```yaml
server:
  address: "127.0.0.1"
  port: 8008
logging:
  enabled: true
  level: "info"
  to_file: true
  file_path: "logs/app.log"
```

## Command-line overrides

The server also supports command-line logging options:

```bash
./my_app --log --log-level debug
./my_app --log-file logs/debug.log
```

## Application root

The default app uses `app_paths.h` to locate templates, static files and documentation. Root lookup order:

1. `QORNIX_APP_ROOT`
2. directory containing the executable
3. parent of the executable directory
4. current working directory
5. parent of current working directory
6. compile-time source directory fallback

Set `QORNIX_APP_ROOT` when you want to run the binary from outside the deploy directory:

```bash
QORNIX_APP_ROOT=/opt/my_app /opt/my_app/my_app
```
