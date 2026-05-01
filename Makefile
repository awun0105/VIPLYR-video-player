.PHONY: all setup build run start clean

# Mặc định gõ 'make' sẽ tự động build nhanh và chạy app
all: start

# Chỉ gọi setup.sh khi bạn muốn dọn sạch làm lại từ đầu
setup:
	./setup.sh

# Lệnh này tận dụng sức mạnh của Ninja: Chỉ build những file vừa sửa!
build:
	@echo "🚀 Đang biên dịch các thay đổi mới nhất bằng Ninja..."
	ninja -C build

# Lệnh chạy ứng dụng
run:
	@echo "🔥 Đang khởi động Viplyr..."
	./build/Viplyr

# Tự động kết hợp: Build nhanh xong thì Chạy luôn
start: build run

# Lệnh xóa nhanh nếu cần
clean:
	rm -rf build