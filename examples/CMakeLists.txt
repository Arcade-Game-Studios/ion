if(APPLE)
    add_executable(basic_example basic/main.cpp)
    target_link_libraries(basic_example PRIVATE ion_core ion_platform_macos "-framework Cocoa")

    add_executable(multi_window multi_window/main.cpp)
    target_link_libraries(multi_window PRIVATE ion_core ion_platform_macos "-framework Cocoa")

    add_executable(input_example input/main.cpp)
    target_link_libraries(input_example PRIVATE ion_core ion_platform_macos "-framework Cocoa")

    add_executable(render_example render/main.cpp)
    target_link_libraries(render_example PRIVATE ion_core ion_platform_macos "-framework Cocoa")

    add_executable(2d_example 2d/main.cpp)
    target_link_libraries(2d_example PRIVATE ion_core ion_platform_macos "-framework Cocoa")
elseif(WIN32)
    add_executable(basic_example basic/main.cpp)
    target_link_libraries(basic_example PRIVATE ion_core ion_platform_windows)

    add_executable(multi_window multi_window/main.cpp)
    target_link_libraries(multi_window PRIVATE ion_core ion_platform_windows)

    add_executable(input_example input/main.cpp)
    target_link_libraries(input_example PRIVATE ion_core ion_platform_windows)

    add_executable(2d_example 2d/main.cpp)
    target_link_libraries(2d_example PRIVATE ion_core ion_platform_windows)
elseif(UNIX)
    add_executable(basic_example basic/main.cpp)
    target_link_libraries(basic_example PRIVATE ion_core ion_platform_linux)

    add_executable(multi_window multi_window/main.cpp)
    target_link_libraries(multi_window PRIVATE ion_core ion_platform_linux)

    add_executable(input_example input/main.cpp)
    target_link_libraries(input_example PRIVATE ion_core ion_platform_linux)

    add_executable(2d_example 2d/main.cpp)
    target_link_libraries(2d_example PRIVATE ion_core ion_platform_linux)
endif()
