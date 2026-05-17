idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_spi
        esp_http_client
        esp_lcd
        esp_netif
        esp_psram
        esp-tls
        esp_wifi
        json
        nvs_flash
)
