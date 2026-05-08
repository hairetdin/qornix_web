#!/usr/bin/env bash
#
# Automated performance test runner for dynamic_web_query_builder_server.
#
# This script:
#   1. Builds the server (if needed)
#   2. Starts the server in the background
#   3. Waits for it to be ready
#   4. Runs the Python performance test
#   5. Stops the server
#   6. Prints a summary
#
# Usage:
#   ./run_perf_test.sh [--build] [--concurrency N] [--requests N] [--output FILE]
#
# Options:
#   --build          Force rebuild (default: only rebuild if binary missing)
#   --concurrency N  Override concurrency (default: 20)
#   --requests N     Override requests per phase (default: 200)
#   --output FILE    Save JSON report to file
#   --help           Show this help

set -euo pipefail

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
SERVER_BIN="$BUILD_DIR/example/dynamic_web_query_builder_server/dynamic_web_query_builder_server"
PERF_SCRIPT="$SCRIPT_DIR/perf_test.py"
SERVER_PID=""
SERVER_HOST="127.0.0.1"
SERVER_PORT="8008"

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------
FORCE_BUILD=false
CONCURRENCY=20
REQUESTS=200
OUTPUT=""

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------
show_help() {
    head -20 "$0" | sed 's/^# \?//'
    exit 0
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --build)       FORCE_BUILD=true; shift ;;
        --concurrency) CONCURRENCY="$2"; shift 2 ;;
        --requests)    REQUESTS="$2"; shift 2 ;;
        --output)      OUTPUT="$2"; shift 2 ;;
        --help)        show_help ;;
        *)
            echo "Unknown option: $1" >&2
            show_help
            ;;
    esac
done

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
log() {
    echo "[$(date '+%H:%M:%S')] $*"
}

cleanup() {
    if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        log "Stopping server (PID: $SERVER_PID)..."
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        log "Server stopped."
    fi
}
trap cleanup EXIT

wait_for_server() {
    local max_attempts=30
    local attempt=0
    log "Waiting for server at $SERVER_HOST:$SERVER_PORT..."
    while ! curl -s -o /dev/null -w "%{http_code}" "http://$SERVER_HOST:$SERVER_PORT/" 2>/dev/null | grep -q "200\|301\|302"; do
        attempt=$((attempt + 1))
        if [[ $attempt -ge $max_attempts ]]; then
            echo "ERROR: Server did not become ready after $((max_attempts * 1)) seconds." >&2
            return 1
        fi
        sleep 1
    done
    log "Server is ready."
}

# ---------------------------------------------------------------------------
# Step 1: Build
# ---------------------------------------------------------------------------
build_server() {
    if [[ "$FORCE_BUILD" == true ]] || [[ ! -f "$SERVER_BIN" ]]; then
        log "Building server..."
        cd "$PROJECT_ROOT"
        mkdir -p "$BUILD_DIR"
        cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -5
        cmake --build "$BUILD_DIR" --target dynamic_web_query_builder_server -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) 2>&1 | tail -5
        if [[ ! -f "$SERVER_BIN" ]]; then
            echo "ERROR: Build failed — binary not found at $SERVER_BIN" >&2
            exit 1
        fi
        log "Build successful."
    else
        log "Binary already exists: $SERVER_BIN (use --build to force rebuild)"
    fi
}

# ---------------------------------------------------------------------------
# Step 2: Start server
# ---------------------------------------------------------------------------
start_server() {
    log "Starting server..."
    # Remove old demo DB to get a clean test
    rm -f "$BUILD_DIR/example/dynamic_web_query_builder_server/dynamic_web_query_builder_demo.sqlite3"

    cd "$SCRIPT_DIR"
    "$SERVER_BIN" &
    SERVER_PID=$!
    log "Server started (PID: $SERVER_PID)"
    wait_for_server
}

# ---------------------------------------------------------------------------
# Step 3: Run performance test
# ---------------------------------------------------------------------------
run_tests() {
    log "Running performance test..."
    log "Concurrency: $CONCURRENCY  |  Requests per phase: $REQUESTS"

    local cmd="python3 $PERF_SCRIPT --host $SERVER_HOST --port $SERVER_PORT --concurrency $CONCURRENCY --requests $REQUESTS"
    if [[ -n "$OUTPUT" ]]; then
        cmd="$cmd --output $OUTPUT"
    fi

    eval "$cmd"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    log "=========================================="
    log "  Performance Test Runner"
    log "=========================================="
    log "Project root: $PROJECT_ROOT"
    log "Server binary: $SERVER_BIN"
    log "Python script: $PERF_SCRIPT"
    echo

    build_server
    echo
    start_server
    echo
    run_tests
    echo

    log "=========================================="
    log "  Done."
    log "=========================================="
}

main
