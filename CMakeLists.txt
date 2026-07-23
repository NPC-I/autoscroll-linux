cmake_minimum_required(VERSION 3.16)
project(autoscroll VERSION 1.0.0 LANGUAGES C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(BUILD_TESTS     "Build unit tests" ON)
option(BUILD_DOCS      "Build man pages"  OFF)

add_subdirectory(src)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
