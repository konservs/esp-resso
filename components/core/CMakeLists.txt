# Portable domain logic. This component is "dual-mode":
#   * Under ESP-IDF (idf.py) it registers as a normal IDF component.
#   * Under a plain CMake host build it builds as a static library named `core`
#     so the unit tests can link it with no ESP-IDF present.
# It must never depend on FreeRTOS, ESP-IDF or any hardware headers.

set(CORE_SRCS
    src/pid.c
    src/filter.c
    src/rtd.c
    src/boiler.c
    src/brew.c
    src/load_guard.c
    src/pump_guard.c
    src/state_machine.c
    src/safety.c
    src/settings.c
    src/buttons.c
    src/ui.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${CORE_SRCS}
                           INCLUDE_DIRS include)
else()
    add_library(core STATIC ${CORE_SRCS})
    target_include_directories(core PUBLIC include)

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(core PRIVATE -Wall -Wextra)
    endif()

    # rtd.c/boiler.c use sqrtf/fabsf; link libm where it is a separate library.
    find_library(M_LIB m)
    if(M_LIB)
        target_link_libraries(core PUBLIC ${M_LIB})
    endif()
endif()
