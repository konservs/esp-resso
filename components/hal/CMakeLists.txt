# HAL is interface-only (headers). Concrete implementations live in
# components/drivers (ESP32) or in test fakes (host). Dual-mode like core.

if(ESP_PLATFORM)
    idf_component_register(INCLUDE_DIRS include
                           REQUIRES core)
else()
    add_library(hal INTERFACE)
    target_include_directories(hal INTERFACE include)
    target_link_libraries(hal INTERFACE core)
endif()
