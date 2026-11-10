find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavutil libswscale libavfilter libavdevice)
find_package(Threads REQUIRED)

add_library(viplyr_core STATIC
    src/AudioTempoFilter.cpp
    src/CodecUtil.cpp
    src/Deinterlacer.cpp
    src/Frame.cpp
    src/FrameReader.cpp
    src/Logging.cpp
    src/Player.cpp
    src/Playlist.cpp
    src/SubtitleReader.cpp
    src/VideoRecorder.cpp
)
target_include_directories(viplyr_core PUBLIC include)
target_link_libraries(viplyr_core PRIVATE PkgConfig::FFMPEG Threads::Threads)
target_compile_options(viplyr_core PRIVATE -Wall -Wextra)
# FFmpeg's C headers need this when they are the first include of a C++ file.
target_compile_definitions(viplyr_core PRIVATE __STDC_CONSTANT_MACROS)
set_target_properties(viplyr_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(VIPLYR_BUILD_TESTS)
    add_executable(frame_dump examples/frame_dump.cpp)
    target_link_libraries(frame_dump PRIVATE viplyr_core)
    add_executable(decode_bench examples/decode_bench.cpp)
    target_link_libraries(decode_bench PRIVATE viplyr_core)

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
        # Interlaced, non-square pixels, two audio tracks (stereo and 5.1, with sound only in
        # the front-left and centre channels), an embedded subtitle track and two chapters.
        set(TEST_SUBTITLES ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_subtitles.srt)
        set(TEST_CHAPTERS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_chapters.ffmeta)
        set(TEST_TRACKS_CLIP ${CMAKE_CURRENT_BINARY_DIR}/test_tracks.mkv)
        add_custom_command(OUTPUT ${TEST_TRACKS_CLIP}
            COMMAND ${FFMPEG_EXECUTABLE} -y -loglevel error
                -f lavfi -i testsrc2=size=360x288:rate=25
                -f lavfi -i sine=frequency=440:sample_rate=44100
                -f lavfi -i "aevalsrc=sin(880*2*PI*t)|0|0.5*sin(440*2*PI*t)|0|0|0:s=44100:c=5.1"
                -i ${TEST_SUBTITLES}
                -f ffmetadata -i ${TEST_CHAPTERS}
                -t 2 -map 0:v -map 1:a -map 2:a -map 3:s -map_chapters 4
                -c:v libx264 -flags +ilme+ildct -top 1 -aspect 16:9 -pix_fmt yuv420p
                -c:a aac -c:s srt
                -metadata:s:a:0 language=eng -metadata:s:a:1 language=vie
                -metadata:s:s:0 language=vie ${TEST_TRACKS_CLIP}
            DEPENDS ${TEST_SUBTITLES} ${TEST_CHAPTERS} VERBATIM)
        add_executable(core_tests tests/core_tests.cpp ${TEST_CLIP} ${TEST_TRACKS_CLIP})
        # FFmpeg directly too: one test writes its own bitmap subtitle file.
        target_link_libraries(core_tests PRIVATE viplyr_core Threads::Threads PkgConfig::FFMPEG)
        add_test(NAME core_tests COMMAND core_tests ${TEST_CLIP} ${TEST_TRACKS_CLIP}
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_styled.ass)
        set_tests_properties(core_tests PROPERTIES TIMEOUT 120)
    endif()
endif()
