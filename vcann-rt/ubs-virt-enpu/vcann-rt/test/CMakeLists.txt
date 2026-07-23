# -----------------------------------------------------------------------------------------------------------
# Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
# ubs-virt-enpu is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
# -----------------------------------------------------------------------------------------------------------

add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=1)

include(${PROJECT_SOURCE_DIR}/scripts/cmake/gtest.cmake)
include(${PROJECT_SOURCE_DIR}/scripts/cmake/mockcpp.cmake)

set(VNPU_SRC_FILES
    ${PROJECT_SOURCE_DIR}/src/core_limiter.c
    ${PROJECT_SOURCE_DIR}/src/deadlock_trace.c
    ${PROJECT_SOURCE_DIR}/src/mem_limiter.c
    ${PROJECT_SOURCE_DIR}/src/npu_manager.c
    ${PROJECT_SOURCE_DIR}/src/log.c
    ${PROJECT_SOURCE_DIR}/src/ascend/device.c
    ${PROJECT_SOURCE_DIR}/src/ascend/event.c
    ${PROJECT_SOURCE_DIR}/src/ascend/graph.c
    ${PROJECT_SOURCE_DIR}/src/ascend/hook.c
    ${PROJECT_SOURCE_DIR}/src/ascend/kernel.c
    ${PROJECT_SOURCE_DIR}/src/ascend/memory.c
    ${PROJECT_SOURCE_DIR}/src/ascend/task.c
    ${PROJECT_SOURCE_DIR}/src/utils/config.c
    ${PROJECT_SOURCE_DIR}/src/utils/dcmi_wrapper.c
    ${PROJECT_SOURCE_DIR}/src/utils/hash_map.c
    ${PROJECT_SOURCE_DIR}/src/utils/utils.c
)

set(VNPU_TESTCASE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/ascend/device_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/ascend/event_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/ascend/graph_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/ascend/memory_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/ascend/kernel_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/ascend/task_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/utils/utils_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/utils/config_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/utils/hash_map_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/npu_manager_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/core_limiter_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/deadlock_trace_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/log_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/testcase/mem_limiter_test.cpp
)

set(VNPU_STUB_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/stub/acl_stub.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stub/runtime_stub.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stub/dcmi_stub.c
)

add_executable(vnpu_test
    ${VNPU_SRC_FILES}
    ${VNPU_TESTCASE_FILES}
    ${VNPU_STUB_FILES}
)

target_include_directories(vnpu_test PRIVATE
    ${PROJECT_SOURCE_DIR}/src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/stub
)


target_link_libraries(vnpu_test PRIVATE
    gtest
    gtest_main
    mockcpp
    pthread
    rt
    dl
    c_sec
)

foreach(stub_file IN LISTS VNPU_STUB_FILES)
    set_source_files_properties(${stub_file} PROPERTIES
        COMPILE_FLAGS "-Wno-unused-parameter"
    )
endforeach()

target_compile_definitions(vnpu_test
    PRIVATE
    _GNU_SOURCE
    VCANN_ENABLE_DEADLOCK_DIAGNOSTICS=1
)

target_compile_options(vnpu_test PRIVATE
    -Wall -Wextra -Wformat-nonliteral -Wformat-security -Wformat-y2k -Wfloat-equal
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++14>
    -fPIC
    -fstack-protector-strong
    -fvisibility=hidden
    -fno-common
    -O1
)

target_link_options(vnpu_test PRIVATE
    -Wl,-z,relro,-z,now,-z,noexecstack
    -s
)

if(ENABLE_COVERAGE)
    add_compile_options(-fprofile-arcs -ftest-coverage)
    target_compile_options(vnpu_test PRIVATE -fprofile-arcs -ftest-coverage)
    target_link_options(vnpu_test PRIVATE -fprofile-arcs -ftest-coverage)
endif()
