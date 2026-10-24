/*
 * epd_ssd1619a.h
 *
 * 4.2 寸单色电子墨水屏驱动 —— SSD1619A (400 x 300, 1bpp)
 * 适配：墨水屏驱动板 V1.5 (8 针 SPI)、ESP32-S3-N16R8、ESP-IDF v6.1
 *
 * 帧数据格式（与本仓库示例目录 frames42 目录下的 raw 帧文件完全一致）：
 *   - 每行 50 字节（400 / 8），整帧 15000 字节
 *   - 行优先（row-major），高位在左（MSB first）
 *   - 位值 1 = 白色，0 = 黑色
 *     （SSD1619A 手册：Write RAM(BW) 写 1 为白色像素，写 0 为黑色像素）
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EPD_SSD1619A_H
#define EPD_SSD1619A_H

#include <stdint.h>
#include <stddef.h>
#include "driver/gpio.h"   /* ESP-IDF v6.x: esp_driver_gpio 组件提供的兼容头 */

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- 几何常量 */
#define EPD_WIDTH              400
#define EPD_HEIGHT             300
#define EPD_BYTES_PER_ROW      (EPD_WIDTH / 8)      /* 50 */
#define EPD_FRAME_BYTES        (EPD_BYTES_PER_ROW * EPD_HEIGHT) /* 15000 */

/* ---------------------------------------------------------------- 引脚配置 */
typedef struct {
    gpio_num_t sck;       /* SPI 时钟  */
    gpio_num_t mosi;      /* SPI 数据  */
    gpio_num_t cs;        /* 片选（软件控制，驱动板 CS1）*/
    gpio_num_t dc;        /* 数据/命令 */
    gpio_num_t rst;       /* 复位（低有效）*/
    gpio_num_t busy;      /* 忙信号     */
    int        busy_high_is_busy; /* 1: BUSY 高电平表示忙；0: 低电平表示忙 */
    int        spi_hz;    /* SPI 时钟频率，建议 4000000 */
} epd_config_t;

/* 常用引脚组合：ESP32-S3 + 驱动板 V1.5（CS2 为三色屏双片选，单色屏不接）*/
#define EPD_S3_DEFAULT_CONFIG()                    \
    {                                               \
        .sck  = GPIO_NUM_12, .mosi = GPIO_NUM_11,  \
        .cs   = GPIO_NUM_10, .dc   = GPIO_NUM_9,   \
        .rst  = GPIO_NUM_8,  .busy = GPIO_NUM_7,   \
        .busy_high_is_busy = 1,                    \
        .spi_hz = 4000000,                         \
    }

/* ---------------------------------------------------------------- API */

/* 初始化 SPI、GPIO、面板。调用后屏幕处于可写状态。 */
void epd_init(const epd_config_t *cfg);

/* 等待面板空闲。timeout_ms 超时返回 false。 */
bool epd_wait_busy(uint32_t timeout_ms);

/* 整帧显示。fb 长度必须 >= EPD_FRAME_BYTES。 */
void epd_display(const uint8_t *fb);

/* 区域显示（局部写 RAM）。
 * fb 必须是「紧密排列」的区域数据：每行 w/8 字节，共 (w/8)*h 字节。
 * 坐标以像素计；x 与 w 必须都是 8 的倍数（RAM X 为字节坐标），
 * 否则直接报错返回。注意：SSD1619A 的波形存在 OTP 里，本驱动仍会
 * 触发一次全屏刷新，是否支持真正的 partial update 取决于面板。 */
void epd_display_area(const uint8_t *fb, int x, int y, int w, int h);

/* 实验接口：指定 0x22 序列和 BUSY 超时，用于测试面板是否支持 partial。
 * 正常业务请用 epd_display_area()（固定 0xC7 / 30s）。 */
bool epd_display_area_ex(const uint8_t *fb, int x, int y, int w, int h,
                         uint8_t ctrl2, uint32_t timeout_ms);

/* 局刷模式：硬件复位 + EPaperDrive 初始化 + 70 字节 partial LUT。
 * 调用前需先 epd_init() 建立 SPI/GPIO。 */
bool epd_partial_init(const uint8_t *lut);

/* 已进入 partial 模式后的区域更新 / 整屏底图重画。 */
bool epd_partial_update(const uint8_t *area, int x, int y, int w, int h,
                        uint32_t timeout_ms);
bool epd_partial_show_full(const uint8_t *fb, uint32_t timeout_ms);
/* 清洁周期后重新加载 partial LUT、恢复 0x21=0x00 */
bool epd_partial_reload_lut(const uint8_t *lut);

/* 完全按 EPaperDrive EPD_Dis_Part 语义的窗口局刷：
 * 整帧 buffer + offset，x/y 交换 + Y 反向，0x11=0x01。 */
bool epd_epd_dis_part(const uint8_t *frame, int x0, int x1, int y0, int y1,
                      uint32_t timeout_ms);

/* 整屏填充（0=黑，非 0=白）。 */
void epd_fill(uint8_t color);

/* 深度睡眠（画面保持，功耗最低）。 */
void epd_deep_sleep(void);

/* ------------------------------------------------------------ 绘图辅助 */
/* 1bpp 帧缓冲：1 = 白，0 = 黑（与 epd_display 的约定一致）*/
void epd_fb_clear(uint8_t *fb, uint8_t color);
void epd_fb_pixel(uint8_t *fb, int x, int y, uint8_t color);
void epd_fb_hline(uint8_t *fb, int x, int y, int w, uint8_t color);
void epd_fb_vline(uint8_t *fb, int x, int y, int h, uint8_t color);
void epd_fb_rect(uint8_t *fb, int x, int y, int w, int h, uint8_t color);
void epd_fb_fill_rect(uint8_t *fb, int x, int y, int w, int h, uint8_t color);
/* 5x7 ASCII 字体，ch 取值 ' '~'~'；color=1 画白字，0 画黑字 */
void epd_fb_text(uint8_t *fb, int x, int y, const char *str, uint8_t color);
int  epd_fb_text_width(const char *str);
/* 放大版 5x7 文本：每个像素放大 scale 倍（1~8） */
void epd_fb_text_scaled(uint8_t *fb, int x, int y, const char *str,
                        uint8_t color, int scale);
int  epd_fb_text_width_scaled(const char *str, int scale);

#ifdef __cplusplus
}
#endif
#endif /* EPD_SSD1619A_H */
