file(GLOB VAS_SECURITY_MANAGER_CPP "*.cpp")
FIND_INCLUDE_DIR(VAS_SECURITY_MANAGER_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR})
add_library(security STATIC ${VAS_SECURITY_MANAGER_CPP})
target_include_directories(security PUBLIC ${VAS_SECURITY_MANAGER_INCLUDE}
        ${PROJECT_SOURCE_DIR}/src/include)
target_link_libraries(security PUBLIC logger boundscheck)
