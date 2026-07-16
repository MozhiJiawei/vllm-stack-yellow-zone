
file(GLOB_RECURSE TEST_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(test-client ${TEST_SOURCE} )

target_include_directories(test-client PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/src/common"
    "${libbpf_INCLUDE_DIRS}"
)

target_compile_options(test-client PRIVATE -fno-access-control)

TARGET_LINK_LIBRARIES(test-client RapidJSON::RapidJSON ubs-opt-lib common-lib gtest mockcpp ${libbpf_LIBRARIES}
    ${ELF_LIBRARIES}
    ${zlib_LIBRARIES}
    -lelf
    -lz
)