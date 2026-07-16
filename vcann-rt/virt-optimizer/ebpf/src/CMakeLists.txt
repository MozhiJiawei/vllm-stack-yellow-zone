
IF(${CMAKE_BUILD_TYPE} MATCHES "Debug")
    add_compile_options(
        -fprofile-arcs
        -ftest-coverage
    )
endif ()

add_subdirectory(common)

add_subdirectory(client)

add_subdirectory(server)

add_subdirectory(optimizer)