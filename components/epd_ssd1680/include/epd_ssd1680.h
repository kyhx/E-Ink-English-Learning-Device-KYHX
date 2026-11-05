/*
 * epd_ssd1680.h
 *
 * 2.9 寸单色电子墨水屏驱动 —— SSD1680 (128 x 296, 1bpp)
 * 目标模块：ZJY128296-029EAAMFGN（中景园 2.9" 128x296 黑白，SSD1680）
 *
 * 与 4.2" SSD1619A 驱动的区别：
 *   - SSD1680 有真正可编程的 RAM 窗口（0x44/0x45），局刷是「任意矩形」，
 *     不需要 MUX + gate start 的带状技巧；
 *   - 局刷波形用 RED RAM 作为「上一帧」基准，因此每次局刷要写两次 RAM：
 *     先写 BW RAM（新图），刷新后把同一份图写进 RED RAM（下次的基准）；
 *   - 全刷时 RED RAM 被 bypass（0x21 = 0x40），不影响输出。
 *
 * 帧格式：
 *   - 每行 16 字节（128 / 8），整帧 4736 字节
 *   - 行优先，MSB 在左
 *   - 位值 1 = 白，0 = 黑（若实测反色，调用 epd1680_set_invert(true)）
 *
 * 协议来源：SSD1680 datasheet 6.x；刷新流程与 bit 定义对照
 *   - Linux DRM ssd16xx 驱动（SSD1680/1683 的 0x22 组合值）
 *   - Waveshare 2.9" V2 / 中景园 SSD1680 例程
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EPD_SSD1680_H
#define EPD_SSD1680_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- 几何常量 */
#define EPD1680_WIDTH          128
#define EPD1680_HEIGHT         296
#define EPD1680_BYTES_PER_ROW  (EPD1680_WIDTH / 8)                 /* 16 */
#define EPD1680_FRAME_BYTES    (EPD1680_BYTES_PER_ROW * EPD1680_HEIGHT) /* 4736 */

/* ---------------------------------------------------------------- 引脚配置 */
typedef struct {
    gpio_num_t sck;
    gpio_num_t mosi;
    gpio_num_t cs;
    gpio_num_t dc;
    gpio_num_t rst;
    gpio_num_t busy;
    int        busy_high_is_busy;  /* SSD1680: 1 = BUSY 高电平表示忙 */
    int        spi_hz;             /* 建议 4 MHz */
} epd1680_config_t;

/* 默认沿用 4.2" 驱动板 V1.5 的 6 根线，换成 2.9" 模块时接线不变即可跑。 */
#define EPD1680_DEFAULT_CONFIG()                   \
    {                                              \
        .sck  = GPIO_NUM_12, .mosi = GPIO_NUM_11,  \
        .cs   = GPIO_NUM_10, .dc   = GPIO_NUM_9,   \
        .rst  = GPIO_NUM_8,  .busy = GPIO_NUM_7,   \
        .busy_high_is_busy = 1,                    \
        .spi_hz = 4000000,                         \
    }

/* ---------------------------------------------------------------- API */

/* 初始化 SPI/GPIO 并下发 SSD1680 初始化序列。返回 false 表示 BUSY 超时。 */
bool epd1680_init(const epd1680_config_t *cfg);

/* 硬件复位（RST 低 10 ms -> 高 20 ms）。深睡后必须复位才能再通信。 */
void epd1680_hw_reset(void);

/* 等待 BUSY 释放。 */
bool epd1680_wait_busy(uint32_t timeout_ms);

/* 当前 BUSY 电平（0/1），用于确认模块是否真的接上。 */
int epd1680_busy_level(void);

/* 位极性：false = 1 白 0 黑（默认）；true = 取反。 */
void epd1680_set_invert(bool on);
bool epd1680_get_invert(void);

/* 整屏全刷（0xF7）。实测 2023 ms，无残影。fb 长度 >= EPD1680_FRAME_BYTES。 */
bool epd1680_full(const uint8_t *fb, uint32_t timeout_ms);

/* 整屏局刷（内部就是 0xFF + 整屏窗口）。实测 468 ms，比全刷快 4.3 倍，
 * 但会累积残影：连续用 20–30 次后插一次 epd1680_full()。
 * 注意：这不是 0xC7——实测 0xC7 的波形取决于上一条命令残留的 LUT，
 * 耗时在 463/2018 ms 之间跳变，已弃用。 */
bool epd1680_fast(const uint8_t *fb, uint32_t timeout_ms);

/* 矩形局刷（0xFF）。
 * win 是「紧密排列」的窗口数据：每行 w/8 字节，共 (w/8)*h 字节。
 * x 与 w 必须是 8 的倍数；w 最小 8。
 * 刷新后自动把新内容写进 RED RAM，作为下一次局刷的基准。 */
bool epd1680_partial(const uint8_t *win, int x, int y, int w, int h,
                     uint32_t timeout_ms);

/* 窗口内容与「上一次局刷的基准」相同的情况可跳过：这里不做，交给调用者。 */

/* 深睡（0x10 = 0x01，保留 RAM）。 */
void epd1680_sleep(void);

/* ------------------------------------------------------------ 绘图辅助 */
void epd1680_fb_clear(uint8_t *fb, uint8_t color);
void epd1680_fb_pixel(uint8_t *fb, int x, int y, uint8_t color);
void epd1680_fb_hline(uint8_t *fb, int x, int y, int w, uint8_t color);
void epd1680_fb_vline(uint8_t *fb, int x, int y, int h, uint8_t color);
void epd1680_fb_rect(uint8_t *fb, int x, int y, int w, int h, uint8_t color);
void epd1680_fb_fill_rect(uint8_t *fb, int x, int y, int w, int h, uint8_t color);
/* 5x7 ASCII（' '..'~'），color=1 白字 0 黑字 */
void epd1680_fb_text(uint8_t *fb, int x, int y, const char *str, uint8_t color);
int  epd1680_fb_text_width(const char *str);
void epd1680_fb_text_scaled(uint8_t *fb, int x, int y, const char *str,
                            uint8_t color, int scale);
int  epd1680_fb_text_width_scaled(const char *str, int scale);

/* 取 5x7 ASCII 字模（5 字节，列优先，bit0 = 最上行）。
 * 供需要自己旋转/变换的 UI 复用同一份字库。ch 超出 ' '..'~' 返回 '?'。 */
const uint8_t *epd1680_ascii_glyph(char ch);

#ifdef __cplusplus
}
#endif
#endif /* EPD_SSD1680_H */
