#!/usr/bin/env bash
# Download an optional ONNX embedding model for qornix_rag semantic retrieval.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

MODEL_REPO="${QORNIX_RAG_ONNX_MODEL_REPO:-Xenova/all-MiniLM-L6-v2}"
MODEL_FILE="${QORNIX_RAG_ONNX_MODEL_FILE:-onnx/model.onnx}"
TOKENIZER_FILE="${QORNIX_RAG_ONNX_TOKENIZER_FILE:-tokenizer.json}"
OUTPUT_DIR="${QORNIX_RAG_ONNX_OUTPUT_DIR:-${SCRIPT_DIR}/models}"
CONFIG_PATH="${QORNIX_RAG_CONFIG:-${SCRIPT_DIR}/config.yaml}"
UPDATE_CONFIG="true"
FORCE="false"
DRY_RUN="false"

usage() {
    cat <<'EOF_USAGE'
Qornix RAG ONNX embedding model downloader

Usage:
  ./qornix_rag/download_onnx_model.sh [options]

Default download:
  repo:       Xenova/all-MiniLM-L6-v2
  model:      onnx/model.onnx
  tokenizer:  tokenizer.json
  output:     qornix_rag/models/

Options:
  --repo <id>             Hugging Face model repo id.
  --model-file <path>     Model file inside the repo.
  --tokenizer-file <path> Tokenizer file inside the repo.
  --output-dir <dir>      Destination directory.
  --config <path>         qornix_rag config file to update.
  --no-config-update      Download files but do not edit config.yaml.
  --force                 Re-download existing files.
  --dry-run               Print actions without downloading or editing.
  --help, -h              Show this help.

Environment overrides:
  QORNIX_RAG_ONNX_MODEL_REPO
  QORNIX_RAG_ONNX_MODEL_FILE
  QORNIX_RAG_ONNX_TOKENIZER_FILE
  QORNIX_RAG_ONNX_OUTPUT_DIR
  QORNIX_RAG_CONFIG

Notes:
  The default model is a BERT-style text encoder ONNX export. It is used for
  retrieval embeddings, not for LLM answer generation.
EOF_USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --repo)
            MODEL_REPO="${2:?--repo requires a value}"
            shift 2
            ;;
        --model-file)
            MODEL_FILE="${2:?--model-file requires a value}"
            shift 2
            ;;
        --tokenizer-file)
            TOKENIZER_FILE="${2:?--tokenizer-file requires a value}"
            shift 2
            ;;
        --output-dir)
            OUTPUT_DIR="${2:?--output-dir requires a value}"
            shift 2
            ;;
        --config)
            CONFIG_PATH="${2:?--config requires a value}"
            shift 2
            ;;
        --no-config-update)
            UPDATE_CONFIG="false"
            shift
            ;;
        --force)
            FORCE="true"
            shift
            ;;
        --dry-run)
            DRY_RUN="true"
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

if [[ "${OUTPUT_DIR}" != /* ]]; then
    OUTPUT_DIR="${REPO_ROOT}/${OUTPUT_DIR}"
fi
if [[ "${CONFIG_PATH}" != /* ]]; then
    CONFIG_PATH="${REPO_ROOT}/${CONFIG_PATH}"
fi

MODEL_DEST="${OUTPUT_DIR}/semantic_model.onnx"
TOKENIZER_DEST="${OUTPUT_DIR}/tokenizer.json"
MODEL_URL="https://huggingface.co/${MODEL_REPO}/resolve/main/${MODEL_FILE}"
TOKENIZER_URL="https://huggingface.co/${MODEL_REPO}/resolve/main/${TOKENIZER_FILE}"

config_path_for() {
    local path="$1"
    if command -v python3 >/dev/null 2>&1; then
        python3 - "$REPO_ROOT" "$path" <<'PY'
import os
import sys

root = os.path.abspath(sys.argv[1])
path = os.path.abspath(sys.argv[2])
try:
    rel = os.path.relpath(path, root)
    if not rel.startswith(".."):
        print(rel)
    else:
        print(path)
except Exception:
    print(path)
PY
    else
        printf '%s\n' "$path"
    fi
}

MODEL_CONFIG_PATH="$(config_path_for "${MODEL_DEST}")"
TOKENIZER_CONFIG_PATH="$(config_path_for "${TOKENIZER_DEST}")"

echo "Qornix RAG ONNX model download"
echo "  Repo:       ${MODEL_REPO}"
echo "  Model:      ${MODEL_FILE}"
echo "  Tokenizer:  ${TOKENIZER_FILE}"
echo "  Output:     ${OUTPUT_DIR}"
echo "  Config:     ${CONFIG_PATH}"
echo ""

if [[ "${DRY_RUN}" == "true" ]]; then
    echo "Dry run: would download:"
    echo "  ${MODEL_URL}"
    echo "  ${TOKENIZER_URL}"
    if [[ "${UPDATE_CONFIG}" == "true" ]]; then
        echo "Dry run: would update embedding backend in ${CONFIG_PATH}"
    fi
    exit 0
fi

download() {
    local url="$1"
    local dest="$2"
    local label="$3"

    if [[ -s "${dest}" && "${FORCE}" != "true" ]]; then
        echo "Using existing ${label}: ${dest}"
        return
    fi

    mkdir -p "$(dirname "${dest}")"
    local tmp
    tmp="$(mktemp "${dest}.tmp.XXXXXX")"
    rm -f "${tmp}"

    echo "Downloading ${label}..."
    if command -v curl >/dev/null 2>&1; then
        curl -fL --retry 3 --connect-timeout 20 -o "${tmp}" "${url}"
    elif command -v wget >/dev/null 2>&1; then
        wget -O "${tmp}" "${url}"
    else
        echo "curl or wget is required to download model files." >&2
        exit 1
    fi

    if [[ ! -s "${tmp}" ]]; then
        echo "Downloaded ${label} is empty: ${url}" >&2
        rm -f "${tmp}"
        exit 1
    fi

    mv "${tmp}" "${dest}"
    echo "Saved ${label}: ${dest}"
}

download "${MODEL_URL}" "${MODEL_DEST}" "ONNX model"
download "${TOKENIZER_URL}" "${TOKENIZER_DEST}" "tokenizer"

if [[ "${UPDATE_CONFIG}" == "true" ]]; then
    if [[ ! -f "${CONFIG_PATH}" ]]; then
        echo "Config file not found, skipping config update: ${CONFIG_PATH}" >&2
    elif ! command -v python3 >/dev/null 2>&1; then
        echo "python3 not found, skipping config update." >&2
        echo "Set embedding.backend: onnx manually in ${CONFIG_PATH}." >&2
    else
        cp "${CONFIG_PATH}" "${CONFIG_PATH}.bak"
        python3 - "${CONFIG_PATH}" "${MODEL_CONFIG_PATH}" "${TOKENIZER_CONFIG_PATH}" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
model_path = sys.argv[2]
tokenizer_path = sys.argv[3]
lines = path.read_text(encoding="utf-8").splitlines()

start = None
for i, line in enumerate(lines):
    if line == "embedding:":
        start = i
        break

if start is None:
    raise SystemExit("embedding block not found")

end = len(lines)
for i in range(start + 1, len(lines)):
    line = lines[i]
    if line and not line.startswith((" ", "\t", "#")):
        end = i
        break

block = [
    "embedding:",
    "  backend: onnx",
    f"  model_path: {model_path}",
    f"  tokenizer_path: {tokenizer_path}",
    "  max_seq_len: 256",
    "  onnx_threads: 2",
    "  normalize_embeddings: true",
    "  enable_fallback: true",
    "",
]

path.write_text("\n".join(lines[:start] + block + lines[end:]) + "\n", encoding="utf-8")
PY
        echo "Updated config: ${CONFIG_PATH}"
        echo "Backup: ${CONFIG_PATH}.bak"
    fi
fi

echo ""
echo "Done."
echo "Next:"
echo "  ./qornix_rag/run.sh"
echo "  open http://localhost:8081/api/health and check rag.embedding_backend"
