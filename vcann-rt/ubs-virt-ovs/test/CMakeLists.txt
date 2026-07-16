include(${CMAKE_SOURCE_DIR}/script/mockcpp.cmake)
include(${CMAKE_SOURCE_DIR}/script/gtest.cmake)

file(GLOB_RECURSE TEST_FILES "*.cpp")
file(GLOB_RECURSE ALL_SRC_DIRS LIST_DIRECTORIES true ${CMAKE_SOURCE_DIR}/src/*)
set(SRC_INCLUDE_DIRS "")
foreach (dir ${ALL_SRC_DIRS})
    IF (IS_DIRECTORY ${dir})
        list(APPEND SRC_INCLUDE_DIRS ${dir})
    endif ()
endforeach ()

foreach (file ${TEST_FILES})
    IF (file MATCHES "cmake-build-debug")
        list(REMOVE_ITEM TEST_FILES ${file})
    endif ()
endforeach ()

configure_file(
        ${CMAKE_SOURCE_DIR}/test/ut_dir.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/ut_dir.h
)

include_directories(
        ${CMAKE_CURRENT_BINARY_DIR}
        ${SRC_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/test
        ${CMAKE_SOURCE_DIR}/deps
        ${CMAKE_SOURCE_DIR}/deps/mockcpp/include
        ${CMAKE_CURRENT_BINARY_DIR}/../deps/mockcpp/include
)

link_directories(
        ${CMAKE_SOURCE_DIR}/deps/mockcpp/lib
)

add_executable(virt_ut ${TEST_FILES})

target_compile_options(virt_ut PRIVATE -fno-access-control -w)

target_link_libraries(virt_ut
        PRIVATE ubs-virt-ovs-core
        PRIVATE GTest::gmock_main
        PRIVATE gmock gmock_main
        PRIVATE mockcpp
        PRIVATE pthread
        PRIVATE boundscheck
)

if (COVERAGE)
    target_compile_options(virt_ut PRIVATE --coverage -O0 -g)
    target_link_libraries(virt_ut PUBLIC --coverage)
endif ()

include(GoogleTest)
add_test(NAME virt_ut COMMAND virt_ut)
