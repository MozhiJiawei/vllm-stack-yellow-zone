file(GLOB_RECURSE ACQUIRE_CPP "*.cpp")
FIND_INCLUDE_DIR(ACQUIRE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(acquire STATIC ${ACQUIRE_CPP})
target_include_directories(acquire PUBLIC ${ACQUIRE_INCLUDE})
include_directories(acquire PRIVATE ${LIBVIRT_INCLUDE_DIR})
target_link_libraries(acquire PRIVATE util logger conf vas_daemon_register ${LIBVIRT_LIB_PATH})
