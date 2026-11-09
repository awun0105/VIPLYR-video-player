find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavutil libswscale libavfilter)
find_package(Threads REQUIRED)

add_library(viplyr_core STATIC
    src/AudioTempoFilter.cpp
    src/CodecUtil.cpp
    src/Frame.cpp
    src/FrameReader.cpp
    src/Player.cpp
    src/Playlist.cpp
)
target_include_directories(viplyr_core PUBLIC include)
target_link_libraries(viplyr_core PRIVATE PkgConfig::FFMPEG Threads::Threads)
target_compile_options(viplyr_core PRIVATE -Wall -Wextra)
set_target_properties(viplyr_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(VIPLYR_BUILD_TESTS)
    add_executable(frame_dump examples/frame_dump.cpp)
    target_link_libraries(frame_dump PRIVATE viplyr_core)

    find_program(FFMPEG_EXECUTABLE ffmpeg)
    if(FFMPEG_EXECUTABLE)
        # A 3 s, 30 fps clip with B-frames, one keyframe per second and a sine tone.
        set(TEST_CLIP ${CMAKE_CURRENT_BINARY_DIR}/test_clip.mp4)
        add_custom_command(OUTPUT ${TEST_CLIP}
            COMMAND ${FFMPEG_EXECUTABLE} -y -loglevel error
                -f lavfi -i testsrc2=size=320x240:rate=30
                -f lavfi -i sine=frequency=440:sample_rate=44100
                -t 3 -c:v libx264 -g 30 -bf 2 -pix_fmt yuv420p -c:a aac ${TEST_CLIP}
            VERBATIM)
        add_executable(core_tests tests/core_tests.cpp ${TEST_CLIP})
        target_link_libraries(core_tests PRIVATE viplyr_core Threads::Threads)
        add_test(NAME core_tests COMMAND core_tests ${TEST_CLIP})
    endif()
endif()
