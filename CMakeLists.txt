cmake_minimum_required(VERSION 3.14)
project(nexus C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Warnings
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# Debug by default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_subdirectory(lib)

enable_testing()
add_subdirectory(lib/test)
