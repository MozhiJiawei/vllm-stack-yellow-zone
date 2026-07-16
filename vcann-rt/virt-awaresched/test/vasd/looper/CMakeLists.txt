add_ut(looper)

target_compile_options(${CMAKE_PROJECT_NAME}_looper_ut PRIVATE
        -Wno-unused-parameter
        -Wno-unused-function
        -Wno-missing-field-initializers
        -Wno-maybe-uninitialized
        -Wno-conversion
        -Wno-float-conversion
)