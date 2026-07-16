find_package(Threads REQUIRED)

file(GLOB_RECURSE SERVER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(ubs-opt-guard ${SERVER_SOURCE})

target_include_directories(ubs-opt-guard PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src/common"
)

target_link_libraries(ubs-opt-guard PRIVATE RapidJSON::RapidJSON common-lib Threads::Threads stdc++fs
    ${UTCOV_TEST}
)

add_custom_command(TARGET ubs-opt-guard
    POST_BUILD
    COMMAND strip --strip-all $<TARGET_FILE:ubs-opt-guard>
)

IF(${CMAKE_BUILD_TYPE} MATCHES "Debug")
    file(GLOB_RECURSE TEST_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        EXCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/main_server.cpp"
    )

    add_library(ubs-opt-guard-lib SHARED
        ${TEST_SOURCE}
    )

    target_link_libraries(ubs-opt-guard-lib PRIVATE RapidJSON::RapidJSON common-lib Threads::Threads stdc++fs
        -lgcov
    )

    target_include_directories(ubs-opt-guard-lib PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src/common"
    )
ENDIF()