idf_component_register(
    SRCS "main.c"
         "word_view.c"
    INCLUDE_DIRS "."
    REQUIRES epd_ssd1619a esp_driver_gpio esp_driver_spi esp_timer
)
