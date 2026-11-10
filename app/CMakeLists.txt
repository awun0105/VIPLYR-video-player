set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets OpenGLWidgets Multimedia Concurrent DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBASS REQUIRED IMPORTED_TARGET libass)  # styled (ASS) subtitles

add_executable(Viplyr
    main.cpp
    AudioOutput.cpp
    ControlBar.cpp
    CudaGlInterop.cpp
    DmaBufImporter.cpp
    ExportCommand.cpp
    ExportDialog.cpp
    ExportJob.cpp
    Icons.cpp
    IdleInhibitor.cpp
    JumpSlider.cpp
    MainWindow.cpp
    MediaFiles.cpp
    MediaProbe.cpp
    OverlayTrack.cpp
    PlayerController.cpp
    PlaylistPanel.cpp
    StartPanel.cpp
    SubtitleRenderer.cpp
    SubtitleTrack.cpp
    ThumbnailPopup.cpp
    ThumbnailProvider.cpp
    VideoWidget.cpp
)
target_link_libraries(Viplyr PRIVATE
    viplyr_core
    Qt6::Widgets Qt6::OpenGLWidgets Qt6::Multimedia Qt6::Concurrent Qt6::DBus PkgConfig::LIBASS ${CMAKE_DL_LIBS})
target_compile_options(Viplyr PRIVATE -Wall -Wextra)
# Keep the binary at build/Viplyr, where the Makefile and the desktop entry expect it.
set_target_properties(Viplyr PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(VIPLYR_BUILD_TESTS)
    # Real audio-backend regression test. Run by hand: it needs an output device,
    # but mutes its own stream and never changes the system volume.
    add_executable(audio_output_tests tests/audio_output_tests.cpp AudioOutput.cpp)
    target_include_directories(audio_output_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(audio_output_tests PRIVATE viplyr_core Qt6::Core Qt6::Multimedia)

    # Drives the player in a real window (seek, step, snapshot, long playback). Needs a
    # display, so it is run by hand: build/app/display_tests <video> [seconds].
    add_executable(display_tests tests/display_tests.cpp
        AudioOutput.cpp CudaGlInterop.cpp DmaBufImporter.cpp OverlayTrack.cpp
        PlayerController.cpp SubtitleRenderer.cpp SubtitleTrack.cpp VideoWidget.cpp)
    target_include_directories(display_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(display_tests PRIVATE viplyr_core
        Qt6::Widgets Qt6::OpenGLWidgets Qt6::Multimedia Qt6::Concurrent PkgConfig::LIBASS ${CMAKE_DL_LIBS})

    find_program(FFMPEG_EXECUTABLE ffmpeg)
    if(FFMPEG_EXECUTABLE)
        # Runs real ffmpeg exports built by ExportCommand and checks the results.
        add_executable(export_tests tests/export_tests.cpp ExportCommand.cpp ExportJob.cpp)
        target_include_directories(export_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(export_tests PRIVATE viplyr_core Qt6::Core Qt6::Gui Qt6::Concurrent)
        add_test(NAME export_tests COMMAND export_tests
                 ${CMAKE_BINARY_DIR}/core/test_clip.mp4 ${CMAKE_BINARY_DIR}/core/test_tracks.mkv)
        add_dependencies(export_tests core_tests)  # the clips are generated for core_tests
    endif()
endif()
