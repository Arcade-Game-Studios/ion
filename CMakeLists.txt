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
option(ION_WARNINGS "Compile Ion targets with extra warnings" ON)
option(ION_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(ION_SANITIZE "Build with AddressSanitizer + UBSan (GCC/Clang)" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

if(ION_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

function(ion_apply_warnings target)
    if(NOT ION_WARNINGS)
        return()
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(ION_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
        if(ION_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

find_package(Threads REQUIRED)

add_library(ion_core STATIC
    core/Application.cpp
    core/Audio.cpp
    core/AudioMixer.cpp
    core/Camera.cpp
    core/Camera2D.cpp
    core/Config.cpp
    core/Engine.cpp
    core/Geometry.cpp
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
target_include_directories(ion_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
find_package(ZLIB REQUIRED)
target_link_libraries(ion_core PUBLIC ZLIB::ZLIB Threads::Threads)
ion_apply_warnings(ion_core)
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
    add_library(ion_platform_macos STATIC
        platform/macos/WindowImpl.mm
        platform/macos/Paths.mm
        platform/macos/InputImpl.mm
        platform/macos/RenderMetal.mm
        platform/macos/RenderGL.mm
        platform/macos/AudioImpl.cpp
    )
    set_source_files_properties(
        platform/macos/WindowImpl.mm
        platform/macos/Paths.mm
        platform/macos/InputImpl.mm
        platform/macos/RenderMetal.mm
        platform/macos/RenderGL.mm
        PROPERTIES LANGUAGE OBJCXX)
    target_include_directories(ion_platform_macos PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    # The platform layer calls back into ion_core (input hooks) and ion_core
    # calls into the platform layer (Window), so each side lists the other.
    target_link_libraries(ion_platform_macos PUBLIC ion_core)
    set(ION_PLATFORM_TARGET ion_platform_macos)
    ion_apply_warnings(ion_platform_macos)
    set_target_properties(ion_platform_macos PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(ion_platform_macos INTERFACE
        "-framework Cocoa"
        "-framework GameController"
        "-framework CoreHaptics"
        "-framework AudioToolbox"
        "-framework CoreAudio"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework OpenGL")
elseif(WIN32)
    add_library(ion_platform_windows STATIC
        platform/windows/WindowImpl.cpp
        platform/windows/Paths.cpp
        platform/windows/InputImpl.cpp
        platform/windows/AudioImpl.cpp
    )
    target_include_directories(ion_platform_windows PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    # The platform layer calls back into ion_core (input hooks) and ion_core
    # calls into the platform layer (Window), so each side lists the other.
    target_link_libraries(ion_platform_windows PUBLIC ion_core)
    target_link_libraries(ion_platform_windows PUBLIC xinput winmm)
    set(ION_PLATFORM_TARGET ion_platform_windows)
    ion_apply_warnings(ion_platform_windows)
elseif(UNIX)
    add_library(ion_platform_linux STATIC
        platform/linux/WindowImpl.cpp
        platform/linux/Paths.cpp
        platform/linux/InputImpl.cpp
        platform/linux/AudioImpl.cpp
    )
    target_include_directories(ion_platform_linux PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    # The platform layer calls back into ion_core (input hooks) and ion_core
    # calls into the platform layer (Window), so each side lists the other.
    target_link_libraries(ion_platform_linux PUBLIC ion_core)
    set(ION_PLATFORM_TARGET ion_platform_linux)
    ion_apply_warnings(ion_platform_linux)
    # dlopen for the runtime-loaded ALSA audio backend.
    target_link_libraries(ion_platform_linux PUBLIC ${CMAKE_DL_LIBS} Threads::Threads)
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

    # ion_add_test(<name> [PLATFORM] [NO_CTEST])
    #   PLATFORM  link the platform library (window/input backends)
    #   NO_CTEST  build only; the binary needs a display or user input
    function(ion_add_test name)
        cmake_parse_arguments(ARG "PLATFORM;NO_CTEST" "" "" ${ARGN})
        add_executable(${name} tests/${name}.cpp)
        target_link_libraries(${name} PRIVATE ion_core)
        if(ARG_PLATFORM)
            target_link_libraries(${name} PRIVATE ${ION_PLATFORM_TARGET})
        endif()
        ion_apply_warnings(${name})
        if(NOT ARG_NO_CTEST)
            add_test(NAME ${name} COMMAND ${name}
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        endif()
    endfunction()

    ion_add_test(header_check PLATFORM NO_CTEST)
    ion_add_test(window_test PLATFORM NO_CTEST)
    ion_add_test(core_test PLATFORM)
    ion_add_test(input_test)
    ion_add_test(render_test PLATFORM)
    ion_add_test(2d_test PLATFORM)
    ion_add_test(model_test PLATFORM)
    ion_add_test(ecs_test)
    ion_add_test(audio_test)
    ion_add_test(asteroids_test)
    ion_add_test(math_test)
    ion_add_test(image_test)
    if(UNIX AND NOT APPLE)
        ion_add_test(window_x11_test PLATFORM)
        ion_add_test(audio_device_test PLATFORM)
    endif()
endif()

include(GNUInstallDirs)

install(TARGETS ion_core
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ion)

if(APPLE)
    install(TARGETS ion_platform_macos
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
elseif(WIN32)
    install(TARGETS ion_platform_windows
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
elseif(UNIX)
    install(TARGETS ion_platform_linux
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

install(DIRECTORY include/ion DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(FILES cmake/ion-config.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ion)
