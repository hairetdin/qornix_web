#!/usr/bin/env bash
# Build a portable standalone Qornix RAG bundle.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEFAULT_BUILD_DIR="${REPO_ROOT}/build-portable"
DEFAULT_DIST_DIR="${REPO_ROOT}/dist"

BUILD_DIR="${QORNIX_RAG_PORTABLE_BUILD_DIR:-${DEFAULT_BUILD_DIR}}"
DIST_DIR="${QORNIX_RAG_DIST_DIR:-${DEFAULT_DIST_DIR}}"
BUILD_TYPE="Release"
BUNDLE_NAME=""
CREATE_ARCHIVE="false"
RUN_SMOKE="false"
INCLUDE_SYSTEM_LIBS="false"

usage() {
    cat <<'EOF_USAGE'
Qornix RAG portable bundle builder

Usage:
  ./qornix_rag/build_portable.sh [options]

Options:
  --build-dir <dir>       CMake build directory.
                          Default: <repo>/build-portable
  --dist-dir <dir>        Output directory.
                          Default: <repo>/dist
  --name <name>           Bundle directory name.
                          Default: qornix_rag-portable-<os>-<arch>
  --archive               Also create a .tar.gz archive.
  --smoke                 Run a short startup smoke test after packaging.
  --include-system-libs   Copy ldd-discovered non-glibc shared libraries into lib/.
  --help, -h              Show this help.

Environment:
  QORNIX_RAG_PORTABLE_BUILD_DIR
  QORNIX_RAG_DIST_DIR

The bundle is Linux x86_64 oriented and uses paths relative to its own root.
EOF_USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)
            BUILD_DIR="${2:?--build-dir requires a value}"
            shift 2
            ;;
        --dist-dir)
            DIST_DIR="${2:?--dist-dir requires a value}"
            shift 2
            ;;
        --name)
            BUNDLE_NAME="${2:?--name requires a value}"
            shift 2
            ;;
        --archive)
            CREATE_ARCHIVE="true"
            shift
            ;;
        --smoke)
            RUN_SMOKE="true"
            shift
            ;;
        --include-system-libs)
            INCLUDE_SYSTEM_LIBS="true"
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if ! command -v cmake >/dev/null 2>&1; then
    echo "CMake is required." >&2
    exit 1
fi

if command -v nproc >/dev/null 2>&1; then
    BUILD_JOBS="$(nproc)"
else
    BUILD_JOBS="2"
fi

OS_NAME="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH_NAME="$(uname -m)"
if [[ -z "${BUNDLE_NAME}" ]]; then
    BUNDLE_NAME="qornix_rag-portable-${OS_NAME}-${ARCH_NAME}"
fi

BUNDLE_DIR="${DIST_DIR}/${BUNDLE_NAME}"

echo "Building Qornix RAG portable bundle"
echo "  Repository: ${REPO_ROOT}"
echo "  Build dir:  ${BUILD_DIR}"
echo "  Bundle:     ${BUNDLE_DIR}"

cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DQORNIX_BUILD_APP=OFF \
    -DQORNIX_BUILD_RAG=ON \
    -DQORNIX_BUILD_RAG_STANDALONE=ON \
    -DQORNIX_BUILD_RAG_ROUTE_EXTENSION=OFF \
    -DQORNIX_BUILD_TESTS=OFF \
    -DQORNIX_ENABLE_ORM=OFF \
    -DQORNIX_BUILD_DYNAMIC_API=OFF \
    -DENABLE_AUTH=OFF
cmake --build "${BUILD_DIR}" --target qornix_rag --parallel "${BUILD_JOBS}"

EXECUTABLE="${BUILD_DIR}/qornix_rag/qornix_rag"
if [[ ! -x "${EXECUTABLE}" ]]; then
    EXECUTABLE="$(find "${BUILD_DIR}" -maxdepth 4 -type f -name qornix_rag -perm -u=x | sort | head -n 1 || true)"
fi
if [[ -z "${EXECUTABLE}" || ! -x "${EXECUTABLE}" ]]; then
    echo "Built qornix_rag executable was not found under ${BUILD_DIR}." >&2
    exit 1
fi

rm -rf "${BUNDLE_DIR}"
mkdir -p \
    "${BUNDLE_DIR}/bin" \
    "${BUNDLE_DIR}/lib" \
    "${BUNDLE_DIR}/templates" \
    "${BUNDLE_DIR}/static" \
    "${BUNDLE_DIR}/config" \
    "${BUNDLE_DIR}/data" \
    "${BUNDLE_DIR}/models" \
    "${BUNDLE_DIR}/logs" \
    "${BUNDLE_DIR}/cache" \
    "${BUNDLE_DIR}/knowledge_base"

cp "${EXECUTABLE}" "${BUNDLE_DIR}/bin/qornix_rag"
cp "${SCRIPT_DIR}/templates/rag_interface.html" "${BUNDLE_DIR}/templates/rag_interface.html"

if [[ -d "${SCRIPT_DIR}/models" ]]; then
    find "${SCRIPT_DIR}/models" -maxdepth 1 -type f \( -name '*.onnx' -o -name '*.json' \) -exec cp {} "${BUNDLE_DIR}/models/" \;
fi

sed \
    -e 's#qornix_rag/models/#models/#g' \
    -e 's#qornix_rag/data/rag_kb.db#data/rag_kb.db#g' \
    -e 's#qornix_rag/knowledge_base#knowledge_base#g' \
    -e 's#auto_index_on_startup: true#auto_index_on_startup: false#g' \
    "${SCRIPT_DIR}/config.yaml" > "${BUNDLE_DIR}/config/config.example.yaml"
cp "${BUNDLE_DIR}/config/config.example.yaml" "${BUNDLE_DIR}/config/config.yaml"

cat > "${BUNDLE_DIR}/run.sh" <<'EOF_RUN'
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export QORNIX_RAG_HOME="${QORNIX_RAG_HOME:-${SCRIPT_DIR}}"
export QORNIX_RAG_TEMPLATES_DIR="${QORNIX_RAG_TEMPLATES_DIR:-${SCRIPT_DIR}/templates}"
export LD_LIBRARY_PATH="${SCRIPT_DIR}/lib:${LD_LIBRARY_PATH:-}"

mkdir -p "${QORNIX_RAG_HOME}/data" \
         "${QORNIX_RAG_HOME}/logs" \
         "${QORNIX_RAG_HOME}/cache" \
         "${QORNIX_RAG_HOME}/knowledge_base"

cd "${SCRIPT_DIR}"
exec "${SCRIPT_DIR}/bin/qornix_rag" \
  --config "${SCRIPT_DIR}/config/config.yaml" \
  --address 127.0.0.1 \
  --port 8081 \
  "$@"
EOF_RUN
chmod +x "${BUNDLE_DIR}/run.sh"

cat > "${BUNDLE_DIR}/README.md" <<'EOF_README'
# Qornix RAG Portable

This is a portable standalone Linux bundle for Qornix RAG.

Run:

```bash
./run.sh
```

Defaults:

- binds to `127.0.0.1:8081`;
- uses `config/config.yaml`;
- loads the web UI from `templates/`;
- stores local runtime data under `data/`, `logs/`, `cache/`, and `knowledge_base/`.

Edit `config/config.yaml` to set the project path, LLM model, or other local settings.
The default portable config disables startup auto-indexing so the service can start
without indexing the bundle itself.
EOF_README

if command -v ldd >/dev/null 2>&1; then
    ldd "${BUNDLE_DIR}/bin/qornix_rag" > "${BUNDLE_DIR}/lib/ldd.txt" || true
    if [[ "${INCLUDE_SYSTEM_LIBS}" == "true" ]]; then
        while read -r lib_path; do
            lib_name="$(basename "${lib_path}")"
            case "${lib_name}" in
                libc.so.*|libm.so.*|libdl.so.*|libpthread.so.*|librt.so.*|ld-linux*|linux-vdso*)
                    continue
                    ;;
            esac
            cp -n "${lib_path}" "${BUNDLE_DIR}/lib/" 2>/dev/null || true
        done < <(ldd "${BUNDLE_DIR}/bin/qornix_rag" | awk '/=> \// {print $3} /^\// {print $1}')
    fi
fi

find "${BUNDLE_DIR}" -type f \( -name '*.db' -o -name '*.sqlite' -o -name '*.sqlite3' \) -delete
find "${BUNDLE_DIR}" -type d \( -name xapian_index -o -name cmake-build-debug -o -name build \) -prune -exec rm -rf {} +

if [[ "${RUN_SMOKE}" == "true" ]]; then
    echo "Running portable smoke test..."
    "${BUNDLE_DIR}/run.sh" --help >/dev/null
    if command -v curl >/dev/null 2>&1; then
        "${BUNDLE_DIR}/run.sh" --port 8092 --project "${BUNDLE_DIR}/knowledge_base" >/tmp/qornix_rag_portable_smoke.log 2>&1 &
        smoke_pid=$!
        cleanup_smoke() {
            kill "${smoke_pid}" >/dev/null 2>&1 || true
            wait "${smoke_pid}" >/dev/null 2>&1 || true
        }
        trap cleanup_smoke EXIT

        healthy="false"
        for _ in {1..40}; do
            if curl -fsS "http://127.0.0.1:8092/api/health" >/tmp/qornix_rag_portable_health.json 2>/dev/null; then
                healthy="true"
                break
            fi
            if ! kill -0 "${smoke_pid}" >/dev/null 2>&1; then
                break
            fi
            sleep 0.25
        done

        if [[ "${healthy}" != "true" ]]; then
            cat /tmp/qornix_rag_portable_smoke.log >&2 || true
            echo "Smoke test failed: /api/health did not respond." >&2
            exit 1
        fi

        cleanup_smoke
        trap - EXIT
    else
        timeout 8s "${BUNDLE_DIR}/run.sh" --port 8092 --project "${BUNDLE_DIR}/knowledge_base" >/tmp/qornix_rag_portable_smoke.log 2>&1 || status=$?
        status="${status:-0}"
        if [[ "${status}" != "0" && "${status}" != "124" ]]; then
            cat /tmp/qornix_rag_portable_smoke.log >&2 || true
            echo "Smoke test failed with status ${status}." >&2
            exit "${status}"
        fi
    fi
fi

find "${BUNDLE_DIR}/data" -mindepth 1 -delete
find "${BUNDLE_DIR}/logs" -mindepth 1 -delete
find "${BUNDLE_DIR}/cache" -mindepth 1 -delete
rm -rf "${BUNDLE_DIR}/xapian_index"

if [[ "${CREATE_ARCHIVE}" == "true" ]]; then
    tar -C "${DIST_DIR}" -czf "${DIST_DIR}/${BUNDLE_NAME}.tar.gz" "${BUNDLE_NAME}"
    echo "Archive: ${DIST_DIR}/${BUNDLE_NAME}.tar.gz"
fi

echo "Portable bundle created: ${BUNDLE_DIR}"
