find_package(Threads REQUIRED)
file(GLOB_RECURSE SERVER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(ubs-opt-tuner ${SERVER_SOURCE})

target_include_directories(ubs-opt-tuner PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src/common"
)

target_link_libraries(ubs-opt-tuner PRIVATE RapidJSON::RapidJSON common-lib
    ${UTCOV_TEST}
)

add_custom_command(TARGET ubs-opt-tuner
    POST_BUILD
    COMMAND strip --strip-all $<TARGET_FILE:ubs-opt-tuner>
)

IF(${CMAKE_BUILD_TYPE} MATCHES "Debug")
    file(GLOB_RECURSE TEST_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        EXCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    )

    add_library(ubs-opt-tuner-lib SHARED
        ${TEST_SOURCE}
    )

    target_link_libraries(ubs-opt-tuner-lib PRIVATE RapidJSON::RapidJSON Threads::Threads stdc++fs
        -lgcov
    )

    target_include_directories(ubs-opt-tuner-lib PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src/common"
    )
ENDIF()