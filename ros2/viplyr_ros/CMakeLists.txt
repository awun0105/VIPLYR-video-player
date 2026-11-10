cmake_minimum_required(VERSION 3.16)
project(viplyr_ros LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(rclcpp_components REQUIRED)

# The engine is built from this repository's core/ (FFmpeg only, no Qt).
set(VIPLYR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../core ${CMAKE_CURRENT_BINARY_DIR}/viplyr_core)

# One library holds the node: loadable as a component, and linked into the executable.
add_library(video_publisher_component SHARED src/video_publisher.cpp)
target_link_libraries(video_publisher_component viplyr_core
    rclcpp::rclcpp rclcpp_components::component ${sensor_msgs_TARGETS} ${std_msgs_TARGETS} ${std_srvs_TARGETS})
target_compile_options(video_publisher_component PRIVATE -Wall -Wextra)
rclcpp_components_register_node(video_publisher_component
    PLUGIN "viplyr_ros::VideoPublisher" EXECUTABLE video_publisher)

install(TARGETS video_publisher_component
    ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})

ament_package()
