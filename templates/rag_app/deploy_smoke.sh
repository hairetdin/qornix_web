#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${1:-http://127.0.0.1:8008}"

curl -fsS "${BASE_URL}/health" >/dev/null || curl -fsS "${BASE_URL}/api/rag/health" >/dev/null
curl -fsS "${BASE_URL}/api/rag/health" >/dev/null

echo "deploy smoke passed: ${BASE_URL}"
