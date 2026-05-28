#!/usr/bin/env bash
# Quick start script for Qornix RAG standalone mode.
# Builds and runs from the repository root no matter where this script is called from.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${QORNIX_RAG_BUILD_DIR:-${REPO_ROOT}/build}"
CONFIG_PATH="${QORNIX_RAG_CONFIG:-${SCRIPT_DIR}/config.yaml}"
QORNIX_RAG_HOME="${QORNIX_RAG_HOME:-${SCRIPT_DIR}}"
QORNIX_RAG_TEMPLATES_DIR="${QORNIX_RAG_TEMPLATES_DIR:-${SCRIPT_DIR}/templates}"

print_usage() {
    cat <<EOF_USAGE
Qornix RAG standalone launcher

Usage:
  ./qornix_rag/run.sh [qornix_rag arguments]

Environment variables:
  QORNIX_RAG_BUILD_DIR   Override CMake build directory.
                         Default: <repo>/build
  QORNIX_RAG_CONFIG      Override standalone config path.
                         Default: <repo>/qornix_rag/config.yaml
  QORNIX_RAG_HOME        Override standalone runtime home.
                         Default: <repo>/qornix_rag
  QORNIX_RAG_TEMPLATES_DIR
                         Override UI templates directory.
                         Default: <repo>/qornix_rag/templates

Examples:
  ./qornix_rag/run.sh
  ./qornix_rag/run.sh --port 8081
  QORNIX_RAG_CONFIG=/path/to/config.yaml ./qornix_rag/run.sh

Notes:
  The script builds the qornix_rag CMake target before launching it.
  Extra arguments are passed to the qornix_rag executable.
EOF_USAGE
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    print_usage
    exit 0
fi

echo "🔍 Qornix RAG - Quick Start"
echo "=============================="
echo ""

# Check dependencies
echo "📋 Checking dependencies..."

if ! command -v cmake >/dev/null 2>&1; then
    echo "❌ CMake was not found!"
    echo "Install it with: sudo apt-get install cmake"
    exit 1
fi

if ! command -v g++ >/dev/null 2>&1; then
    echo "❌ G++ was not found!"
    echo "Install it with: sudo apt-get install g++"
    exit 1
fi

if command -v nproc >/dev/null 2>&1; then
    BUILD_JOBS="$(nproc)"
else
    BUILD_JOBS="2"
fi

echo "✅ Dependencies found"
echo ""

# Prepare local standalone directories. They are intentionally kept outside the
# portable bundle work, which will get its own layout in Milestone A5.
mkdir -p "${QORNIX_RAG_HOME}/data" "${QORNIX_RAG_HOME}/data/uploads" "${QORNIX_RAG_HOME}/logs" "${QORNIX_RAG_HOME}/knowledge_base"

# Build
echo "🔨 Building project..."
echo "   Repository: ${REPO_ROOT}"
echo "   Build dir:  ${BUILD_DIR}"
cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" -DQORNIX_BUILD_RAG=ON
cmake --build "${BUILD_DIR}" --target qornix_rag --parallel "${BUILD_JOBS}"

echo "✅ Build complete"
echo ""

# Resolve the standalone executable. With the repository root as the CMake
# source directory, target binaries are generated under build/qornix_rag/.
EXECUTABLE_CANDIDATES=(
    "${BUILD_DIR}/qornix_rag/qornix_rag"
    "${BUILD_DIR}/bin/qornix_rag"
    "${BUILD_DIR}/qornix_rag"
)

QORNIX_RAG_EXECUTABLE=""
for candidate in "${EXECUTABLE_CANDIDATES[@]}"; do
    if [[ -f "${candidate}" && -x "${candidate}" ]]; then
        QORNIX_RAG_EXECUTABLE="${candidate}"
        break
    fi
done

if [[ -z "${QORNIX_RAG_EXECUTABLE}" ]]; then
    QORNIX_RAG_EXECUTABLE="$(find "${BUILD_DIR}" -maxdepth 3 -type f -name qornix_rag -perm -u=x | sort | head -n 1 || true)"
fi

if [[ -z "${QORNIX_RAG_EXECUTABLE}" ]]; then
    echo "❌ qornix_rag executable was not found after the build."
    echo "   Build dir: ${BUILD_DIR}"
    echo "   Expected one of:"
    for candidate in "${EXECUTABLE_CANDIDATES[@]}"; do
        echo "   - ${candidate}"
    done
    exit 1
fi

# Run
if [[ ! -f "${QORNIX_RAG_TEMPLATES_DIR}/rag_interface.html" ]]; then
    echo "❌ Web UI template was not found: ${QORNIX_RAG_TEMPLATES_DIR}/rag_interface.html"
    echo "   Check QORNIX_RAG_TEMPLATES_DIR or the integrity of qornix_rag/templates."
    exit 1
fi

export QORNIX_RAG_HOME
export QORNIX_RAG_TEMPLATES_DIR

# Run
echo "🚀 Starting server..."
echo "   Binary: ${QORNIX_RAG_EXECUTABLE}"
echo "   Config: ${CONFIG_PATH}"
echo "   Templates: ${QORNIX_RAG_TEMPLATES_DIR}"
echo ""

cd "${REPO_ROOT}"
exec "${QORNIX_RAG_EXECUTABLE}" --config "${CONFIG_PATH}" "$@"
