add_subdirectory(arg_parse)

# CLI executable file
add_executable(vasctl vasctl_main.cpp)
target_link_libraries(vasctl PRIVATE vas_ctl_register)
target_compile_options(vasctl PRIVATE
        $<$<CONFIG:Debug>: ${DEBUG_FLAGS}>
        $<$<CONFIG:Release>: ${RELEASE_FLAGS}>
        $<$<CONFIG:RelWithDebInfo>: ${RELEASE_FLAGS}>
        $<$<CONFIG:MinSizeRel>: ${RELEASE_FLAGS}>
)
if (${CMAKE_BUILD_TYPE} MATCHES "Release")
    target_link_options(vasctl PRIVATE -rdynamic -pie)
    target_compile_options(vasctl PRIVATE -ftrapv)
endif()
get_target_property(VAS_CLI_COMPILE_OPTIONS vasctl COMPILE_OPTIONS)
message(DEBUG "vasctl compile options: ${VAS_CLI_COMPILE_OPTIONS}")