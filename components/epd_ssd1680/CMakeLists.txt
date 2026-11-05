# 2.9" SSD1680 (128x296) driver.
# 5x7 字体表与 4.2" 驱动同源；为避免跨组件私有头依赖，这里保留一份拷贝，
# 来源与许可证见文件头（Adafruit glcdfont, BSD-2-Clause）。
idf_component_register(
    SRCS "epd_ssd1680.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_spi esp_timer
)
