find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(libbpf REQUIRED libbpf)
pkg_check_modules(ELF REQUIRED libelf)
pkg_check_modules(zlib REQUIRED zlib)

set(BPF_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/lib")
file(MAKE_DIRECTORY ${BPF_OUTPUT_DIR})

file(GLOB_RECURSE BPF_PROGRAMS ${PROJECT_SOURCE_DIR}/src/client/bpfs/*.c)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|arm64)")
    set(BPF_FLAGS
        -O3
        -g
        -Wextra
        -Wfloat-equal
        -fno-common
        -Wall
        -target bpf
        -D__TARGET_ARCH_arm64
        -I${CMAKE_CURRENT_SOURCE_DIR})
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64)")
    set(BPF_FLAGS
        -O3
        -g
        -Wextra
        -Wfloat-equal
        -fno-common
        -Wall
        -target bpf
        -D__TARGET_ARCH_x86_64
        -I${CMAKE_CURRENT_SOURCE_DIR})
else()
    message(FATAL_ERROR "eBPF only support x86_64, AMD64, aarch64 or arm64 architecture.")
endif()

set(BPF_OBJECTS)

if(DEFINED CLANG_COMPILER_PATH)
    message(STATUS "CLANG_COMPILER_PATH:${CLANG_COMPILER_PATH}")
else()
    set(CLANG_COMPILER_PATH "/usr/bin")
endif()

foreach (source ${BPF_PROGRAMS})
    get_filename_component(source_name ${source} NAME_WE)

    set(output_o "${BPF_OUTPUT_DIR}/${source_name}.o")
    set(output_h "${BPF_OUTPUT_DIR}/${source_name}.h")

    add_custom_command(
            OUTPUT ${output_o}
            COMMAND ${CLANG_COMPILER_PATH}/clang ${BPF_FLAGS} -c ${source} -o ${output_o}
            DEPENDS ${source}
            COMMENT "Compiling BPF: ${source} → ${output_o}"
            VERBATIM
    )

    add_custom_command(
            OUTPUT ${output_h}
            COMMAND bpftool gen skeleton ${output_o} > ${output_h}
            DEPENDS ${output_o}
            COMMENT "Compiling BPF: ${output_o} → ${output_h}"
            VERBATIM
    )

    list(APPEND BPF_OBJECTS ${output_h})
endforeach ()

add_custom_target(bpf_objects ALL
    DEPENDS ${BPF_OBJECTS}
    COMMENT "Building all BPF objects in ${BPF_OUTPUT_DIR}"
)

file(GLOB_RECURSE COLLECTOR_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    EXCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/bpfs"
)

add_executable(ubs-opt ${COLLECTOR_SOURCE})

target_include_directories(ubs-opt PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src/common"
    ${libbpf_INCLUDE_DIRS}
    ${BPF_OUTPUT_DIR}
)

target_link_libraries(ubs-opt PRIVATE RapidJSON::RapidJSON common-lib Threads::Threads stdc++fs
    ${libbpf_LIBRARIES}
    ${ELF_LIBRARIES}
    ${zlib_LIBRARIES}
    ${UTCOV_TEST}
    -lelf
    -lz
    )

add_dependencies(ubs-opt bpf_objects)

add_custom_command(TARGET ubs-opt
    POST_BUILD
    COMMAND strip --strip-all $<TARGET_FILE:ubs-opt>
)

IF(${CMAKE_BUILD_TYPE} MATCHES "Debug")

    file(GLOB_RECURSE TEST_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        EXCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/bpfs"
        EXCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    )

    add_library(ubs-opt-lib SHARED
        ${TEST_SOURCE}
    )

    target_link_libraries(ubs-opt-lib PRIVATE RapidJSON::RapidJSON Threads::Threads stdc++fs
        ${libbpf_LIBRARIES}
        ${ELF_LIBRARIES}
        ${zlib_LIBRARIES}
        -lgcov
        -lelf
        -lz
    )

    add_dependencies(ubs-opt-lib bpf_objects)

    target_include_directories(ubs-opt-lib PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src/common"
        ${libbpf_INCLUDE_DIRS}
        ${BPF_OUTPUT_DIR}
    )
ENDIF()