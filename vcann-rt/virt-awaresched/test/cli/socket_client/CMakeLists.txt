add_ut(socket_client)

target_compile_options(${CMAKE_PROJECT_NAME}_socket_client_ut PRIVATE
        -Wno-unused-parameter
        -Wno-unused-function
        -Wno-missing-field-initializers
        -Wno-maybe-uninitialized
        -Wno-conversion
        -Wno-float-conversion
)