cmake_minimum_required(VERSION 3.16)
project(my_game LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED ion_DIR)
    set(ion_DIR "$ENV{HOME}/.ion/lib/cmake/ion" CACHE PATH "Ion engine install path")
endif()

find_package(ion REQUIRED)

add_executable(my_game main.cpp)
target_link_libraries(my_game PRIVATE ion_core ion_platform_macos)
