#!/bin/bash
# Quick start script for Qornix RAG

set -e

echo "🔍 Qornix RAG - Быстрый старт"
echo "=============================="
echo ""

# Check dependencies
echo "📋 Проверка зависимостей..."

if ! command -v cmake &> /dev/null; then
    echo "❌ CMake не найден!"
    echo "Установите: sudo apt-get install cmake"
    exit 1
fi

if ! command -v g++ &> /dev/null; then
    echo "❌ G++ не найден!"
    echo "Установите: sudo apt-get install g++"
    exit 1
fi

echo "✅ Зависимости найдены"
echo ""

# Build
echo "🔨 Сборка проекта..."
mkdir -p build
cd build
cmake ..
make -j$(nproc)

echo "✅ Сборка завершена"
echo ""

# Run
echo "🚀 Запуск сервера..."
echo ""

cd ..
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
./build/qornix_rag --config "${SCRIPT_DIR}/config.yaml" "$@"
