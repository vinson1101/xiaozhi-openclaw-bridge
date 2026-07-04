idf_component_register(
    SRCS "main.c" "eyes.c" "screen.c" "provisioning.c" "lcd.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_client esp_wifi nvs_flash esp_lcd esp_timer esp_driver_spi esp_driver_ledc
)
