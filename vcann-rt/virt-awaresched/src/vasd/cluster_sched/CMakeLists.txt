file(GLOB CLUSTER_SCHED_CPP "*.cpp")
FIND_INCLUDE_DIR(CLUSTER_SCHED_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR})
add_library(cluster_sched STATIC ${CLUSTER_SCHED_CPP})
target_include_directories(cluster_sched PUBLIC ${CLUSTER_SCHED_INCLUDE}
        ${PROJECT_SOURCE_DIR}/src/include)
target_link_libraries(cluster_sched PUBLIC logger util vas_daemon_register acquire security)
