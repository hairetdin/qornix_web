#!/bin/bash
# build_extensions.sh

echo "Building route extensions..."

# Create build directory
mkdir -p build
cd build

# Generate Makefiles with CMake
cmake ..

# Compile all extensions
make

if [ $? -eq 0 ]; then
    echo "Extensions built successfully!"

    # Copy compiled files to the root extensions directory
    find . -name "*.so" -exec cp {} .. \;
    echo "Extension libraries copied to route_extensions directory"
else
    echo "Build failed!"
    exit 1
fi

cd ../..
