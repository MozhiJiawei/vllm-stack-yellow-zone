# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
# ubs-virt-ovs is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
add_library(ubs-virt-ovs-core STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/services/urma/urma_service.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ipc/server/auth_manager.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ipc/server/connection.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ipc/server/server.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ipc/server/thread_pool.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ipc/server/dispatcher.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ipc/core/service_base.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/logger/logger.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ubse/client_library.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ubse/urma_utility.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/config/config_manager.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/config/config_module.cpp
)

target_include_directories(ubs-virt-ovs-core
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/common
        ${CMAKE_CURRENT_SOURCE_DIR}/ipc
        ${CMAKE_CURRENT_SOURCE_DIR}/ipc/server
        ${CMAKE_CURRENT_SOURCE_DIR}/ipc/code
        ${CMAKE_CURRENT_SOURCE_DIR}/ipc/core
        ${CMAKE_CURRENT_SOURCE_DIR}/ipc/protocol
        ${CMAKE_CURRENT_SOURCE_DIR}/services
        ${CMAKE_CURRENT_SOURCE_DIR}/services/urma
        ${CMAKE_CURRENT_SOURCE_DIR}/logger
        ${CMAKE_CURRENT_SOURCE_DIR}/serde
        ${CMAKE_CURRENT_SOURCE_DIR}/ubse
        ${CMAKE_CURRENT_SOURCE_DIR}/config

)

add_executable(ubs-virt-ovs
        main.cpp
)

# -----------------------------
# Find libboundscheck header files and libraries
# -----------------------------
find_path(LIBBOUNDCHECK_INCLUDE_DIR
        NAMES securec.h
        PATHS /usr/include
        NO_DEFAULT_PATH
)

find_library(LIBBOUNDCHECK_SHARED_LIBRARY
        NAMES libboundscheck.so boundscheck
        PATHS /usr/lib64 /usr/lib
        NO_DEFAULT_PATH
)

# -----------------------------
# Verify if found
# -----------------------------
if(NOT LIBBOUNDCHECK_INCLUDE_DIR OR NOT LIBBOUNDCHECK_SHARED_LIBRARY)
    message(FATAL_ERROR
            "libboundscheck not found! Please install the RPM package:\n"
            "  sudo rpm -ivh libboundscheck-v1.1.11-6.oe2403.aarch64.rpm\n"
            "Or via yum/dnf:\n"
            "  sudo yum install libboundscheck\n"
            "Expected:\n"
            "  Header: /usr/include/securec.h\n"
            "  Shared: /usr/lib64/libboundscheck.so"
    )
endif()

message(STATUS "Found libboundscheck headers: ${LIBBOUNDCHECK_INCLUDE_DIR}")
message(STATUS "Found libboundscheck shared: ${LIBBOUNDCHECK_SHARED_LIBRARY}")

target_link_libraries(ubs-virt-ovs PRIVATE
        boundscheck
        -Wl,--whole-archive ubs-virt-ovs-core -Wl,--no-whole-archive
)

install(TARGETS ubs-virt-ovs
        RUNTIME DESTINATION bin
)

if (BUILD_TESTS)
    SET(TEST_BIN_DIR "${CMAKE_BINARY_DIR}/test")
    target_compile_definitions(ubs-virt-ovs-core
            PRIVATE DISABLE_SERVICE_AUTO_REGISTER
    )
    target_compile_options(ubs-virt-ovs-core PRIVATE --coverage -O0 -g)
    target_link_libraries(ubs-virt-ovs-core PUBLIC --coverage)
    set_target_properties(ubs-virt-ovs-core PROPERTIES OUTPUT_NAME "ubs-virt-ovs-core")
    if (COVERAGE)
        add_custom_target(coverage
                COMMAND ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR}
                COMMAND ${TEST_BIN_DIR}/virt_ut --gtest_output=xml:${CMAKE_CURRENT_BINARY_DIR}/coverage/test_detail.xml
                COMMAND lcov --capture --rc lcov_branch_coverage=1 --rc lcov_excl_br_line='.*__cxa_throw.*|.*__cxa_allocate_exception.*|.*__cxa_rethrow.*|.*std::.*' --directory ${CMAKE_BINARY_DIR} --directory ${CMAKE_CURRENT_BINARY_DIR} --output-file coverage.info
                COMMAND lcov --rc lcov_branch_coverage=1 --rc lcov_excl_br_line='.*__cxa_throw.*|.*__cxa_allocate_exception.*|.*__cxa_rethrow.*|.*std::.*' --remove coverage.info '/usr/*' '*/deps/*' '*/test/*' --output-file coverage.info
                COMMAND genhtml --ignore-errors source --branch-coverage --rc lcov_branch_coverage=1 coverage.info --output-directory coverage_html
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        )
    endif ()

endif ()