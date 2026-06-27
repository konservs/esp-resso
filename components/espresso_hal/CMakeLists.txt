# HAL is interface-only (headers). Concrete implementations live in
# components/drivers (ESP32) or in test fakes (host). Dual-mode like core.
#
# Named `espresso_hal` (not `hal`) because `hal` is a reserved ESP-IDF component;
# the include prefix stays `hal/...` (our header basenames don't collide with
# IDF's hal headers).

if(ESP_PLATFORM)
    idf_component_register(INCLUDE_DIRS include
                           REQUIRES core)
else()
    add_library(espresso_hal INTERFACE)
    target_include_directories(espresso_hal INTERFACE include)
    target_link_libraries(espresso_hal INTERFACE core)
endif()
