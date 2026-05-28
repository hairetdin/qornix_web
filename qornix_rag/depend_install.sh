#!/bin/bash
# Dependency installation script for Qornix RAG on Debian/Ubuntu.
# It installs the normal local-development profile. ONNX Runtime and external
# vector stores are documented in qornix_rag/doc/FULL_RAG_GUIDE.md because their
# install paths and versions are environment-specific.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

install_if_available() {
    local pkg="$1"
    if apt-cache show "$pkg" >/dev/null 2>&1; then
        sudo apt-get install -y "$pkg"
    else
        echo "⚠️  Optional apt package not found in enabled repositories: $pkg"
    fi
}

echo "📦 Установка зависимостей для Qornix RAG..."
echo "============================================"
echo ""

sudo apt-get update

echo "📋 Установка основных build/runtime зависимостей..."
sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    git \
    curl \
    wget \
    ca-certificates \
    g++ \
    libssl-dev \
    libboost-system-dev \
    libboost-filesystem-dev \
    libboost-url-dev \
    libboost-json-dev \
    libboost-log-dev \
    libyaml-cpp-dev \
    libcurl4-openssl-dev \
    libsqlite3-dev \
    libxapian-dev \
    xapian-tools \
    libzip-dev \
    libpugixml-dev \
    poppler-utils \
    tesseract-ocr

echo "📋 Установка опциональных dev-зависимостей, если они есть в репозиториях..."
install_if_available libpq-dev
install_if_available postgresql-client
install_if_available libfaiss-dev
install_if_available redis-server

echo "🔨 Установка HNSWLIB..."
WORK_DIR="${SCRIPT_DIR}/.deps"
mkdir -p "$WORK_DIR"
if [ ! -d "$WORK_DIR/hnswlib" ]; then
    git clone https://github.com/nmslib/hnswlib.git "$WORK_DIR/hnswlib"
fi
cmake -S "$WORK_DIR/hnswlib" -B "$WORK_DIR/hnswlib/build"
cmake --build "$WORK_DIR/hnswlib/build" -j"$(nproc)"
sudo cmake --install "$WORK_DIR/hnswlib/build"

echo ""
echo "✅ Базовые зависимости установлены."
echo ""
echo "Следующие компоненты устанавливаются отдельно при необходимости:"
echo "  - ONNX Runtime C++ SDK для semantic embeddings"
echo "  - Redis server для cache.backend=redis (depend_install.sh установит пакет, если он есть в apt)"
echo "  - Qdrant service для vector_store.backend=qdrant"
echo "  - PostgreSQL + pgvector для vector_store.backend=pgvector"
echo "  - Ollama/LM Studio/vLLM/OpenAI-compatible LLM provider"
echo ""
echo "Подробности: qornix_rag/doc/FULL_RAG_GUIDE.md"
echo "Теперь можно запустить:"
echo "  ./qornix_rag/run.sh"
