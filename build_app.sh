#!/bin/bash
# build_app.sh

echo "Building Qornix Web Application..."

# Create build directory
mkdir -p build
cd build

# Generate build files with CMake
echo "Generating build files..."
cmake ..

# Compile the application
echo "Compiling application..."
make

if [ $? -eq 0 ]; then
    echo "Application built successfully!"
    echo "Executable location: build/qornix_web"
else
    echo "Build failed!"
    exit 1
fi

cd ..
