
file(GLOB_RECURSE TEST_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(test-server ${TEST_SOURCE} )

target_include_directories(test-server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" 
    "${libbpf_INCLUDE_DIRS}" "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/src/common"
)

target_compile_options(test-server PRIVATE -fno-access-control)

TARGET_LINK_LIBRARIES(test-server RapidJSON::RapidJSON ubs-opt-guard-lib common-lib gtest mockcpp ${libbpf_LIBRARIES}
    ${ELF_LIBRARIES}
    ${zlib_LIBRARIES}
    -lelf
    -lz
)