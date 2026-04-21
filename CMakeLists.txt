cmake_minimum_required(VERSION 3.5)
project(VideoPlayerPro LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 COMPONENTS Widgets Multimedia MultimediaWidgets REQUIRED)

# Quét tất cả file .cpp trong thư mục src và các thư mục con
file(GLOB_RECURSE SRC_FILES "src/*.cpp")

add_executable(Viplyr ${SRC_FILES})
target_link_libraries(Viplyr PRIVATE Qt5::Widgets Qt5::Multimedia Qt5::MultimediaWidgets)