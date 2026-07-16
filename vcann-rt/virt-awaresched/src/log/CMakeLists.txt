file(GLOB LOGGER_CPP "*.cpp")
FIND_INCLUDE_DIR(LOGGER_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR})
add_library(logger STATIC ${LOGGER_CPP})
target_include_directories(logger PUBLIC ${LOGGER_INCLUDE})
