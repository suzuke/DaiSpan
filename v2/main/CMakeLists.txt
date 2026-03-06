file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "."
    REQUIRES
        driver
        nvs_flash
        esp_wifi
        esp_http_server
        esp_event
        json
        app_update
        esp_hap_core
        esp_hap_apple_profiles
        esp_hap_platform
)
