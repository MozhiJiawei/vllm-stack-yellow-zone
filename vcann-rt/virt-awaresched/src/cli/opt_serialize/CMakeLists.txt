file(GLOB_RECURSE SRC_FILES *.cpp)
FIND_INCLUDE_DIR(CMD_SERIZLIZE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR})
add_library(cmd_serialize STATIC ${SRC_FILES})
target_include_directories(cmd_serialize PUBLIC ${CMD_SERIZLIZE_INCLUDE} )
