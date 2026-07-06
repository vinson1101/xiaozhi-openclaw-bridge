idf_component_register(
    INCLUDE_DIRS "."
    REQUIRES esp_http_client esp_timer esp_driver_uart
)

add_prebuilt_library(vbota_prebuilt "libesp32c3.a")
target_link_libraries(${COMPONENT_LIB} INTERFACE vbota_prebuilt)
