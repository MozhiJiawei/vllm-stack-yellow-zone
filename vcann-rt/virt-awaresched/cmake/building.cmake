IF(NOT DEFINED CMAKE_BUILD_TYPE OR x${CMAKE_BUILD_TYPE} STREQUAL "x")
    SET(CMAKE_BUILD_TYPE Release)
ENDIF()

# SET COMPILE FLAGS
if (${CMAKE_BUILD_TYPE} MATCHES "Release")
    add_link_options(
            -Wl,-z,noexecstack          # Stack non-executable protection
            -Wl,-z,relro,-z,now         # GOT table fully relocated read-only
            -s
    )
    
    add_compile_options(
            -fstack-protector-strong    # Enable stack protection to prevent stack overflow attacks.
            -D_FORTIFY_SOURCE=2 -O2     # Turn on the FS option
            -fPIE                       # Generating location-independent executables
            -fPIC
    )
endif()