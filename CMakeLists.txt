cmake_minimum_required(VERSION 3.16)

project(ion VERSION 0.1.0 LANGUAGES CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ION_BUILD_EXAMPLES "Build example programs" ON)
option(ION_BUILD_TESTS "Build tests" ON)

add_library(ion_core STATIC
    core/Application.cpp
)
target_include_directories(ion_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(ion_core PUBLIC
    ION_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    ION_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    ION_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

if(APPLE)
    add_library(ion_platform_macos OBJECT
        platform/macos/WindowImpl.mm
    )
    set_source_files_properties(platform/macos/WindowImpl.mm PROPERTIES LANGUAGE OBJCXX)
    target_include_directories(ion_platform_macos PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_target_properties(ion_platform_macos PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(ion_platform_macos INTERFACE "-framework Cocoa")
elseif(WIN32)
    add_library(ion_platform_windows OBJECT
        platform/windows/WindowImpl.cpp
    )
    target_include_directories(ion_platform_windows PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
elseif(UNIX)
    add_library(ion_platform_linux OBJECT
        platform/linux/WindowImpl.cpp
    )
    target_include_directories(ion_platform_linux PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()

if(ION_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(ION_BUILD_TESTS)
    enable_testing()
    add_executable(window_test tests/window_test.cpp)
    target_link_libraries(window_test PRIVATE ion_core)
    if(APPLE)
        target_link_libraries(window_test PRIVATE ion_platform_macos "-framework Cocoa")
    elseif(WIN32)
        target_link_libraries(window_test PRIVATE ion_platform_windows)
    elseif(UNIX)
        target_link_libraries(window_test PRIVATE ion_platform_linux)
    endif()
endif()
