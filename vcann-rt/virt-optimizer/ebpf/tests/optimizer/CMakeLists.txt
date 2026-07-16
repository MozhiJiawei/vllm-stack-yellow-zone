#
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
#

file(GLOB_RECURSE TEST_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(test-optimizer ${TEST_SOURCE})

target_include_directories(test-optimizer PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${libbpf_INCLUDE_DIRS}"
        "${PROJECT_SOURCE_DIR}/src"
        "${PROJECT_SOURCE_DIR}/src/common"
        "${PROJECT_SOURCE_DIR}/src/optimizer"
)

target_compile_options(test-optimizer PRIVATE -fno-access-control)

TARGET_LINK_LIBRARIES(test-optimizer RapidJSON::RapidJSON ubs-opt-tuner-lib common-lib gtest mockcpp ${libbpf_LIBRARIES}
        ${ELF_LIBRARIES}
        ${zlib_LIBRARIES}
        -lelf
        -lz
)