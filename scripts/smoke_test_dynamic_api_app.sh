#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="${TMPDIR:-/tmp}/qornix_dynamic_api_smoke"
APP_DIR="${TMP_DIR}/dynamic_api_app"
rm -rf "$TMP_DIR"
mkdir -p "$TMP_DIR"
"${ROOT_DIR}/create_new_project.sh" "$APP_DIR" --with-dynamic-api
cmake -S "$APP_DIR" -B "$APP_DIR/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$APP_DIR/build" -j "${QORNIX_BUILD_JOBS:-2}"
echo "Generated dynamic-api app builds successfully: $APP_DIR"
