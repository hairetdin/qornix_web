#!/usr/bin/env bash
# Download an optional ONNX embedding model for qornix_rag semantic retrieval.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$(basename "${SCRIPT_DIR}")" == "qornix_rag" ]]; then
    PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
else
    PROJECT_ROOT="${SCRIPT_DIR}"
fi

MODEL_REPO="${QORNIX_RAG_ONNX_MODEL_REPO:-Xenova/all-MiniLM-L6-v2}"
MODEL_ID="${QORNIX_RAG_ONNX_MODEL_ID:-mini-lm-l6-v2}"
MODEL_NAME="${QORNIX_RAG_ONNX_MODEL_NAME:-all-MiniLM-L6-v2}"
MODEL_VERSION="${QORNIX_RAG_ONNX_MODEL_VERSION:-main}"
MODEL_FILE="${QORNIX_RAG_ONNX_MODEL_FILE:-onnx/model.onnx}"
TOKENIZER_FILE="${QORNIX_RAG_ONNX_TOKENIZER_FILE:-tokenizer.json}"
TOKENIZER_TYPE="${QORNIX_RAG_ONNX_TOKENIZER_TYPE:-WordPiece}"
POOLING="${QORNIX_RAG_ONNX_POOLING:-mean}"
DIMENSION="${QORNIX_RAG_ONNX_DIMENSION:-384}"
MAX_SEQ_LEN="${QORNIX_RAG_ONNX_MAX_SEQ_LEN:-256}"
ONNX_THREADS="${QORNIX_RAG_ONNX_THREADS:-2}"
OUTPUT_DIR="${QORNIX_RAG_ONNX_OUTPUT_DIR:-${SCRIPT_DIR}/models}"
CONFIG_PATH="${QORNIX_RAG_CONFIG:-${SCRIPT_DIR}/config.yaml}"
UPDATE_CONFIG="true"
SET_ACTIVE="true"
FORCE="false"
DRY_RUN="false"

usage() {
    cat <<'EOF_USAGE'
Qornix RAG ONNX embedding model downloader

Usage:
  download_onnx_model.sh [options]

Default download:
  repo:       Xenova/all-MiniLM-L6-v2
  model:      onnx/model.onnx
  tokenizer:  tokenizer.json
  output:     qornix_rag/models/

Options:
  --repo <id>             Hugging Face model repo id.
  --model-id <id>         Local registry id for the installed embedding model.
  --model-name <name>     Human-readable model name written to config.
  --model-version <value> Model version/revision metadata written to config.
  --model-file <path>     Model file inside the repo.
  --tokenizer-file <path> Tokenizer file inside the repo.
  --tokenizer-type <type> Tokenizer type metadata, default WordPiece.
  --pooling <mean|cls>    Pooling mode metadata, default mean.
  --dimension <n>         Embedding dimension metadata, default 384.
  --max-seq-len <n>       Tokenizer/model sequence length metadata, default 256.
  --onnx-threads <n>      ONNX Runtime thread count metadata, default 2.
  --output-dir <dir>      Destination directory.
  --config <path>         qornix_rag config file to update.
  --no-config-update      Download files but do not edit config.yaml.
  --no-set-active         Add/update registry metadata without changing active_model_id.
  --force                 Re-download existing files.
  --dry-run               Print actions without downloading or editing.
  --help, -h              Show this help.

Environment overrides:
  QORNIX_RAG_ONNX_MODEL_REPO
  QORNIX_RAG_ONNX_MODEL_ID
  QORNIX_RAG_ONNX_MODEL_NAME
  QORNIX_RAG_ONNX_MODEL_VERSION
  QORNIX_RAG_ONNX_MODEL_FILE
  QORNIX_RAG_ONNX_TOKENIZER_FILE
  QORNIX_RAG_ONNX_TOKENIZER_TYPE
  QORNIX_RAG_ONNX_POOLING
  QORNIX_RAG_ONNX_DIMENSION
  QORNIX_RAG_ONNX_MAX_SEQ_LEN
  QORNIX_RAG_ONNX_THREADS
  QORNIX_RAG_ONNX_OUTPUT_DIR
  QORNIX_RAG_CONFIG

Notes:
  The default model is a BERT-style text encoder ONNX export. It is used for
  retrieval embeddings, not for LLM answer generation. Config updates write an
  embedding registry entry and make it active unless --no-set-active is used.
EOF_USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --repo)
            MODEL_REPO="${2:?--repo requires a value}"
            shift 2
            ;;
        --model-id)
            MODEL_ID="${2:?--model-id requires a value}"
            shift 2
            ;;
        --model-name)
            MODEL_NAME="${2:?--model-name requires a value}"
            shift 2
            ;;
        --model-version)
            MODEL_VERSION="${2:?--model-version requires a value}"
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
        --tokenizer-type)
            TOKENIZER_TYPE="${2:?--tokenizer-type requires a value}"
            shift 2
            ;;
        --pooling)
            POOLING="${2:?--pooling requires a value}"
            shift 2
            ;;
        --dimension)
            DIMENSION="${2:?--dimension requires a value}"
            shift 2
            ;;
        --max-seq-len)
            MAX_SEQ_LEN="${2:?--max-seq-len requires a value}"
            shift 2
            ;;
        --onnx-threads)
            ONNX_THREADS="${2:?--onnx-threads requires a value}"
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
        --no-set-active)
            SET_ACTIVE="false"
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
    OUTPUT_DIR="${PROJECT_ROOT}/${OUTPUT_DIR}"
fi
if [[ "${CONFIG_PATH}" != /* ]]; then
    CONFIG_PATH="${PROJECT_ROOT}/${CONFIG_PATH}"
fi

if [[ ! "${MODEL_ID}" =~ ^[A-Za-z0-9_.-]+$ ]]; then
    echo "Invalid --model-id '${MODEL_ID}'. Use only letters, numbers, dot, underscore, and hyphen." >&2
    exit 1
fi

MODEL_DIR_NAME="${MODEL_ID//\//_}"
MODEL_DEST="${OUTPUT_DIR}/${MODEL_DIR_NAME}/semantic_model.onnx"
TOKENIZER_DEST="${OUTPUT_DIR}/${MODEL_DIR_NAME}/tokenizer.json"
MODEL_URL="https://huggingface.co/${MODEL_REPO}/resolve/main/${MODEL_FILE}"
TOKENIZER_URL="https://huggingface.co/${MODEL_REPO}/resolve/main/${TOKENIZER_FILE}"

config_path_for() {
    local path="$1"
    if command -v python3 >/dev/null 2>&1; then
        python3 - "$PROJECT_ROOT" "$path" <<'PY'
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
echo "  Model id:   ${MODEL_ID}"
echo "  Name:       ${MODEL_NAME}"
echo "  Version:    ${MODEL_VERSION}"
echo "  Model:      ${MODEL_FILE}"
echo "  Tokenizer:  ${TOKENIZER_FILE}"
echo "  Output:     ${OUTPUT_DIR}"
echo "  Config:     ${CONFIG_PATH}"
echo "  Active:     ${SET_ACTIVE}"
echo ""

if [[ "${DRY_RUN}" == "true" ]]; then
    echo "Dry run: would download:"
    echo "  ${MODEL_URL}"
    echo "  ${TOKENIZER_URL}"
    if [[ "${UPDATE_CONFIG}" == "true" ]]; then
        echo "Dry run: would update embedding registry in ${CONFIG_PATH}"
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
        python3 - "${CONFIG_PATH}" "${MODEL_CONFIG_PATH}" "${TOKENIZER_CONFIG_PATH}" \
            "${MODEL_ID}" "${MODEL_NAME}" "${MODEL_VERSION}" "${TOKENIZER_TYPE}" \
            "${POOLING}" "${DIMENSION}" "${MAX_SEQ_LEN}" "${ONNX_THREADS}" "${SET_ACTIVE}" \
            "${MODEL_REPO}" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
model_path = sys.argv[2]
tokenizer_path = sys.argv[3]
model_id = sys.argv[4]
model_name = sys.argv[5]
model_version = sys.argv[6]
tokenizer_type = sys.argv[7]
pooling = sys.argv[8]
dimension = sys.argv[9]
max_seq_len = sys.argv[10]
onnx_threads = sys.argv[11]
set_active = sys.argv[12].lower() == "true"
model_repo = sys.argv[13]
lines = path.read_text(encoding="utf-8").splitlines()

start = None
for i, line in enumerate(lines):
    if line == "embedding:":
        start = i
        break

if start is None:
    start = len(lines)
    end = len(lines)
else:
    end = len(lines)
    for i in range(start + 1, len(lines)):
        line = lines[i]
        if line and not line.startswith((" ", "\t", "#")):
            end = i
            break

def q(value):
    return '"' + str(value).replace("\\", "\\\\").replace('"', '\\"') + '"'

block = [
    "embedding:",
    "  backend: onnx",
]
if set_active:
    block.append(f"  active_model_id: {q(model_id)}")
block.extend([
    f"  model_id: {q(model_id)}",
    f"  model_name: {q(model_name)}",
    f"  model_version: {q(model_version)}",
    f"  model_path: {q(model_path)}",
    f"  tokenizer_path: {q(tokenizer_path)}",
    f"  tokenizer_type: {q(tokenizer_type)}",
    f"  pooling: {q(pooling)}",
    f"  dimension: {dimension}",
    f"  max_seq_len: {max_seq_len}",
    f"  onnx_threads: {onnx_threads}",
    "  normalize_embeddings: true",
    "  enable_fallback: true",
    "  registry:",
    f"    {model_id}:",
    "      backend: onnx",
    f"      name: {q(model_name)}",
    f"      version: {q(model_version)}",
    f"      model_path: {q(model_path)}",
    f"      tokenizer_path: {q(tokenizer_path)}",
    f"      tokenizer_type: {q(tokenizer_type)}",
    f"      pooling: {q(pooling)}",
    f"      dimension: {dimension}",
    f"      max_seq_len: {max_seq_len}",
    f"      onnx_threads: {onnx_threads}",
    "      normalize_embeddings: true",
    "      enable_fallback: true",
    f"      source: {q('huggingface:' + model_repo)}",
    "",
])

path.write_text("\n".join(lines[:start] + block + lines[end:]) + "\n", encoding="utf-8")
PY
        echo "Updated config: ${CONFIG_PATH}"
        echo "Backup: ${CONFIG_PATH}.bak"
    fi
fi

echo ""
echo "Done."
echo "Next:"
echo "  start the RAG server/app"
echo "  open http://localhost:8081/api/embedding/models and check the active registry entry"
echo "  POST /api/embedding/switch with {\"model_id\":\"${MODEL_ID}\",\"reindex\":true} to switch and re-embed explicitly"
