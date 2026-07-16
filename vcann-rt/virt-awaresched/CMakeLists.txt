cmake_minimum_required(VERSION 3.22)
project(VirtAwareSched VERSION 1.0 LANGUAGES C CXX)

# Echo the execute_process command (for debugging purposes), default is NONE.
set(EXECUTE_PROCESS_COMMAND_ECHO NONE)
ADD_DEFINITIONS(-DLOG_LEVE=DEBUG)

# Print basic information.
message(STATUS "CMake version: ${CMAKE_VERSION}")
message(STATUS "Source dir: ${CMAKE_SOURCE_DIR}")
message(STATUS "Build dir: ${CMAKE_BINARY_DIR}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Build generator: ${CMAKE_GENERATOR}")
# Obtain and print compiler information
message(STATUS "C compiler for the host machine: ${CMAKE_C_COMPILER} (${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION})")
set(C_LINKER_PREFERENCE "gold")
set(CXX_LINKER_PREFERENCE "gold")
# Add linker options to specify the output path for symbol files.
message(STATUS "CXX compiler for the host machine: ${CMAKE_CXX_COMPILER} (${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION})")
execute_process(
        COMMAND sh -c "${CMAKE_LINKER} --version | head -n 1"
        OUTPUT_VARIABLE LINKER_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "C linker for the host machine: ${CMAKE_LINKER} (${LINKER_VERSION})")
# Obtain and print host resource information
message(STATUS "Host machine system: ${CMAKE_HOST_SYSTEM}")
message(STATUS "Host machine cpu family: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
cmake_host_system_information(RESULT number_of_logical_cores QUERY NUMBER_OF_LOGICAL_CORES)
message(STATUS "Host machine logical cpu cores: ${number_of_logical_cores}")
cmake_host_system_information(RESULT number_of_physical_cores QUERY NUMBER_OF_PHYSICAL_CORES)
message(STATUS "Host machine physical cpu cores: ${number_of_physical_cores}")
cmake_host_system_information(RESULT total_physical_memory QUERY TOTAL_PHYSICAL_MEMORY)
message(STATUS "Host machine total physical memory: ${total_physical_memory} MB")
cmake_host_system_information(RESULT available_physical_memory QUERY AVAILABLE_PHYSICAL_MEMORY)
message(STATUS "Host machine available physical memory: ${available_physical_memory} MB")

# Set compilation log colors
option(FORCE_COLORED_OUTPUT "Always produce ANSI-colored output (GNU/Clang only)." OFF)
if (${FORCE_COLORED_OUTPUT})
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        add_compile_options(-fcolor-diagnostics)
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        add_compile_options(-fdiagnostics-color=always)
    endif ()
else ()
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        add_compile_options(-fdiagnostics-color=auto)
    endif ()
endif ()
# Setting compilation options
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS NO)
set(CMAKE_SKIP_RPATH TRUE)
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2 -g")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -g")
message(STATUS "Build C standard: C${CMAKE_C_STANDARD}")
message(STATUS "Build C++ standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "CMAKE_CXX_FLAGS_DEBUG: ${CMAKE_CXX_FLAGS_DEBUG}")
message(STATUS "CMAKE_CXX_FLAGS_RELWITHDEBINFO: ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
message(STATUS "CMAKE_CXX_FLAGS_RELEASE: ${CMAKE_CXX_FLAGS_RELEASE}")
message(STATUS "CMAKE_CXX_FLAGS_MINSIZEREL: ${CMAKE_CXX_FLAGS_MINSIZEREL}")

# Configure CMake module path
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
include(utils)
include(building)
include(libboundscheck)

option(ENABLE_COVERAGE "Use gcov" OFF)
message(STATUS ENABLE_COVERAGE=${ENABLE_COVERAGE})
if(ENABLE_COVERAGE)
    setup_coverage()
endif()

# Set all executable output directories
set(INCLUDE_DIR src/include)
set(UTIL_INCLUDE_DIR src/util)
set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(PROJECT_DEPS_DIR ${PROJECT_SOURCE_DIR}/deps)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/static)
set(PROJECT_TOP_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(LIBVIRT_INCLUDE_DIR /usr/include/libvirt)
set(LIBVIRT_LIB_PATH /usr/lib64/libvirt.so)
# Print path information.
message(STATUS "CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")
message(STATUS "CMAKE_INSTALL_INCLUDEDIR: ${CMAKE_INSTALL_INCLUDEDIR}")
message(STATUS "CMAKE_MODULE_PATH: ${CMAKE_MODULE_PATH}")
message(STATUS "CMAKE_RUNTIME_OUTPUT_DIRECTORY: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "CMAKE_ARCHIVE_OUTPUT_DIRECTORY: ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
message(STATUS "CMAKE_CURRENT_BINARY_DIR: ${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "PROJECT_TOP_BUILD_DIR: ${PROJECT_TOP_BUILD_DIR}")
get_directory_property(COMPILE_OPTIONS DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} COMPILE_OPTIONS)
message(STATUS "CXX Compile options: ${CMAKE_CXX_FLAGS_RELEASE} ${COMPILE_OPTIONS}")

# Add header file search path
include_directories(${INCLUDE_DIR} ${LIBVIRT_INCLUDE_DIR} ${UTIL_INCLUDE_DIR})
# Adding a Source Code Directory
add_subdirectory(src)

message(STATUS BUILD_TESTS=${BUILD_TESTS})
if (BUILD_TESTS)
    add_subdirectory(test)
endif ()

add_custom_target(build_all
        COMMAND make
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# packaging
set(CPACK_RPM_DAEMON_USER_BINARY_SPECFILE ${CMAKE_BINARY_DIR}/vas-daemon.spec)

add_custom_target(build_package
        # Create package directory
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/package
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_INSTALL_PREFIX}/bin

        # Copy the bin directory
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/* ${CMAKE_INSTALL_PREFIX}/bin

        # Compress and pack
        COMMAND tar -czvf ${PROJECT_SOURCE_DIR}/package/virt-awaresched.tar.gz -C ${CMAKE_INSTALL_PREFIX} bin

        COMMENT "Packaging bin directories"
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)
add_dependencies(build_package build_all)