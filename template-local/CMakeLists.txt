cmake_minimum_required(VERSION 3.16)
project(my_game LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ION_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ION_BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(ion)

add_executable(my_game main.cpp)
target_link_libraries(my_game PRIVATE ion_core ion_platform_macos)
