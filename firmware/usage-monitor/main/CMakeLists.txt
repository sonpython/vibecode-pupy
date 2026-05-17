idf_component_register(
    SRCS "main.c" "brand_icons.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_spi
        esp_driver_usb_serial_jtag
        esp_adc
        esp_http_client
        esp_lcd
        esp_netif
        esp_psram
        esp-tls
        esp_wifi
        esp_lvgl_port
        json
        lvgl
        nvs_flash
)
