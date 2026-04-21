cmake_minimum_required(VERSION 3.5)
project(VideoPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 COMPONENTS Widgets Multimedia MultimediaWidgets REQUIRED)

add_executable(VideoPlayer main.cpp PlayerView.cpp AppUI.cpp)
target_link_libraries(VideoPlayer PRIVATE Qt5::Widgets Qt5::Multimedia Qt5::MultimediaWidgets)