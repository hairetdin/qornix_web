#!/bin/bash
# Dependency installation script for Qornix RAG

set -e

echo "📦 Установка зависимостей для Qornix RAG..."
echo "============================================"
echo ""

sudo apt-get update

echo "📋 установка основных зависимостей..."
sudo apt-get install -y \
    cmake \
    g++ \
    libboost-system-dev \
    libboost-filesystem-dev \
    libboost-url-dev \
    libboost-json-dev \
    libyaml-cpp-dev

echo "🔨 установка HNSWLIB..."
if [ ! -d "hnswlib" ]; then
    git clone https://github.com/nmslib/hnswlib.git
    cd hnswlib
    mkdir build && cd build
    cmake ..
    make -j$(nproc)
    sudo make install
    cd ../..
else
    echo "✅ HNSWLIB уже установлен"
fi

echo "📚 установка Xapian..."
sudo apt-get install -y \
    libxapian-dev \
    xapian-tools

echo ""
echo "✅ Все зависимости установлены!"
echo ""
echo "Теперь вы можете запустить:"
echo "  ./run.sh"
