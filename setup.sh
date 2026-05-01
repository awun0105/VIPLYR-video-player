#!/bin/bash

# This command ensures the script exits immediately if any error occurs
set -e

echo "🧹 1. Cleaning up old cache..."
rm -rf build

echo "📁 2. Creating a new environment..."
mkdir build
cd build

echo "⚙️ 3. Configuring CMake with Ninja..."
cmake -G Ninja ..

echo "🚀 4. Compiling..."
ninja

echo "========================================="
echo "✅ SUCCESS! The application is ready."
echo "👉 Run the following command to open: ./build/Viplyr"
echo "========================================="