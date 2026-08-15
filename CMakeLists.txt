cmake_minimum_required(VERSION 3.16)

project(ion VERSION 0.2.0 LANGUAGES CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ION_BUILD_EXAMPLES "Build example programs" ON)
option(ION_BUILD_TESTS "Build tests" ON)
option(ION_ENABLE_VULKAN "Enable the Vulkan render backend" ON)

add_library(ion_core STATIC
    core/Application.cpp
    core/Camera.cpp
    core/Camera2D.cpp
    core/Config.cpp
    core/Engine.cpp
    core/GltfModel.cpp
    core/Image.cpp
    core/Input.cpp
    core/Light.cpp
    core/Log.cpp
    core/Memory.cpp
    core/Mesh.cpp
    core/Model.cpp
    core/ObjModel.cpp
    core/ParticleSystem.cpp
    core/PerformanceOverlay.cpp
    core/Render.cpp
    core/Skybox.cpp
    core/SpriteAnimation.cpp
    core/SpriteBatch.cpp
    core/Svg.cpp
    core/TextureAtlas.cpp
    core/Text.cpp
    core/Tilemap.cpp
)
target_include_directories(ion_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
find_package(ZLIB REQUIRED)
target_link_libraries(ion_core PUBLIC ZLIB::ZLIB)
target_compile_definitions(ion_core PUBLIC
    ION_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    ION_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    ION_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    ION_MEMORY_TRACKING
)

if(ION_ENABLE_VULKAN AND NOT APPLE)
    # Locate the Vulkan loader. Prefer the dev symlink (libvulkan.so), but
    # also accept the versioned runtime library that ships with drivers.
    set(ION_VULKAN_LOADER "" CACHE FILEPATH "Path to the Vulkan loader library")
    if(NOT ION_VULKAN_LOADER)
        find_library(ION_VULKAN_FIND NAMES vulkan vulkan-1)
        if(ION_VULKAN_FIND)
            set(ION_VULKAN_LOADER "${ION_VULKAN_FIND}")
        endif()
    endif()
    if(NOT ION_VULKAN_LOADER AND UNIX)
        find_library(ION_VULKAN_SONAME NAMES libvulkan.so.1
            PATHS /usr/lib64 /usr/lib /lib64 /lib NO_DEFAULT_PATH)
        if(ION_VULKAN_SONAME)
            set(ION_VULKAN_LOADER "${ION_VULKAN_SONAME}")
        endif()
    endif()
    if(ION_VULKAN_LOADER)
        target_sources(ion_core PRIVATE core/RenderVulkan.cpp)
        target_include_directories(ion_core PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Vulkan-Headers/include)
        target_compile_definitions(ion_core PRIVATE ION_ENABLE_VULKAN)
        target_link_libraries(ion_core PUBLIC ${ION_VULKAN_LOADER})
        if(UNIX)
            find_library(ION_DL_LIBRARY NAMES dl)
            if(ION_DL_LIBRARY)
                target_link_libraries(ion_core PUBLIC ${ION_DL_LIBRARY})
            endif()
        endif()
        message(STATUS "Vulkan backend enabled (loader: ${ION_VULKAN_LOADER})")
    else()
        message(WARNING "Vulkan loader not found; ION_ENABLE_VULKAN ignored")
    endif()
endif()

if(APPLE)
    add_library(ion_platform_macos OBJECT
        platform/macos/WindowImpl.mm
        platform/macos/Paths.mm
        platform/macos/InputImpl.mm
        platform/macos/RenderMetal.mm
        platform/macos/RenderGL.mm
    )
    set_source_files_properties(
        platform/macos/WindowImpl.mm
        platform/macos/Paths.mm
        platform/macos/InputImpl.mm
        platform/macos/RenderMetal.mm
        platform/macos/RenderGL.mm
        PROPERTIES LANGUAGE OBJCXX)
    target_include_directories(ion_platform_macos PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_target_properties(ion_platform_macos PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(ion_platform_macos INTERFACE
        "-framework Cocoa"
        "-framework GameController"
        "-framework CoreHaptics"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework OpenGL")
elseif(WIN32)
    add_library(ion_platform_windows OBJECT
        platform/windows/WindowImpl.cpp
        platform/windows/Paths.cpp
        platform/windows/InputImpl.cpp
    )
    target_include_directories(ion_platform_windows PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
elseif(UNIX)
    add_library(ion_platform_linux OBJECT
        platform/linux/WindowImpl.cpp
        platform/linux/Paths.cpp
        platform/linux/InputImpl.cpp
    )
    target_include_directories(ion_platform_linux PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    find_library(X11_LIBRARY NAMES X11)
    if(X11_LIBRARY)
        target_link_libraries(ion_platform_linux INTERFACE ${X11_LIBRARY})
    endif()
endif()

if(ION_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(ION_BUILD_TESTS)
    enable_testing()
    add_executable(header_check tests/header_check.cpp)
    target_link_libraries(header_check PRIVATE ion_core)
    if(APPLE)
        target_link_libraries(header_check PRIVATE ion_platform_macos "-framework Cocoa")
    elseif(WIN32)
        target_link_libraries(header_check PRIVATE ion_platform_windows)
    elseif(UNIX)
        target_link_libraries(header_check PRIVATE ion_platform_linux)
    endif()

    add_executable(window_test tests/window_test.cpp)
    target_link_libraries(window_test PRIVATE ion_core)
    if(APPLE)
        target_link_libraries(window_test PRIVATE ion_platform_macos "-framework Cocoa")
    elseif(WIN32)
        target_link_libraries(window_test PRIVATE ion_platform_windows)
    elseif(UNIX)
        target_link_libraries(window_test PRIVATE ion_platform_linux)
    endif()

    add_executable(core_test tests/core_test.cpp)
    target_link_libraries(core_test PRIVATE ion_core)
    if(APPLE)
        target_link_libraries(core_test PRIVATE ion_platform_macos "-framework Cocoa")
    elseif(WIN32)
        target_link_libraries(core_test PRIVATE ion_platform_windows)
    elseif(UNIX)
        target_link_libraries(core_test PRIVATE ion_platform_linux)
    endif()
    add_test(NAME core_test COMMAND core_test)

    add_executable(input_test tests/input_test.cpp)
    target_link_libraries(input_test PRIVATE ion_core)
    add_test(NAME input_test COMMAND input_test)

    add_executable(render_test tests/render_test.cpp)
    target_link_libraries(render_test PRIVATE ion_core)
    if(APPLE)
        target_link_libraries(render_test PRIVATE ion_platform_macos "-framework Cocoa")
    elseif(WIN32)
        target_link_libraries(render_test PRIVATE ion_platform_windows)
    elseif(UNIX)
        target_link_libraries(render_test PRIVATE ion_platform_linux)
    endif()
    add_test(NAME render_test COMMAND render_test)

    add_executable(2d_test tests/2d_test.cpp)
    target_link_libraries(2d_test PRIVATE ion_core)
    if(APPLE)
        target_link_libraries(2d_test PRIVATE ion_platform_macos "-framework Cocoa")
    elseif(WIN32)
        target_link_libraries(2d_test PRIVATE ion_platform_windows)
    elseif(UNIX)
        target_link_libraries(2d_test PRIVATE ion_platform_linux)
    endif()
    add_test(NAME 2d_test COMMAND 2d_test)

    add_executable(model_test tests/model_test.cpp)
    target_link_libraries(model_test PRIVATE ion_core)
    if(APPLE)
        target_link_libraries(model_test PRIVATE ion_platform_macos "-framework Cocoa")
    elseif(WIN32)
        target_link_libraries(model_test PRIVATE ion_platform_windows)
    elseif(UNIX)
        target_link_libraries(model_test PRIVATE ion_platform_linux)
    endif()
    add_test(NAME model_test COMMAND model_test)
endif()
