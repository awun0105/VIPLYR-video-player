#!/bin/bash

# This command ensures the script exits immediately if any error occurs
set -e

echo "🧹 1. Cleaning up old cache..."
rm -rf build

echo "⚙️ 2. Configuring CMake with Ninja (Release)..."
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release

echo "🚀 3. Compiling..."
ninja -C build

echo "========================================="
echo "✅ SUCCESS! The application is ready."
echo "👉 Run the following command to open: ./build/Viplyr"
echo "========================================="
