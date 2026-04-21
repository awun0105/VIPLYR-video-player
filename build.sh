#!/bin/bash

# Lệnh này giúp script dừng lại ngay lập tức nếu có bất kỳ lỗi nào xảy ra
set -e

echo "🧹 1. Đang dọn dẹp bộ đệm cũ..."
rm -rf build

echo "📁 2. Đang tạo môi trường mới..."
mkdir build
cd build

echo "⚙️ 3. Đang cấu hình CMake với Ninja..."
cmake -G Ninja ..

echo "🚀 4. Đang biên dịch..."
ninja

echo "========================================="
echo "✅ HOÀN TẤT! Ứng dụng đã sẵn sàng."
echo "👉 Hãy chạy lệnh sau để mở: ./build/Viplyr"
echo "========================================="