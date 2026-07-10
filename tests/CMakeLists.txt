# Host (Windows/Linux) unit-test build. This is a PLAIN CMake project — it does
# NOT use ESP-IDF, so it builds with gcc/clang/MinGW and runs natively.
#
#   cmake --preset host
#   cmake --build --preset host
#   ctest --preset host --output-on-failure
#
# Unity (the C test framework) and fff (function mocking) are fetched at
# configure time; the portable `core`/`hal` are pulled in from ../components.
cmake_minimum_required(VERSION 3.16)
project(esp_resso_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()

include(FetchContent)

# --- Unity test framework --------------------------------------------------
FetchContent_Declare(
    unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG        v2.6.0)
FetchContent_MakeAvailable(unity)  # provides the `unity` library target

# --- fff (header-only mocking) ---------------------------------------------
# Populate only (do not add_subdirectory) so fff's own tests are not built.
FetchContent_Declare(
    fff
    GIT_REPOSITORY https://github.com/meekrosoft/fff.git
    GIT_TAG        v1.1)
FetchContent_GetProperties(fff)
if(NOT fff_POPULATED)
    FetchContent_Populate(fff)
endif()
add_library(fff INTERFACE)
target_include_directories(fff INTERFACE ${fff_SOURCE_DIR})

# --- Code under test -------------------------------------------------------
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../components/core        ${CMAKE_BINARY_DIR}/core)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../components/espresso_hal  ${CMAKE_BINARY_DIR}/espresso_hal)

# Shared test helpers (deterministic clock, etc.).
add_library(test_support STATIC support/fake_clock.c)
target_include_directories(test_support PUBLIC support)
target_link_libraries(test_support PUBLIC core unity)

# Warnings on for the tests too.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

# --- Pure-core test suites (Unity only) ------------------------------------
set(CORE_TESTS
    test_pid
    test_filter
    test_rtd
    test_boiler
    test_brew
    test_load_guard
    test_pump_guard
    test_state_machine
    test_safety
    test_settings
    test_buttons
    test_ui)

foreach(t ${CORE_TESTS})
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PRIVATE core unity test_support)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

# --- HAL mocking demo (Unity + fff) ----------------------------------------
add_executable(test_hal_mock test_hal_mock.c)
target_link_libraries(test_hal_mock PRIVATE espresso_hal unity fff)
add_test(NAME test_hal_mock COMMAND test_hal_mock)
