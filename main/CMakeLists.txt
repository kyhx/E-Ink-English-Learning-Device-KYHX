idf_component_register(
    SRCS "main.c"
         "board/buttons.c"
         "services/storage.c"
         "services/audio.c"
         "services/audio_in.c"
         "services/audio_out.c"
         "services/audio_wav.c"
         "services/audio_vad.c"
         "services/audio_analysis.c"
         "bench/bench.c"
         "panel_test/panel1680_test.c"
         "ui29/ui29_card.c"
    INCLUDE_DIRS "." "board" "services" "bench" "panel_test" "ui29"
    REQUIRES epd_ssd1680
             esp_driver_gpio
             esp_driver_spi
             esp_driver_uart
             esp_driver_i2s
             esp_timer
             esp_partition
             nvs_flash
             console
             joltwallet__littlefs
)
