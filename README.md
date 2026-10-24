# E-Ink 英语学习机（ESP32-S3 + 4.2" 墨水屏）

基于 **ESP32-S3-N16R8 + 墨水屏驱动板 V1.5 + 4.2 寸单色电子墨水屏（SSD1619A，400×300）** 的
电子墨水屏英语单词学习终端固件，使用 **ESP-IDF v6.1 / C** 开发。

纸质单词书内容固定、检索不便，难以按遗忘规律安排复习，携带与翻阅成本较高；手机背单词 App
虽然检索与调度能力强，但依赖高亮背光屏幕，长时间近距离使用易造成视觉疲劳，且功能高度依赖
网络与云端服务，在教室、通勤、自习等场景下的专注度容易被消息推送打断。
电子墨水屏（E-Paper / E-Ink）具有双稳态特性：仅在刷新时消耗电能，断电后画面保持不变；
反射式成像接近纸张观感，无背光直射，适合长时间阅读。将其用于单词学习终端，
可在功耗与视觉舒适度上同时优于手机方案。

---

## 1. 当前状态

已在本项目的硬件上完成实测（2026-10）：

| 项目 | 结果 |
| --- | --- |
| 编译 | ESP-IDF v6.1 + xtensa-esp-elf，零 error |
| 面板初始化 / 全白 / 全黑 / 文字 | 正常 |
| OTP 全刷 | 4–6 s，全屏闪 |
| **70 行带状真局刷** | **约 346 ms/次；带状外不动、横带不闪，只有文字变化** |
| 清洁周期（全白→全黑→底图） | 3 × 709 ms，可完全恢复残影 |
| 上电演示 | 50 个单词，每词 346 ms 带状刷新 + 1 s 停留，第 25 词清洁一次，结束显示 DONE 并深睡 |

> **当前方案：MUX=69 + gate start=95，只扫描 95..164 共 70 行；带状内写整条 400px 宽的数据，未变化像素不驱动。**
> 带状外完全不扫描、完全不动；横带不闪，只有单词/序号变化。
> 它不是任意 2D 窗口：X 方向仍全宽，但整宽数据保证横带不闪。详见 [`docs/局刷测试记录.md`](docs/局刷测试记录.md)、[`docs/真局部研究.md`](docs/真局部研究.md)。

---

## 2. 硬件与接线

驱动板 V1.5 丝印：`GND / 3V3 / SCK / SDA / RST / DC / CS1 / BUSY / CS2`（CS2 仅三色屏使用）。

| 驱动板丝印 | 含义 | ESP32-S3 GPIO | 备注 |
| --- | --- | --- | --- |
| CS1 | 片选（低有效） | GPIO10 | 软件片选 |
| DC | 数据/命令 | GPIO9 | 低=命令，高=数据 |
| RST | 复位（低有效） | GPIO8 | ≥10 ms 低脉冲 |
| BUSY | 忙信号 | GPIO7 | **高电平表示忙** |
| SCK | SPI 时钟 | GPIO12 | SPI2 IO_MUX 默认脚 |
| SDA | SPI MOSI | GPIO11 | 无需 MISO |
| 3V3 | 3.3 V | 3V3 | 切勿接 5 V |
| GND | 地 | GND | 必须共地 |
| CS2 | 三色屏片选 2 | — | 单色屏悬空 |

接线图：[`docs/wiring.png`](docs/wiring.png)。引脚可在
[`components/epd_ssd1619a/include/epd_ssd1619a.h`](components/epd_ssd1619a/include/epd_ssd1619a.h)
的 `EPD_S3_DEFAULT_CONFIG()` 中修改，或运行时传自定义 `epd_config_t`。

所选 7/8/9/10/11/12 均避开 strapping 脚（0/3/45/46）、USB-JTAG（19/20）、
Flash/PSRAM（26–37）与 UART0（43/44）。注意 GPIO11/12 属 ADC2，若后续要用 ADC 需换脚。

---

## 3. 目录结构

```text
E-Ink-English-Learning-Device-KYHX/
├── CMakeLists.txt                  # 工程顶层
├── sdkconfig.defaults              # esp32s3 / 16MB Flash / 8MB Octal PSRAM / 自定义分区
├── sdkconfig                       # 实测通过的完整配置（CPU 240MHz、FreeRTOS 1000Hz）
├── partitions.csv                  # nvs / phy_init / factory(3MB)
├── .clangd                         # clangd 过滤 -f* -m*
├── main/
│   ├── CMakeLists.txt
│   ├── main.c                      # app_main：初始化 / 清洁周期 / 单词循环 / 深睡
│   ├── word_view.c                 # 单词卡片布局与帧合成（纯内存绘制）
│   └── word_view.h
├── components/
│   └── epd_ssd1619a/               # 墨水屏驱动组件
│       ├── CMakeLists.txt
│       ├── epd_ssd1619a.c          # SPI / 初始化 / 刷新 / 绘图
│       ├── epd_font_5x7.h          # 5x7 ASCII 点阵字体（自动生成）
│       └── include/
│           ├── epd_ssd1619a.h      # 对外 API 与引脚配置
│           └── epd_lut_partial.h   # 70 字节 partial LUT（AGPL 来源，见第 8 节）
├── docs/
│   ├── 交接文档.md                 # 现状 / 踩坑 / 下一步（新人先读这个）
│   ├── 技术方案.md                 # 驱动方案与寄存器时序细节
│   ├── 局刷测试记录.md             # 全部刷新实验数据与波形分析
│   ├── 真局部研究.md               # 70 行带状真局刷的边界与 datasheet 依据
│   ├── HINK-E042A13-A0资料汇总.md  # 屏体资料汇总
│   ├── wiring.png / demo_preview.png / font_preview.png
│   └── datasheets/                 # SSD1619A、GDEH042Z96 规格书
└── tools/
    ├── png_to_raw.py               # PNG → 15000 字节 1bpp 帧
    ├── preview_demo.py             # 离线渲染屏显效果（无需硬件）
    ├── gen_font.py / font5x7_ascii.py / gen_wiring.py
```

---

## 4. 编译与烧录

需要 **ESP-IDF v5.3+**（本项目在 **v6.1** 上验证）与 Python 3.10+。

```bash
# Linux / macOS / Git Bash
. $IDF_PATH/export.sh
cd E-Ink-English-Learning-Device-KYHX
idf.py set-target esp32s3     # 首次，或删除 sdkconfig 后
idf.py build
idf.py -p COM4 flash monitor  # Ctrl+] 退出
```

Windows PowerShell：

```powershell
$env:IDF_PATH='F:\esp\v6.1\esp-idf'
. 'F:\esp\v6.1\esp-idf\export.ps1'
idf.py -p COM4 flash monitor
```

| 命令 | 作用 |
| --- | --- |
| `idf.py build` | 编译 |
| `idf.py flash` | 烧录 |
| `idf.py monitor` | 串口监视 |
| `idf.py fullclean` | 彻底清理 |

> API 依赖名：ESP-IDF **v5.3+ / v6.x** 用 `esp_driver_gpio` / `esp_driver_spi`；
> **v5.0–5.2** 需把 `main/CMakeLists.txt` 与 `components/epd_ssd1619a/CMakeLists.txt`
> 里的依赖改回 `driver`。

---

## 5. 帧数据格式

- 分辨率 **400 × 300**，1 bit/px，每行 **50** 字节，整帧 **15000** 字节
- 行优先，**MSB 在左**：像素 `x` 位于第 `x/8` 字节的第 `7 - x%8` 位
- **位值 1 = 白，0 = 黑**
- 单色屏只用 `0x24` Write RAM(B/W)，**不要**用 `0x26`（RED，三色屏专用）

---

## 6. 驱动 API

```c
void epd_init(const epd_config_t *cfg);      // SPI/GPIO + OTP 全刷初始化，NULL = 默认引脚
bool epd_wait_busy(uint32_t timeout_ms);
void epd_display(const uint8_t *fb);         // 整帧显示（OTP 波形，约 4–6 s）
void epd_display_area(const uint8_t *fb, int x, int y, int w, int h);
void epd_fill(uint8_t color);                // 0=黑，非 0=白
void epd_deep_sleep(void);                   // 深睡，画面保持

// 快速刷新（70 字节 partial LUT）
bool epd_partial_init(const uint8_t *lut);   // 硬件复位 + partial 初始化 + 载入 LUT
bool epd_partial_update(const uint8_t *area, int x, int y, int w, int h, uint32_t timeout_ms);
bool epd_partial_show_full(const uint8_t *fb, uint32_t timeout_ms);
bool epd_partial_reload_lut(const uint8_t *lut);  // 清洁周期后重载 LUT
bool epd_band_update(const uint8_t *frame, int y0, int y1, uint32_t timeout_ms); // 70 行带状局刷
bool epd_epd_dis_part(...);                  // 对照实验接口，当前未使用
```

绘图辅助（只操作内存帧缓冲）：

```c
epd_fb_clear / epd_fb_pixel / epd_fb_hline / epd_fb_vline / epd_fb_rect / epd_fb_fill_rect
epd_fb_text / epd_fb_text_width                    // 5x7 ASCII
epd_fb_text_scaled / epd_fb_text_width_scaled      // 1~8 倍放大
```

最小示例：

```c
static uint8_t fb[EPD_FRAME_BYTES];

void app_main(void) {
    epd_init(NULL);
    epd_partial_init(epd_lut_partial_70);
    epd_fb_clear(fb, 1);
    epd_fb_text_scaled(fb, 40, 120, "hello", 0, 4);
    epd_partial_show_full(fb, 5000);
    epd_deep_sleep();
}
```

---

## 7. 刷新方案要点

| 阶段 | 命令 | 说明 |
| --- | --- | --- |
| 初始化 | `0x22 = 0xB1` → `0x20` | 从 OTP 载入温度与波形 |
| 全刷 | `0x24` 写帧 → `0x22 = 0xC7` → `0x20` | 4–6 s，全屏闪 |
| 70 行带状局刷 | `0x01` MUX=69 → `0x0F` gate start=95 → `0x45` Y=95..164 → `0x44` X=0..399 → `0x24` 写 400×70 → `0x20` | 约 346 ms，带状外不动、横带不闪 |
| 全帧快速刷新 | `0x22 = 0x80`+`0x20` 开时钟 → `0x32` + 70B LUT → 设窗口 → `0x24` 写整帧 → `0x20` → 再写一遍基线 | 约 740 ms，无可见闪（备用） |

几个高频坑（完整表见 [`docs/交接文档.md`](docs/交接文档.md)、[`docs/技术方案.md`](docs/技术方案.md)）：

- **`0x22` 不是波形索引**：SSD1619A 与 SSD1683 语义不同，`0xF7` 会挂死 BUSY，全刷用 `0xC7`。
- **CS 必须贯穿整个“命令+参数/数据”事务**，中间抬起 CS 会丢参数。
- 每次写 RAM 前都要重新下发 `0x44/0x45/0x4E/0x4F`。
- `0xC7` 刷新结束会关时钟，写 RAM 前需 `0x22 = 0x80` + `0x20` 重新开时钟。

---

## 8. 许可证与第三方声明

- `main/`、`components/epd_ssd1619a/` 中的其余代码：MIT
- `epd_font_5x7.h` 字体数据：源自 Adafruit GFX（BSD-2-Clause）
- **`components/epd_ssd1619a/include/epd_lut_partial.h` 的 70 字节 partial LUT
  与对应的初始化/刷新序列来自 [HalfSweetStudio/EPaperDrive](https://github.com/HalfSweetStudio/EPaperDrive)，该库为 AGPL-3.0。**

因此“MIT 整体发布”与上述 LUT 存在冲突。对外开源/商用前必须三选一：
① 整个项目按 AGPL-3.0 发布；② 取得作者授权；③ 依据 datasheet 独立重写 partial LUT 与序列。
仅本地实验/论文使用时，请在文中注明来源与许可证。

---

## 9. 下一步

1. 处理上面的 AGPL 许可证问题（发布前必做）；
2. 演示改为 **按键翻词 + 按需刷新**，去掉连续 50 词；
3. 停留/空闲用 light sleep 或 tickless idle，CPU 240 → 160/80 MHz；
4. 清洁周期改自适应（每 20–30 次刷新，或残影明显时）；
5. 补功耗与残影实测（INA226 / PPK2），接入真实词库与复习调度（Leitner）；
6. 评估 SPI 4 → 10 MHz（缩短 15000 字节写入时间）。

详细交接与风险清单见 [`docs/交接文档.md`](docs/交接文档.md)。
