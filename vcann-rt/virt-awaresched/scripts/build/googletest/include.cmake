message(STATUS "Add compile options")
add_compile_options(-fstack-protector-strong -ftrapv -fPIC -D_FORTIFY_SOURCE=2 -O2)
