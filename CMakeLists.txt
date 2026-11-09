cmake_minimum_required(VERSION 3.16)
project(Viplyr LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VIPLYR_BUILD_APP "Build the Qt6 desktop player" ON)
option(VIPLYR_BUILD_TESTS "Build the core tests and examples" ON)

if(VIPLYR_BUILD_TESTS)
    enable_testing()
endif()

# The playback engine: depends on FFmpeg only, usable without Qt.
add_subdirectory(core)

if(VIPLYR_BUILD_APP)
    add_subdirectory(app)
endif()
