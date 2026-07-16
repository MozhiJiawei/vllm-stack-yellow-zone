# -----------------------------
# Step 1: 尝试使用系统 GTest (CONFIG 模式优先)
# -----------------------------
find_package(GTest CONFIG QUIET)

# -----------------------------
# Step 2: 如果没找到，尝试 Module 模式（旧版）
# -----------------------------
if(NOT GTest_FOUND)
    # 重置可能残留的变量和目标
    foreach(_target GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main)
        if(TARGET ${_target})
            message(WARNING "Removing pre-existing target ${_target} from previous find_package(GTest)")
            remove_if_target_exists(${_target})  # 见下方宏
        endif()
    endforeach()

    find_package(GTest MODULE QUIET)
endif()

# -----------------------------
# Step 3: 检查 GTest_FOUND 并处理系统目标
# -----------------------------
if(GTest_FOUND)
    message(STATUS "Using system GTest: ${GTest_VERSION} (found via ${GTest_CONFIG})")

    # --- 处理 gtest_main ---
    if(NOT TARGET GTest::gtest_main AND TARGET GTest::Main)
        add_library(GTest::gtest_main INTERFACE IMPORTED)
        set_target_properties(GTest::gtest_main PROPERTIES
                INTERFACE_LINK_LIBRARIES "GTest::Main"
        )
        message(STATUS "Created alias: GTest::gtest_main -> GTest::Main")
    endif()

    # --- 处理 gmock ---
    if(NOT TARGET GTest::gmock AND TARGET GTest::GTest AND TARGET GMock::GMock)
        add_library(GTest::gmock INTERFACE IMPORTED)
        set_target_properties(GTest::gmock PROPERTIES
                INTERFACE_LINK_LIBRARIES "GMock::GMock;GTest::GTest;Threads::Threads"
        )
        message(STATUS "Created alias: GTest::gmock")
    endif()

    # --- 处理 gmock_main ---
    if(NOT TARGET GTest::gmock_main)
        if(TARGET GTest::gmock AND TARGET GTest::gtest_main)
            add_library(GTest::gmock_main INTERFACE IMPORTED)
            set_target_properties(GTest::gmock_main PROPERTIES
                    INTERFACE_LINK_LIBRARIES "GTest::gmock;GTest::gtest_main"
            )
            message(STATUS "Created alias: GTest::gmock_main -> GTest::gmock + gtest_main")
        elseif(EXISTS "/usr/lib64/libgmock_main.a")
            add_library(GTest::gmock_main STATIC IMPORTED)
            set_target_properties(GTest::gmock_main PROPERTIES
                    IMPORTED_LOCATION "/usr/lib64/libgmock_main.a"
                    INTERFACE_INCLUDE_DIRECTORIES "${GTest_INCLUDE_DIRS}"
                    INTERFACE_LINK_LIBRARIES "Threads::Threads"
            )
            message(STATUS "Created GTest::gmock_main from /usr/lib64/libgmock_main.a")
        else()
            message(WARNING "GTest::gmock_main not available and cannot be created!")
        endif()
    endif()

else()
    # -----------------------------
    # Step 4: Fallback 到 FetchContent
    # -----------------------------
    message(STATUS "GTest not found in system, using bundled version")

    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()

    include(FetchContent)

    set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
    set(BUILD_GTEST ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://gitcode.com/GitHub_Trending/go/googletest.git
            GIT_TAG main
    )
    FetchContent_MakeAvailable(googletest)

    # 只有在目标不存在时才创建 ALIAS
    foreach(_tgt gtest gtest_main gmock gmock_main)
        if(TARGET ${_tgt} AND NOT TARGET GTest::${_tgt})
            add_library(GTest::${_tgt} ALIAS ${_tgt})
            message(STATUS "Created ALIAS: GTest::${_tgt} -> ${_tgt}")
        elseif(TARGET GTest::${_tgt})
            message(STATUS "GTest::${_tgt} already exists, skipping ALIAS creation")
        else()
            message(WARNING "Target ${_tgt} not found after FetchContent!")
        endif()
    endforeach()

    message(STATUS "Bundled GTest available as GTest::* targets")
endif()

# -----------------------------
# Step 5: 验证最终结果
# -----------------------------
if(TARGET GTest::gmock_main)
    message(STATUS "Final: GTest::gmock_main is ready to use")
else()
    message(FATAL_ERROR "Failed to create GTest::gmock_main! Check your environment.")
endif()