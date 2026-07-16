add_library(socket_client STATIC socket_client.cpp)
target_include_directories(socket_client
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src/log
)
target_link_libraries(socket_client PRIVATE logger boundscheck)

add_library(socket_server STATIC socket_server.cpp)
target_include_directories(socket_server
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src/log
)
target_link_libraries(socket_server PRIVATE logger boundscheck security)
