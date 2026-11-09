set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets OpenGLWidgets Multimedia Concurrent DBus)

add_executable(Viplyr
    main.cpp
    AudioOutput.cpp
    ControlBar.cpp
    ExportCommand.cpp
    ExportDialog.cpp
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
    SubtitleTrack.cpp
    ThumbnailPopup.cpp
    ThumbnailProvider.cpp
    VideoWidget.cpp
)
target_link_libraries(Viplyr PRIVATE
    viplyr_core
    Qt6::Widgets Qt6::OpenGLWidgets Qt6::Multimedia Qt6::Concurrent Qt6::DBus)
target_compile_options(Viplyr PRIVATE -Wall -Wextra)
# Keep the binary at build/Viplyr, where the Makefile and the desktop entry expect it.
set_target_properties(Viplyr PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(VIPLYR_BUILD_TESTS)
    find_program(FFMPEG_EXECUTABLE ffmpeg)
    if(FFMPEG_EXECUTABLE)
        # Runs real ffmpeg exports built by ExportCommand and checks the results.
        add_executable(export_tests tests/export_tests.cpp ExportCommand.cpp)
        target_include_directories(export_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(export_tests PRIVATE viplyr_core Qt6::Core Qt6::Gui)
        add_test(NAME export_tests COMMAND export_tests
                 ${CMAKE_BINARY_DIR}/core/test_clip.mp4 ${CMAKE_BINARY_DIR}/core/test_tracks.mkv)
        add_dependencies(export_tests core_tests)  # the clips are generated for core_tests
    endif()
endif()
