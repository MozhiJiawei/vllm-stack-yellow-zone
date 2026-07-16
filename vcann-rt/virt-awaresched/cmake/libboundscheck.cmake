find_path(LIBBOUNDCHECK_INCLUDE_DIR
        NAMES securec.h
        PATHS /usr/include
        NO_DEFAULT_PATH
)

find_library(LIBBOUNDCHECK_SHARED_LIBRARY
        NAMES libboundscheck.so boundscheck
        PATHS /usr/lib64 /usr/lib
        NO_DEFAULT_PATH
)