include(${PROJECT_SOURCE_DIR}/cmake/gtest.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/mockcpp.cmake)
link_directories(${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})

set(UT_BINARY ${CMAKE_PROJECT_NAME}_ut)
set(TEST_DIR ${PROJECT_SOURCE_DIR}/test)
message(STATUS "CMAKE_BINARY_DIR  ${CMAKE_BINARY_DIR}")
message(STATUS "test dir ${TEST_DIR}")

configure_file (
        "${PROJECT_SOURCE_DIR}/test/config.h.in"
        "${CMAKE_BINARY_DIR}/include/rack_test/config.h"
)

# UT Document
file(GLOB_RECURSE TEST_SOURCES LIST_DIRECTORIES false *.h *.cpp)

add_executable(${UT_BINARY} EXCLUDE_FROM_ALL ${TEST_SOURCES})

# Breaking control
target_compile_options(${UT_BINARY} PRIVATE -fno-access-control ${DEBUG_FLAGS} -w)
# Test Piece Library
target_link_libraries(${UT_BINARY} PUBLIC gtest pthread gcov mockcpp)
# Business Static Library
target_link_libraries(${UT_BINARY} PUBLIC
        cmd_serialize
        cli_parse
        socket_server
        socket_client
        acquire
        api
        vas_daemon_register
        util
        logger
        cluster_sched
        looper
        vas_ctl_register
        security
)
set_target_properties(${UT_BINARY} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/test)

set(TEST_EXTRA_ARGS $ENV{TRANS_PARAMS})

set(TEST_COMMAND_BASE "${PROJECT_SOURCE_DIR}/test/${UT_BINARY}")

if(TEST_EXTRA_ARGS)
    set(TEST_FULL_COMMAND ${TEST_COMMAND_BASE} ${TEST_EXTRA_ARGS})
else()
    set(TEST_FULL_COMMAND ${TEST_COMMAND_BASE} --gtest_output=xml:${PROJECT_SOURCE_DIR}/build/test_detail.xml)
endif()

if(TEST_EXTRA_ARGS)
    add_custom_target(build_test COMMAND export "VIRT_CURL_PATH=/usr/lib64"
            && ${TEST_FULL_COMMAND}
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/test)
else()
    add_custom_target(build_test COMMAND export "VIRT_CURL_PATH=/usr/lib64"
            && ${TEST_FULL_COMMAND}
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/test)
endif()

add_dependencies(build_test ${UT_BINARY})

include(GoogleTest)
gtest_add_tests(TARGET ${UT_BINARY})

add_subdirectory(vasd)
add_subdirectory(cli)