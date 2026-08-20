include(CMakeFindDependencyMacro)
find_dependency(ZLIB)

if(NOT TARGET ion_core)
    add_library(ion_core STATIC IMPORTED)
    set_target_properties(ion_core PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../../include"
        INTERFACE_LINK_LIBRARIES "ZLIB::ZLIB")
    set_target_properties(ion_core PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/../../libion_core.a")
endif()

if(APPLE AND NOT TARGET ion_platform_macos)
    add_library(ion_platform_macos STATIC IMPORTED)
    set_target_properties(ion_platform_macos PROPERTIES
        INTERFACE_LINK_LIBRARIES
            "-framework Cocoa;-framework GameController;-framework CoreHaptics;-framework Metal;-framework QuartzCore;-framework OpenGL")
    set_target_properties(ion_platform_macos PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/../../libion_platform_macos.a")
endif()

if(WIN32 AND NOT TARGET ion_platform_windows)
    add_library(ion_platform_windows STATIC IMPORTED)
    set_target_properties(ion_platform_windows PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/../../libion_platform_windows.a")
endif()

if(UNIX AND NOT APPLE AND NOT TARGET ion_platform_linux)
    add_library(ion_platform_linux STATIC IMPORTED)
    set_target_properties(ion_platform_linux PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/../../libion_platform_linux.a")
    find_library(X11_LIBRARY NAMES X11)
    if(X11_LIBRARY)
        set_target_properties(ion_platform_linux PROPERTIES
            INTERFACE_LINK_LIBRARIES "${X11_LIBRARY}")
    endif()
endif()
