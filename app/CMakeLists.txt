set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets OpenGLWidgets Multimedia Concurrent DBus)

add_executable(Viplyr
    main.cpp
    AudioOutput.cpp
    ControlBar.cpp
    IdleInhibitor.cpp
    JumpSlider.cpp
    MainWindow.cpp
    MediaFiles.cpp
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
