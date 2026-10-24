idf_component_register(
    SRCS "epd_ssd1619a.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_spi esp_timer
)
