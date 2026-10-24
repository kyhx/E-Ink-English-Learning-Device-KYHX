/*
 * epd_ssd1619a.c
 *
 * 4.2 寸单色电子墨水屏驱动 —— SSD1619A (400 x 300, 1bpp)
 * ESP32-S3-N16R8 / ESP-IDF v6.1 / 墨水屏驱动板 V1.5
 *
 * 初始化序列参考 Solomon Systech SSD1619A datasheet 与
 * Good Display GDEH042Z96 (400x300 mono) 参考程序。
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
#include "epd_ssd1619a.h"
#include "../epd_font_5x7.h"

static const char *TAG = "epd_ssd1619a";

static epd_config_t g_cfg;
static spi_device_handle_t g_spi;
static uint8_t g_zero[EPD_FRAME_BYTES];   /* 用于 RED 平面清零 */
static uint8_t g_part_tmp[EPD_FRAME_BYTES];

/* ------------------------------------------------------------------ 底层 */

static void epd_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void epd_write_pin(gpio_num_t pin, int level)
{
    gpio_set_level(pin, level ? 1 : 0);
}

static void epd_spi_send(const uint8_t *data, size_t len)
{
    if (len == 0) return;
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(g_spi, &t));
}

/* 发送一个完整事务：命令 + 可选参数/数据。
 *
 * datasheet Table 6-2 / Figure 6-1：4-wire SPI 的 CS# 必须从命令字节
 * 一直保持低电平到最后一个参数字节；D/C# 只在字节之间切换。
 * 如果在命令和参数之间抬起 CS，SSD1619A 会丢掉参数/数据，
 * 典型现象是 0x24 写 RAM 完全无效、屏幕保持全白。 */
static void epd_cmd_data(uint8_t c, const uint8_t *d, size_t n)
{
    epd_write_pin(g_cfg.dc, 0);
    epd_write_pin(g_cfg.cs, 0);
    epd_spi_send(&c, 1);
    if (n) {
        epd_write_pin(g_cfg.dc, 1);
        epd_spi_send(d, n);
    }
    epd_write_pin(g_cfg.cs, 1);
}

/* 无参数命令 */
static void epd_cmd(uint8_t c)
{
    epd_cmd_data(c, NULL, 0);
}

/* 单字节参数命令 */
static void epd_cmd_byte(uint8_t c, uint8_t d)
{
    epd_cmd_data(c, &d, 1);
}

/* BUSY 极性可配置：SSD1619A 手册为"忙时高电平"(active HIGH)，
   但部分驱动板带反相器，故做成可配置。 */
static bool epd_busy_now(void)
{
    int lvl = gpio_get_level(g_cfg.busy);
    return g_cfg.busy_high_is_busy ? (lvl == 1) : (lvl == 0);
}

bool epd_wait_busy(uint32_t timeout_ms)
{
    int64_t start = esp_timer_get_time();
    /* 先等一个极短时间，避开 BUSY 尚未拉高的窗口 */
    epd_delay_ms(10);
    while (epd_busy_now()) {
        if ((esp_timer_get_time() - start) / 1000 > (int64_t)timeout_ms) {
            ESP_LOGW(TAG, "wait busy timeout after %u ms", timeout_ms);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return true;
}

/* ------------------------------------------------------------------ 初始化 */

static void epd_hardware_reset(void)
{
    epd_write_pin(g_cfg.rst, 1);
    epd_delay_ms(20);
    epd_write_pin(g_cfg.rst, 0);
    epd_delay_ms(10);
    epd_write_pin(g_cfg.rst, 1);
    epd_delay_ms(20);
}

/* RAM 窗口 + 地址计数器
 *
 * SSD1619A datasheet 9.1 Normal Display 官方流程：
 *   C 44  RAM X address start/end   -- X 用「字节」坐标（S0~S399 -> 0x00~0x31）
 *   C 45  RAM Y address start/end   -- Y 用「像素」坐标（G0~G299 -> 0x0000~0x012B）
 *   C 4E  RAM X address counter
 *   C 4F  RAM Y address counter
 *
 * 注意：写完 RAM 后地址计数器按 AM/ID 自动递增，无需重复设置；
 * 但每次写 RAM 前必须重新下发窗口，否则会显示错误图像。
 */
static void epd_set_ram_window(int x, int y, int w, int h)
{
    uint8_t a[4];

    /* 0x44 RAM X start/end (字节坐标) */
    a[0] = (uint8_t)(x / 8);
    a[1] = (uint8_t)((x + w - 1) / 8);
    epd_cmd_data(0x44, a, 2);

    /* 0x45 RAM Y start/end (像素坐标) */
    a[0] = (uint8_t)(y & 0xFF);
    a[1] = (uint8_t)((y >> 8) & 0xFF);
    a[2] = (uint8_t)((y + h - 1) & 0xFF);
    a[3] = (uint8_t)(((y + h - 1) >> 8) & 0xFF);
    epd_cmd_data(0x45, a, 4);

    /* 0x4E RAM X address counter */
    a[0] = (uint8_t)(x / 8);
    epd_cmd_data(0x4E, a, 1);

    /* 0x4F RAM Y address counter */
    a[0] = (uint8_t)(y & 0xFF);
    a[1] = (uint8_t)((y >> 8) & 0xFF);
    epd_cmd_data(0x4F, a, 2);
}

/* SSD1619A 上电初始化
 *
 * 严格按 datasheet 9.1「Normal Display」流程：
 *   HW Reset -> 0x12 SWRESET -> 等 BUSY=L
 *   -> 0x74/0x7E 模拟块/数字块 -> 0x01 驱动输出控制
 *   -> 0x3A/0x3B 哑行周期/栅线宽 -> 0x3C 边框波形
 *   -> 0x11/0x44/0x45/0x4E/0x4F 数据窗口
 */
static bool epd_panel_init(void)
{
    uint8_t a[4];

    /* --- 0x12 SWRESET 后必须等 BUSY 变低 --- */
    epd_cmd(0x12);
    if (!epd_wait_busy(5000)) {
        ESP_LOGE(TAG, "SWRESET busy timeout");
        return false;
    }

    /* Set Analog Block Control */
    epd_cmd_byte(0x74, 0x54);
    /* Set Digital Block Control */
    epd_cmd_byte(0x7E, 0x3B);

    /* ACVCOM 毛刺抑制（GDEH042Z96 参考初始化） */
    a[0] = 0x04; a[1] = 0x63;
    epd_cmd_data(0x2B, a, 2);

    /* Soft start setting（GDEH042Z96 参考初始化） */
    a[0] = 0x8B; a[1] = 0x9C; a[2] = 0x96; a[3] = 0x0F;
    epd_cmd_data(0x0C, a, 4);

    /* 驱动输出控制：MUX ratio 决定扫描行数（0x012B = 299 -> 300 行）
       第 3 字节 = Source/Gate 扫描方向，0x00 为 G0->G299 向下扫描 */
    a[0] = 0x2B; a[1] = 0x01; a[2] = 0x00;
    epd_cmd_data(0x01, a, 3);

    /* 哑行周期 / 栅线宽度：用 datasheet 400x300 表的 50Hz 值 */
    epd_cmd_byte(0x3A, 0x2C);
    epd_cmd_byte(0x3B, 0x0A);

    /* 边框波形：0x01 = 使用 GS 过渡波形（显示边框）*/
    epd_cmd_byte(0x3C, 0x01);

    /* 温度传感器：内部温度计 0x80 */
    epd_cmd_byte(0x18, 0x80);

    /* 关键：让芯片从 OTP 加载温度和波形设置。
       GDEH042Z96 参考程序在初始化阶段发 0x22=0xB1 + 0x20，
       这一步可能耗时约 16 秒；漏掉它会导致 BUSY 正常但屏幕不显示。 */
    epd_cmd_byte(0x22, 0xB1);
    epd_cmd(0x20);
    if (!epd_wait_busy(20000)) {
        ESP_LOGE(TAG, "OTP waveform load timeout");
        return false;
    }

    /* 数据进入模式：X 递增 / Y 递增 */
    epd_cmd_byte(0x11, 0x03);

    /* 全屏 RAM 窗口 + 地址计数器 */
    epd_set_ram_window(0, 0, EPD_WIDTH, EPD_HEIGHT);

    /* Display Update Control 1：datasheet p.25 只有一个参数字节
       A[7:4]=0100 -> RED RAM bypass as 0
       A[3:0]=0000 -> BW RAM normal
       单色屏只写 0x24，因此 RED 平面旁路，BW 保持正常。 */
    a[0] = 0x40;
    epd_cmd_data(0x21, a, 1);
    return true;
}

/* 触发一次显示更新
 *
 * SSD1619A 的 0x22（Display Update Control 2）不是「刷新波形选择」，
 * 而是 datasheet p.26 定义的一组显示更新序列：
 *   0xC7  全刷 Mode1：clock -> analog -> DISPLAY Mode1 -> disable analog/clock
 *   0xCF  全刷 Mode2
 *   0x01  只关时钟；0x03 关模拟块 + 时钟
 *
 * 注意：0xF7 是 SSD1683 的常用全刷值，但在 SSD1619A 上包含
 * “从 I2C 读温度 / 加载 LUT”阶段，实测会让 BUSY 一直不释放，不能用。
 * 0xC7/0xCF 之后必须跟 0x20 (Master Activation) 才会真正执行。
 */
static void epd_trigger_update(uint8_t ctrl2)
{
    epd_cmd_byte(0x22, ctrl2);
    epd_cmd(0x20);                 /* Master Activation */
    if (!epd_wait_busy(30000)) {
        ESP_LOGW(TAG, "display update busy timeout");
    }
}

/* 0x22=0x80 + 0x20：重新使能时钟。
 * 上一次 0xC7 刷新结束时已经 Disable Clock，直接写 RAM 会被忽略，
 * 因此每次写 RAM 前必须先开时钟。 */
static bool epd_clock_on(void)
{
    epd_cmd_byte(0x22, 0x80);
    epd_cmd(0x20);
    return epd_wait_busy(5000);
}

void epd_init(const epd_config_t *cfg)
{
    if (cfg == NULL) {
        static const epd_config_t def = EPD_S3_DEFAULT_CONFIG();
        g_cfg = def;
    } else {
        g_cfg = *cfg;
    }

    ESP_LOGI(TAG, "init: sck=%d mosi=%d cs=%d dc=%d rst=%d busy=%d busy_high=%d",
             g_cfg.sck, g_cfg.mosi, g_cfg.cs, g_cfg.dc, g_cfg.rst,
             g_cfg.busy, g_cfg.busy_high_is_busy);

    /* GPIO */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << g_cfg.cs) | (1ULL << g_cfg.dc) | (1ULL << g_cfg.rst),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    /* BUSY 输入：SSD1619A 为推挽输出，但驱动板可能没有外部上下拉。
     * 按极性启用弱上下拉，保证浮空时读到的是「空闲」电平：
     *   高=忙 -> 下拉；低=忙 -> 上拉。 */
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << g_cfg.busy),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = g_cfg.busy_high_is_busy ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE,
        .pull_down_en = g_cfg.busy_high_is_busy ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));

    epd_write_pin(g_cfg.cs, 1);
    epd_write_pin(g_cfg.dc, 1);
    epd_write_pin(g_cfg.rst, 1);

    /* SPI2_HOST，任意引脚经 GPIO 矩阵映射 */
    spi_bus_config_t bus = {
        .mosi_io_num = g_cfg.mosi,
        .miso_io_num = -1,
        .sclk_io_num = g_cfg.sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EPD_FRAME_BYTES + 16,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {
        .clock_speed_hz = g_cfg.spi_hz ? g_cfg.spi_hz : 4000000,
        .mode = 0,              /* SSD1619A: CPOL=0, CPHA=0 (mode 0) */
        .spics_io_num = -1,     /* 片选由软件控制 */
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &g_spi));

    epd_hardware_reset();
    if (!epd_panel_init()) {
        ESP_LOGE(TAG, "panel init failed");
        return;
    }
    if (!epd_wait_busy(5000)) {
        ESP_LOGW(TAG, "panel still busy after init");
    }
}

/* ------------------------------------------------------------------ 显示 */

/* 写帧到 RAM
 *
 * fb 必须是「紧密排列」的区域数据：每行 (w/8) 字节，共 (w/8)*h 字节。
 * 注意：SSD1619A 的 RAM X 窗口是字节坐标，写入时每行只推进 w/8 字节，
 * 所以这里不能用整屏的行宽 50 字节。
 */
static void epd_write_fb(const uint8_t *fb, int x, int y, int w, int h)
{
    if (fb == NULL) return;
    epd_cmd_byte(0x11, 0x03);     /* X inc / Y inc */
    epd_set_ram_window(x, y, w, h);
    epd_cmd_data(0x24, fb, (size_t)(w / 8) * h);
}

void epd_display(const uint8_t *fb)
{
    if (fb == NULL) return;
    if (!epd_wait_busy(5000)) {
        ESP_LOGE(TAG, "busy timeout before full refresh");
        return;
    }
    if (!epd_clock_on()) {
        ESP_LOGE(TAG, "clock enable timeout");
        return;
    }

    /* 0x21 Display Update Control 1：单字节，RED bypass / BW normal */
    uint8_t a = 0x40;
    epd_cmd_data(0x21, &a, 1);

    epd_write_fb(fb, 0, 0, EPD_WIDTH, EPD_HEIGHT);

    /* 0x22 = 0xC7（datasheet p.26）：
       Enable Clock -> Enable Analog -> DISPLAY Mode 1
       -> Disable Analog -> Disable Clock。
       Master Activation 会从 OTP 加载对应波形，不需要 MCU 指定 LUT。 */
    epd_trigger_update(0xC7);
}

bool epd_display_area_ex(const uint8_t *fb, int x, int y, int w, int h,
                         uint8_t ctrl2, uint32_t timeout_ms)
{
    if (fb == NULL) return false;
    if (x < 0 || y < 0 || w <= 0 || h <= 0) return false;
    if (x + w > EPD_WIDTH || y + h > EPD_HEIGHT) return false;

    /* RAM X 是字节坐标，x 与 w 必须是 8 的倍数。
     * 这里不做静默对齐，避免调用方缓冲区与屏幕区域错位。 */
    if ((x & 7) || (w & 7)) {
        ESP_LOGE(TAG, "x and w must be multiples of 8 (x=%d w=%d)", x, w);
        return false;
    }

    if (!epd_wait_busy(5000)) {
        ESP_LOGE(TAG, "busy timeout before area refresh");
        return false;
    }
    if (!epd_clock_on()) {
        ESP_LOGE(TAG, "clock enable timeout");
        return false;
    }

    uint8_t a = 0x40;
    epd_cmd_data(0x21, &a, 1);

    epd_write_fb(fb, x, y, w, h);

    epd_cmd_byte(0x22, ctrl2);
    epd_cmd(0x20);                 /* Master Activation */
    return epd_wait_busy(timeout_ms);
}

void epd_display_area(const uint8_t *fb, int x, int y, int w, int h)
{
    (void)epd_display_area_ex(fb, x, y, w, h, 0xC7, 30000);
}

/* 三色/双平面测试：同时写 BW(0x24) 和 RED(0x26) 两个平面。
 * 0x21=0x00 表示两个平面都正常使用（不是 0x40 的 RED bypass）。
 * ctrl2 可指定 0xC7/0xFF 等，timeout_ms 用于实验。 */
bool epd_partial_init(const uint8_t *lut)
{
    if (lut == NULL) return false;

    /* EPaperDrive / HINK-E042A13-A0 的局刷初始化序列 */
    epd_hardware_reset();
    epd_cmd_byte(0x74, 0x54);
    epd_cmd_byte(0x7E, 0x3B);
    const uint8_t drv[3] = { 0x2B, 0x01, 0x00 };
    epd_cmd_data(0x01, drv, 3);
    const uint8_t soft[4] = { 0x8B, 0x9C, 0xD6, 0x0F };
    epd_cmd_data(0x0C, soft, 4);
    epd_cmd_byte(0x3A, 0x21);
    epd_cmd_byte(0x3B, 0x06);
    epd_cmd_byte(0x3C, 0x03);
    epd_cmd_byte(0x11, 0x03);
    epd_cmd_byte(0x2C, 0x00);
    const uint8_t otp[5] = { 0x00, 0x00, 0x00, 0x00, 0x80 };
    epd_cmd_data(0x37, otp, 5);
    epd_cmd_byte(0x21, 0x40);
    epd_cmd_byte(0x22, 0xC7);
    epd_cmd_byte(0x21, 0x00);
    epd_cmd_data(0x32, lut, 70);
    return true;
}

/* 已进入 partial 模式后的区域更新（不重新初始化）。 */
bool epd_partial_update(const uint8_t *area, int x, int y, int w, int h,
                        uint32_t timeout_ms)
{
    if (area == NULL) return false;
    if (x < 0 || y < 0 || w <= 0 || h <= 0) return false;
    if (x + w > EPD_WIDTH || y + h > EPD_HEIGHT) return false;
    if ((x & 7) || (w & 7)) return false;

    /* 恢复时钟；否则 0x44/0x45 窗口命令可能不生效，导致整屏刷新。 */
    if (!epd_clock_on()) return false;
    epd_cmd_byte(0x22, 0xC7);
    epd_cmd_byte(0x11, 0x03);
    epd_set_ram_window(x, y, w, h);
    epd_cmd_data(0x24, area, (size_t)(w / 8) * h);
    epd_cmd(0x20);
    if (!epd_wait_busy(timeout_ms)) return false;

    /* 原版 EPD_Dis_Part 在刷新后再写一次同样的数据，作为下一次
     * 局刷的基线；缺少这一步时连续刷新容易出现奇偶帧异常。 */
    epd_cmd_byte(0x11, 0x03);
    epd_set_ram_window(x, y, w, h);
    epd_cmd_data(0x24, area, (size_t)(w / 8) * h);
    return true;
}

/* 完全按 EPaperDrive 的 EPD_Dis_Part 语义：
 *  - 传整帧 buffer，按 offset 取窗口
 *  - x/y 交换 + Y 反向窗口
 *  - 0x11=0x01（Y 递减）
 *  - 刷新后补写一次基线
 * 物理区域为 [x0..x1] x [y0..y1]，x0 必须 8 对齐，宽度为 8 的倍数。 */
bool epd_epd_dis_part(const uint8_t *frame, int x0, int x1, int y0, int y1,
                      uint32_t timeout_ms)
{
    if (frame == NULL) return false;
    if (x0 < 0 || y0 < 0 || x1 >= EPD_WIDTH || y1 >= EPD_HEIGHT) return false;
    if (x1 <= x0 || y1 <= y0) return false;
    if ((x0 & 7) || (((x1 - x0 + 1) & 7) != 0)) return false;

    int w = x1 - x0 + 1;
    int h = y1 - y0 + 1;

    /* EPaperDrive 的输入参数映射 */
    int a = 298 - y1;
    int b = 297 - y0;
    int c = x0;
    int d = x1;

    int xs = c;
    int xe = d;
    int ye = 300 - a - 2;      /* = y1 */
    int ys = 300 - b - 3;      /* = y0 */
    int tmp = ys;
    ys = 299 - ye;             /* = 299 - y1 */
    ye = 299 - tmp;            /* = 299 - y0 */

    /* 从整帧取窗口数据；Y 递减模式下按 y1 -> y0 反向取行 */
    for (int row = 0; row < h; row++) {
        memcpy(&g_part_tmp[row * (w / 8)],
               &frame[(y1 - row) * EPD_BYTES_PER_ROW + x0 / 8],
               w / 8);
    }

    if (!epd_clock_on()) return false;
    epd_cmd_byte(0x22, 0xC7);
    epd_cmd_byte(0x11, 0x01);

    uint8_t xa[2] = { (uint8_t)(xs / 8), (uint8_t)(xe / 8) };
    epd_cmd_data(0x44, xa, 2);
    uint8_t ya[4] = {
        (uint8_t)(ye & 0xFF), (uint8_t)((ye >> 8) & 0xFF),
        (uint8_t)(ys & 0xFF), (uint8_t)((ys >> 8) & 0xFF)
    };
    epd_cmd_data(0x45, ya, 4);
    epd_cmd_byte(0x4E, (uint8_t)(xs / 8));
    uint8_t yc[2] = { (uint8_t)(ye & 0xFF), (uint8_t)((ye >> 8) & 0xFF) };
    epd_cmd_data(0x4F, yc, 2);

    epd_cmd_data(0x24, g_part_tmp, (size_t)(w / 8) * h);
    epd_cmd(0x20);
    if (!epd_wait_busy(timeout_ms)) return false;

    /* 补写基线 */
    epd_cmd_byte(0x11, 0x01);
    epd_cmd_data(0x44, xa, 2);
    epd_cmd_data(0x45, ya, 4);
    epd_cmd_byte(0x4E, (uint8_t)(xs / 8));
    epd_cmd_data(0x4F, yc, 2);
    epd_cmd_data(0x24, g_part_tmp, (size_t)(w / 8) * h);
    return true;
}

/* 重新加载 partial LUT 并恢复 0x21=0x00；清洁周期后使用。 */
bool epd_partial_reload_lut(const uint8_t *lut)
{
    if (lut == NULL) return false;
    if (!epd_clock_on()) return false;
    epd_cmd_byte(0x21, 0x00);
    epd_cmd_data(0x32, lut, 70);
    epd_cmd_byte(0x22, 0xC7);
    return true;
}

/* 已进入 partial 模式后重画整屏底图（约 700 ms）。 */
bool epd_partial_show_full(const uint8_t *fb, uint32_t timeout_ms)
{
    if (fb == NULL) return false;
    if (!epd_clock_on()) return false;
    epd_cmd_byte(0x22, 0xC7);
    epd_cmd_byte(0x11, 0x03);
    epd_set_ram_window(0, 0, EPD_WIDTH, EPD_HEIGHT);
    epd_cmd_data(0x24, fb, EPD_FRAME_BYTES);
    epd_cmd(0x20);
    return epd_wait_busy(timeout_ms);
}

void epd_fill(uint8_t color)
{
    static uint8_t fb[EPD_FRAME_BYTES];
    memset(fb, color ? 0xFF : 0x00, sizeof(fb));
    epd_display(fb);
}

void epd_deep_sleep(void)
{
    /* 0x10 Deep Sleep Mode，A[1:0] = 01 -> Deep Sleep Mode 1
     * 保留 RAM 内容，静态电流约 1~5 uA。退出深睡需要硬件复位。
     * 注意：进入深睡后 BUSY 脚会一直输出高电平。 */
    uint8_t mode = 0x01;
    if (!epd_wait_busy(5000)) {
        ESP_LOGE(TAG, "busy timeout before deep sleep");
        return;
    }
    epd_cmd_data(0x10, &mode, 1);
    epd_delay_ms(100);
    epd_write_pin(g_cfg.cs, 1);
    epd_write_pin(g_cfg.dc, 0);
}

/* ------------------------------------------------------------------ 绘图 */

/* 帧缓冲位序：MSB 在左，第 x 像素在 byte[x/8] 的 bit (7 - x%8) */
void epd_fb_clear(uint8_t *fb, uint8_t color)
{
    if (fb) memset(fb, color ? 0xFF : 0x00, EPD_FRAME_BYTES);
}

void epd_fb_pixel(uint8_t *fb, int x, int y, uint8_t color)
{
    if (!fb || x < 0 || y < 0 || x >= EPD_WIDTH || y >= EPD_HEIGHT) return;
    uint8_t mask = (uint8_t)(0x80 >> (x & 7));
    uint8_t *p = &fb[y * EPD_BYTES_PER_ROW + (x >> 3)];
    if (color) *p |= mask;
    else      *p &= (uint8_t)~mask;
}

void epd_fb_hline(uint8_t *fb, int x, int y, int w, uint8_t color)
{
    for (int i = 0; i < w; i++) epd_fb_pixel(fb, x + i, y, color);
}

void epd_fb_vline(uint8_t *fb, int x, int y, int h, uint8_t color)
{
    for (int i = 0; i < h; i++) epd_fb_pixel(fb, x, y + i, color);
}

void epd_fb_rect(uint8_t *fb, int x, int y, int w, int h, uint8_t color)
{
    if (w <= 0 || h <= 0) return;
    epd_fb_hline(fb, x, y, w, color);
    epd_fb_hline(fb, x, y + h - 1, w, color);
    epd_fb_vline(fb, x, y, h, color);
    epd_fb_vline(fb, x + w - 1, y, h, color);
}

void epd_fb_fill_rect(uint8_t *fb, int x, int y, int w, int h, uint8_t color)
{
    for (int j = 0; j < h; j++) epd_fb_hline(fb, x, y + j, w, color);
}

void epd_fb_text(uint8_t *fb, int x, int y, const char *str, uint8_t color)
{
    if (!fb || !str) return;
    int cx = x;
    for (; *str; str++) {
        unsigned char ch = (unsigned char)*str;
        if (ch < EPD_FONT_FIRST || ch > EPD_FONT_LAST) ch = '?';
        const uint8_t *g = font5x7[ch - EPD_FONT_FIRST];
        for (int col = 0; col < EPD_FONT_W; col++) {
            for (int row = 0; row < EPD_FONT_HEIGHT; row++) {
                /* 每字节 bit0 = 最上行，bit7 = 下伸行 */
                if (g[col] & (1 << row)) {
                    epd_fb_pixel(fb, cx + col, y + row, color);
                }
            }
        }
        cx += EPD_FONT_W + 1;
    }
}

int epd_fb_text_width(const char *str)
{
    if (!str) return 0;
    int n = 0;
    for (; *str; str++) n++;
    return n ? n * (EPD_FONT_W + 1) - 1 : 0;
}

void epd_fb_text_scaled(uint8_t *fb, int x, int y, const char *str,
                        uint8_t color, int scale)
{
    if (!fb || !str) return;
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    int cx = x;
    for (; *str; str++) {
        unsigned char ch = (unsigned char)*str;
        if (ch < EPD_FONT_FIRST || ch > EPD_FONT_LAST) ch = '?';
        const uint8_t *g = font5x7[ch - EPD_FONT_FIRST];
        for (int col = 0; col < EPD_FONT_W; col++) {
            for (int row = 0; row < EPD_FONT_HEIGHT; row++) {
                if (g[col] & (1 << row)) {
                    for (int dy = 0; dy < scale; dy++) {
                        for (int dx = 0; dx < scale; dx++) {
                            epd_fb_pixel(fb, cx + col * scale + dx,
                                         y + row * scale + dy, color);
                        }
                    }
                }
            }
        }
        cx += (EPD_FONT_W + 1) * scale;
    }
}

int epd_fb_text_width_scaled(const char *str, int scale)
{
    if (!str) return 0;
    if (scale < 1) scale = 1;
    int n = 0;
    for (; *str; str++) n++;
    return n ? (n * (EPD_FONT_W + 1) - 1) * scale : 0;
}
