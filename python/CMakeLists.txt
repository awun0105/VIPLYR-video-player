find_package(Python3 COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG QUIET)
if(NOT pybind11_FOUND OR NOT Python3_FOUND)
    message(STATUS "Python module skipped: install pybind11-dev and python3-dev to build it")
    return()
endif()

# Produces build/python/viplyr.cpython-*.so; `import viplyr` with PYTHONPATH=build/python.
pybind11_add_module(viplyr_python viplyr_python.cpp)
set_target_properties(viplyr_python PROPERTIES OUTPUT_NAME viplyr)
target_link_libraries(viplyr_python PRIVATE viplyr_core)

if(VIPLYR_BUILD_TESTS)
    add_test(NAME python_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_viplyr.py
                     ${CMAKE_BINARY_DIR}/core/test_clip.mp4 ${CMAKE_BINARY_DIR}/core/test_tracks.mkv)
    set_tests_properties(python_tests PROPERTIES ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}")
endif()
