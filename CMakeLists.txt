cmake_minimum_required(VERSION 3.16)
project(Viplyr VERSION 1.0.0 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VIPLYR_BUILD_APP "Build the Qt6 desktop player" ON)
option(VIPLYR_BUILD_TESTS "Build the core tests and examples" ON)
option(VIPLYR_BUILD_PYTHON "Build the Python module (needs pybind11)" ON)

if(VIPLYR_BUILD_TESTS)
    enable_testing()
endif()

# The playback engine: depends on FFmpeg only, usable without Qt.
add_subdirectory(core)

if(VIPLYR_BUILD_APP)
    add_subdirectory(app)
endif()

if(VIPLYR_BUILD_PYTHON)
    add_subdirectory(python)
endif()

# ---------------------------------------------------------------- packaging
# `cmake --install build` or `cpack --config build/CPackConfig.cmake` (.deb, .tar.gz).
if(VIPLYR_BUILD_APP)
    include(GNUInstallDirs)
    install(TARGETS Viplyr RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/viplyr.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES icon/viplyr_icon.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps
            RENAME viplyr.png)

    set(CPACK_PACKAGE_NAME viplyr)
    set(CPACK_PACKAGE_VENDOR "Viplyr")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Video player and editor with GPU decoding")
    set(CPACK_PACKAGE_CONTACT "Lam Quang Anh Quan")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/awun0105/VIPLYR-video-player")
    set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)  # dependencies read from the linked libraries
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "ffmpeg")  # exports run the ffmpeg command
    set(CPACK_DEBIAN_PACKAGE_SECTION video)
    set(CPACK_STRIP_FILES ON)
    include(CPack)
endif()
