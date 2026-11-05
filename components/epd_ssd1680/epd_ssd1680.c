/*
 * epd_ssd1680.c
 *
 * 2.9 寸单色电子墨水屏驱动 —— SSD1680 (128 x 296, 1bpp)
 * 目标模块：ZJY128296-029EAAMFGN
 *
 * 刷新流程（SSD1680 datasheet + Linux DRM ssd16xx + Waveshare 2.9 V2 对照）：
 *
 *   全刷  0x24(BW)=新图 -> 0x26(RED)=新图 -> 0x21={0x40,0x00} -> 0x22=0xF7 -> 0x20
 *   快刷  0x24(BW)=新图 -> 0x26(RED)=新图 -> 0x21={0x40,0x00} -> 0x22=0xC7 -> 0x20
 *   局刷  0x24(BW)=新图 -> 0x21={0x00,0x00} -> 0x22=0xFF -> 0x20 -> 0x26(RED)=新图
 *
 * 局刷为什么写两次：SSD1680 的局刷波形把 BW RAM（新）和 RED RAM（旧）逐像素
 * 比较决定翻转，所以 RED RAM 必须保存「屏幕当前内容」。刷新完把同一份图写回
 * RED RAM，下一次局刷才有正确基准。第一次上电前必须先做一次全刷建立基准。
 *
 * 0x22 bit：7=开时钟 6=开模拟 5=载温度 4=载LUT 3=Display Mode2 2=显示
 *           1=关模拟 0=关时钟
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "epd_ssd1680.h"
#include "epd_font_5x7.h"

static const char *TAG = "epd_ssd1680";

/* ------------------------------------------------------------- 命令常量 */
#define CMD_DRIVER_OUTPUT     0x01
#define CMD_DEEP_SLEEP        0x10
#define CMD_DATA_ENTRY        0x11
#define CMD_SW_RESET          0x12
#define CMD_TEMP_SENSOR       0x18
#define CMD_MASTER_ACT        0x20
#define CMD_UPDATE_CTRL1      0x21
#define CMD_UPDATE_CTRL2      0x22
#define CMD_WRITE_RAM_BW      0x24
#define CMD_WRITE_RAM_RED     0x26
#define CMD_BORDER_WAVEFORM   0x3C
#define CMD_RAM_X_START_END   0x44
#define CMD_RAM_Y_START_END   0x45
#define CMD_RAM_X_COUNTER     0x4E
#define CMD_RAM_Y_COUNTER     0x4F

/* 0x22 组合值 */
#define MODE_FULL       0xF7  /* 载温度+LUT，全刷 */
#define MODE_FAST       0xC7  /* 不载 LUT，快刷 */
#define MODE_PARTIAL    0xFF  /* Display Mode 2，局刷 */

/* 0x21 第 1 字节 */
#define CTRL1_NORMAL    0x00  /* BW+RED 都参与波形（局刷） */
#define CTRL1_BYPASS    0x40  /* bypass RED（全刷） */

/* ------------------------------------------------------------- 模块状态 */
static epd1680_config_t g_cfg;
static spi_device_handle_t g_spi;
static bool g_inited;
static bool g_invert;
static uint8_t g_scratch[EPD1680_FRAME_BYTES];

/* ------------------------------------------------------------- 底层收发 */

static void delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void pin(gpio_num_t p, int level)
{
    gpio_set_level(p, level ? 1 : 0);
}

static void spi_send(const uint8_t *data, size_t len)
{
    if (len == 0) return;
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(g_spi, &t));
}

/* 一个事务：命令字节 + 可选参数/数据；CS 全程拉低（与 4.2" 驱动同理，
 * 中间抬 CS 会让 SSD1680 丢掉参数）。 */
static void cmd_data(uint8_t c, const uint8_t *d, size_t n)
{
    pin(g_cfg.dc, 0);
    pin(g_cfg.cs, 0);
    spi_send(&c, 1);
    if (n) {
        pin(g_cfg.dc, 1);
        spi_send(d, n);
    }
    pin(g_cfg.cs, 1);
}

static void cmd(uint8_t c)
{
    cmd_data(c, NULL, 0);
}

static void cmd1(uint8_t c, uint8_t d)
{
    cmd_data(c, &d, 1);
}

static void cmd2(uint8_t c, uint8_t d0, uint8_t d1)
{
    uint8_t b[2] = {d0, d1};
    cmd_data(c, b, 2);
}

/* 位极性处理：需要发送视图时，把数据取反后发出。 */
static const uint8_t *tx_view(const uint8_t *src, size_t len)
{
    if (!g_invert) return src;
    if (len > sizeof(g_scratch)) len = sizeof(g_scratch);
    for (size_t i = 0; i < len; i++) {
        g_scratch[i] = (uint8_t)~src[i];
    }
    return g_scratch;
}

/* ------------------------------------------------------------- BUSY */

static int busy_now(void)
{
    int lvl = gpio_get_level(g_cfg.busy);
    return g_cfg.busy_high_is_busy ? (lvl == 1) : (lvl == 0);
}

int epd1680_busy_level(void)
{
    return gpio_get_level(g_cfg.busy);
}

bool epd1680_wait_busy(uint32_t timeout_ms)
{
    int64_t start = esp_timer_get_time();
    /* 先给面板一点时间把 BUSY 拉起来，避免刚发完命令就读到旧的空闲电平 */
    delay_ms(10);
    while (busy_now()) {
        if ((esp_timer_get_time() - start) / 1000 > (int64_t)timeout_ms) {
            ESP_LOGW(TAG, "busy timeout %u ms (level=%d)", (unsigned)timeout_ms,
                     gpio_get_level(g_cfg.busy));
            return false;
        }
        delay_ms(5);
    }
    return true;
}

/* ------------------------------------------------------------- 复位/初始化 */

void epd1680_hw_reset(void)
{
    pin(g_cfg.rst, 1);
    delay_ms(10);
    pin(g_cfg.rst, 0);
    delay_ms(10);
    pin(g_cfg.rst, 1);
    delay_ms(20);
}

/* RAM 窗口 + 地址计数器（X 用字节坐标，Y 用像素坐标） */
static void set_window(int x, int y, int w, int h)
{
    /* 0x44 需要两个字节，必须在同一个事务里发完 */
    {
        uint8_t xb[2] = {(uint8_t)(x / 8), (uint8_t)((x + w - 1) / 8)};
        cmd_data(CMD_RAM_X_START_END, xb, 2);
    }
    {
        int y1 = y + h - 1;
        uint8_t yb[4] = {
            (uint8_t)(y & 0xFF), (uint8_t)((y >> 8) & 0xFF),
            (uint8_t)(y1 & 0xFF), (uint8_t)((y1 >> 8) & 0xFF),
        };
        cmd_data(CMD_RAM_Y_START_END, yb, 4);
    }
    {
        uint8_t xb[1] = {(uint8_t)(x / 8)};
        cmd_data(CMD_RAM_X_COUNTER, xb, 1);
    }
    {
        uint8_t yb[2] = {(uint8_t)(y & 0xFF), (uint8_t)((y >> 8) & 0xFF)};
        cmd_data(CMD_RAM_Y_COUNTER, yb, 2);
    }
}

/* 把一段紧密排列的窗口数据写进指定 RAM */
static void write_ram(uint8_t ram_cmd, const uint8_t *data, size_t len)
{
    cmd(ram_cmd);
    pin(g_cfg.dc, 1);
    pin(g_cfg.cs, 0);
    spi_send(tx_view(data, len), len);
    pin(g_cfg.cs, 1);
}

static void display_update(uint8_t ctrl1_byte1, uint8_t mode)
{
    cmd2(CMD_UPDATE_CTRL1, ctrl1_byte1, 0x00);
    cmd1(CMD_UPDATE_CTRL2, mode);
    cmd(CMD_MASTER_ACT);
}

bool epd1680_init(const epd1680_config_t *cfg)
{
    if (g_inited) {
        return true;   /* 重复 init 会打出 GPIO 冲突警告，直接返回 */
    }
    if (cfg) {
        g_cfg = *cfg;
    } else {
        static const epd1680_config_t def = EPD1680_DEFAULT_CONFIG();
        g_cfg = def;
    }

    ESP_LOGI(TAG, "init: sck=%d mosi=%d cs=%d dc=%d rst=%d busy=%d high_busy=%d",
             g_cfg.sck, g_cfg.mosi, g_cfg.cs, g_cfg.dc, g_cfg.rst,
             g_cfg.busy, g_cfg.busy_high_is_busy);

    gpio_config_t out = {
        .pin_bit_mask = (1ULL << g_cfg.cs) | (1ULL << g_cfg.dc) | (1ULL << g_cfg.rst),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out));

    gpio_config_t in = {
        .pin_bit_mask = (1ULL << g_cfg.busy),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = g_cfg.busy_high_is_busy ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE,
        .pull_down_en = g_cfg.busy_high_is_busy ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));

    pin(g_cfg.cs, 1);
    pin(g_cfg.dc, 1);
    pin(g_cfg.rst, 1);

    spi_bus_config_t bus = {
        .mosi_io_num = g_cfg.mosi,
        .miso_io_num = -1,
        .sclk_io_num = g_cfg.sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EPD1680_FRAME_BYTES + 16,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return false;
    }

    spi_device_interface_config_t dev = {
        .clock_speed_hz = g_cfg.spi_hz ? g_cfg.spi_hz : 4000000,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    if (g_spi == NULL) {
        err = spi_bus_add_device(SPI2_HOST, &dev, &g_spi);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "spi_bus_add_device: %s", esp_err_to_name(err));
            return false;
        }
    }

    epd1680_hw_reset();

    /* 软复位 */
    cmd(CMD_SW_RESET);
    if (!epd1680_wait_busy(2000)) {
        ESP_LOGE(TAG, "no BUSY after SW reset (BUSY stuck high?)");
        return false;
    }

    /* 0x01 MUX = 295（296 条 gate），byte3 = 0x00 (GD=0,SM=0,TB=0) */
    {
        uint8_t b[3] = {
            (uint8_t)((EPD1680_HEIGHT - 1) & 0xFF),
            (uint8_t)(((EPD1680_HEIGHT - 1) >> 8) & 0xFF),
            0x00,
        };
        cmd_data(CMD_DRIVER_OUTPUT, b, 3);
    }

    cmd1(CMD_DATA_ENTRY, 0x03);        /* X++, Y++ */
    cmd1(CMD_TEMP_SENSOR, 0x80);       /* 内部温度传感器 */
    cmd1(CMD_BORDER_WAVEFORM, 0x80);   /* 边框跟随 VCOM */

    /* 全屏窗口 */
    {
        uint8_t xb[2] = {0x00, (uint8_t)(EPD1680_BYTES_PER_ROW - 1)};
        cmd_data(CMD_RAM_X_START_END, xb, 2);
        uint8_t yb[4] = {0x00, 0x00,
                         (uint8_t)((EPD1680_HEIGHT - 1) & 0xFF),
                         (uint8_t)(((EPD1680_HEIGHT - 1) >> 8) & 0xFF)};
        cmd_data(CMD_RAM_Y_START_END, yb, 4);
        uint8_t xc[1] = {0x00};
        cmd_data(CMD_RAM_X_COUNTER, xc, 1);
        uint8_t yc[2] = {0x00, 0x00};
        cmd_data(CMD_RAM_Y_COUNTER, yc, 2);
    }

    g_inited = true;
    ESP_LOGI(TAG, "init done, BUSY level=%d", epd1680_busy_level());
    return true;
}

/* ------------------------------------------------------------- 刷新 */

bool epd1680_full(const uint8_t *fb, uint32_t timeout_ms)
{
    if (!g_inited || fb == NULL) return false;
    set_window(0, 0, EPD1680_WIDTH, EPD1680_HEIGHT);
    write_ram(CMD_WRITE_RAM_BW, fb, EPD1680_FRAME_BYTES);
    write_ram(CMD_WRITE_RAM_RED, fb, EPD1680_FRAME_BYTES);
    display_update(CTRL1_BYPASS, MODE_FULL);
    return epd1680_wait_busy(timeout_ms);
}

bool epd1680_fast(const uint8_t *fb, uint32_t timeout_ms)
{
    if (!g_inited || fb == NULL) return false;
    /* 实测（2026-11-05）：0x22=0xC7「不载 LUT」的波形取决于上一条命令残留的
     * LUT，时间在 463 ms 与 2018 ms 之间跳变，不可作为稳定接口。
     * 全屏局刷（0xFF）实测 468 ms 且行为确定，因此这里直接用整屏窗口局刷。 */
    return epd1680_partial(fb, 0, 0, EPD1680_WIDTH, EPD1680_HEIGHT, timeout_ms);
}

bool epd1680_partial(const uint8_t *win, int x, int y, int w, int h,
                     uint32_t timeout_ms)
{
    if (!g_inited || win == NULL) return false;
    if (x < 0 || y < 0 || w <= 0 || h <= 0) return false;
    if (x + w > EPD1680_WIDTH || y + h > EPD1680_HEIGHT) return false;
    if ((x % 8) || (w % 8)) {
        ESP_LOGW(TAG, "partial: x=%d w=%d must be multiples of 8", x, w);
        return false;
    }

    size_t len = (size_t)(w / 8) * (size_t)h;
    set_window(x, y, w, h);
    write_ram(CMD_WRITE_RAM_BW, win, len);
    display_update(CTRL1_NORMAL, MODE_PARTIAL);
    if (!epd1680_wait_busy(timeout_ms)) {
        return false;
    }

    /* 刷新完把「当前显示内容」写回 RED RAM，作为下一次局刷的基准 */
    set_window(x, y, w, h);
    write_ram(CMD_WRITE_RAM_RED, win, len);
    return true;
}

void epd1680_sleep(void)
{
    if (!g_inited) return;
    cmd1(CMD_DEEP_SLEEP, 0x01);
    g_inited = false;
    ESP_LOGI(TAG, "deep sleep");
}

void epd1680_set_invert(bool on)
{
    g_invert = on;
}

bool epd1680_get_invert(void)
{
    return g_invert;
}

/* ------------------------------------------------------------- 绘图 */

void epd1680_fb_clear(uint8_t *fb, uint8_t color)
{
    memset(fb, color ? 0xFF : 0x00, EPD1680_FRAME_BYTES);
}

void epd1680_fb_pixel(uint8_t *fb, int x, int y, uint8_t color)
{
    if (x < 0 || x >= EPD1680_WIDTH || y < 0 || y >= EPD1680_HEIGHT) return;
    uint8_t mask = (uint8_t)(0x80 >> (x & 7));
    uint8_t *p = &fb[y * EPD1680_BYTES_PER_ROW + (x >> 3)];
    if (color) {
        *p |= mask;
    } else {
        *p &= (uint8_t)~mask;
    }
}

void epd1680_fb_hline(uint8_t *fb, int x, int y, int w, uint8_t color)
{
    for (int i = 0; i < w; i++) epd1680_fb_pixel(fb, x + i, y, color);
}

void epd1680_fb_vline(uint8_t *fb, int x, int y, int h, uint8_t color)
{
    for (int i = 0; i < h; i++) epd1680_fb_pixel(fb, x, y + i, color);
}

void epd1680_fb_rect(uint8_t *fb, int x, int y, int w, int h, uint8_t color)
{
    if (w <= 0 || h <= 0) return;
    epd1680_fb_hline(fb, x, y, w, color);
    epd1680_fb_hline(fb, x, y + h - 1, w, color);
    epd1680_fb_vline(fb, x, y, h, color);
    epd1680_fb_vline(fb, x + w - 1, y, h, color);
}

void epd1680_fb_fill_rect(uint8_t *fb, int x, int y, int w, int h, uint8_t color)
{
    for (int j = 0; j < h; j++) epd1680_fb_hline(fb, x, y + j, w, color);
}

int epd1680_fb_text_width(const char *str)
{
    return (int)strlen(str) * EPD_FONT_W;
}

static void draw_glyph(uint8_t *fb, int x, int y, char ch, uint8_t color, int scale)
{
    if (ch < EPD_FONT_FIRST || ch > EPD_FONT_LAST) ch = '?';
    const uint8_t *g = font5x7[ch - EPD_FONT_FIRST];
    for (int col = 0; col < EPD_FONT_W; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < EPD_FONT_HEIGHT; row++) {
            if (!(bits & (1u << row))) continue;
            if (scale == 1) {
                epd1680_fb_pixel(fb, x + col, y + row, color);
            } else {
                epd1680_fb_fill_rect(fb, x + col * scale, y + row * scale,
                                     scale, scale, color);
            }
        }
    }
}

void epd1680_fb_text(uint8_t *fb, int x, int y, const char *str, uint8_t color)
{
    int cx = x;
    for (const char *p = str; *p; p++) {
        draw_glyph(fb, cx, y, *p, color, 1);
        cx += EPD_FONT_W;
    }
}

void epd1680_fb_text_scaled(uint8_t *fb, int x, int y, const char *str,
                            uint8_t color, int scale)
{
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    int cx = x;
    for (const char *p = str; *p; p++) {
        draw_glyph(fb, cx, y, *p, color, scale);
        cx += EPD_FONT_W * scale;
    }
}

const uint8_t *epd1680_ascii_glyph(char ch)
{
    if (ch < EPD_FONT_FIRST || ch > EPD_FONT_LAST) ch = '?';
    return font5x7[ch - EPD_FONT_FIRST];
}

int epd1680_fb_text_width_scaled(const char *str, int scale)
{
    if (scale < 1) scale = 1;
    return (int)strlen(str) * EPD_FONT_W * scale;
}
