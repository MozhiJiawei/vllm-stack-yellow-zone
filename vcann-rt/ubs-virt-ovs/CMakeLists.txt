cmake_minimum_required(VERSION 3.15)
project(UbsVirtOvs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
set(DEPS_DIR ${PROJECT_SOURCE_DIR}/deps)
set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)

option(BUILD_TESTS "Build unit tests" OFF)

add_subdirectory(src)

if (BUILD_TESTS)
    add_subdirectory(test)
    enable_testing()
endif ()