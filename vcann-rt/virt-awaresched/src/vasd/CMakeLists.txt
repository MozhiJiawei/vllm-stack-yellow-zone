add_subdirectory(api)
add_subdirectory(arg_parse)
add_subdirectory(cluster_sched)
add_subdirectory(conf)
add_subdirectory(acquire)
add_subdirectory(looper)
add_subdirectory(security)

add_executable(vas_daemon vas_daemon_main.cpp)
target_link_libraries(vas_daemon PRIVATE
        logger
        vas_daemon_register
        cli_parse
        looper
        acquire
        cmd_serialize
        cluster_sched
        security
)
target_compile_options(vas_daemon PRIVATE
        # C and C++ compile option
        $<$<CONFIG:Debug>: ${DEBUG_FLAGS}>
        $<$<CONFIG:Release>: ${RELEASE_FLAGS}>
        $<$<CONFIG:RelWithDebInfo>: ${RELEASE_FLAGS}>
        $<$<CONFIG:MinSizeRel>: ${RELEASE_FLAGS}>
)
if (${CMAKE_BUILD_TYPE} MATCHES "Release")
    target_link_options(vas_daemon PRIVATE -rdynamic -pie)
    target_compile_options(vas_daemon PRIVATE -ftrapv)
endif()
get_target_property(VAS_CLI_COMPILE_OPTIONS vas_daemon COMPILE_OPTIONS)
message(DEBUG "vas_daemon compile options: ${VAS_CLI_COMPILE_OPTIONS}")
