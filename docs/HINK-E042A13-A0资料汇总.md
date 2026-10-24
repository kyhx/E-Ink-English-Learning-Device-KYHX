# HINK-E042A13-A0（SSD1619A）资料汇总

| 项目 | 内容 |
| --- | --- |
| 查询/测试日期 | 2026-10-24 |
| 目标型号 | HINK-E042A13-A0 |
| 主控 | SSD1619A（社区拆解与反汇编确认） |
| 尺寸/分辨率 | 4.2" / 400×300 |
| 公开规格书 | **未找到 HINK-E042A13-A0 原厂 PDF** |
| 结构与驱动参考 | **GDEH042Z96**（Good Display / 大连佳显，SSD1619） |
| 实测颜色 | **单色**（RED 平面测试：第三块仍为白色） |
| 实测局刷 | **可用**（EPaperDrive 70 字节 partial LUT，120×40 区域约 0.67 秒，无全屏闪） |

---

## 1. 这个型号是什么

HINK-E042A13-A0 是 **汉朔（Hanshow）Stellar-XL 电子价签**上使用的 4.2" 定制墨水屏，FPC 上印有：

```text
HINK-E042A13-A0   Date: 2017-4-24   SYX832
```

社区拆解与点亮记录（数码之家）结论：

- 该屏与 **GDEH042Z96** 外形、FPC 印字基本一致；
- 反汇编价签固件确认主控指令集为 **SSD1619**；
- 网上找不到 HINK-E042A13-A0 的规格书；
- 移植 GDEH042Z96 的单色例程可以点亮；
- 同一作者后续测试明确：**SSD1619 主控使用 OTP 中存储的查找表，可显示单色**。

> 来源：
> - [数码之家：两款汉朔 4.2 寸墨水屏电子价签拆解与点亮](https://www.mydigit.cn/forum.php?mod=viewthread&tid=274369)
> - [数码之家：盒马 4.2 寸价签拆解，屏丝印 HINK-E042A13-A0](https://www.mydigit.cn/forum.php?mod=viewthread&tid=454355)
> - [数码之家：4.2 寸价签墨水屏 8 灰阶测试](https://www.mydigit.cn/thread-295094-1-1.html)

因此：**用 GDEH042Z96 的资料来初始化 HINK-E042A13-A0 是可行的，但两者颜色能力不同：GDEH042Z96 是三色，HINK 这块实测是单色。**

---

## 2. GDEH042Z96 关键规格（结构/驱动参考）

> 注意：GDEH042Z96 官方是 **黑白红三色**；HINK-E042A13-A0 实测为单色。下表只作为外形、FPC、主控和初始化参考，不代表 HINK 的颜色能力。

| 项目 | 参数 |
| --- | --- |
| 产品型号 | GDEH042Z96 |
| 分辨率 | 400×300 |
| 颜色 | 黑 / 白 / 红（三色） |
| 控制器 | SSD1619 |
| 接口 | SPI（4-wire） |
| FPC | 24 Pin，Pitch 0.5 mm |
| 外形尺寸 | 91 × 77 × 1.25 mm |
| 视域尺寸 | 84.8 × 63.6 mm |
| 点间距 | 0.212 × 0.212 mm |
| 工作温度 | 0 ℃ ~ 40 ℃ |
| 供电 | 3.3 V |
| 全刷时间 | 官方标称 16 s（本工程只写 BW 平面，实测约 4 s） |
| 刷新功耗 | 26.4 mW |
| 待机功耗 | 0.0165 mW |
| 局部刷新 | GDEH042Z96（三色）官方明确不支持 |

官方产品页原文：

> “注意：只有单色电子纸显示屏支持局刷，三色电子纸显示屏不支持局刷（只支持全刷）。”

来源：

- [GDEH042Z96 产品页（大连佳显）](https://www.e-paper-display.cn/products_detail/productId=532.html)
- [GDEH042Z96 规格书下载页](https://www.e-paper-display.cn/download_detail/downloadsId=1106.html)
- [SSD1619A 规格书下载页](https://www.e-paper-display.cn/download_detail/downloadsId=1112.html)

本地已保存：

- [`datasheets/GDEH042Z96_spec.pdf`](datasheets/GDEH042Z96_spec.pdf)
- [`datasheets/SSD1619A_spec.pdf`](datasheets/SSD1619A_spec.pdf)

---

## 3. 颜色测试：确认是单色

### 3.1 测试方法

不能只看黑白显示来判断颜色，因为原代码只写 `0x24`（BW 平面），并用 `0x21=0x40` 主动旁路了 RED 平面。为此新增三色测试接口：

```c
/* 0x21=0x00：BW 和 RED 两个平面都正常使用 */
epd_display_bw_red(bw_fb, red_fb);
```

测试图案为三条竖带：

```text
白（BW=1, RED=0） | 黑（BW=0, RED=0） | 红（BW=1, RED=1）
```

### 3.2 测试结果

屏幕第三块（最右边）**仍然是白色**，没有出现红色。

结论：**HINK-E042A13-A0 是单色屏**（或 RED 平面未接入）。结合社区“SSD1619 OTP 查找表、可显示单色”的记录，可以确认它是汉朔定制的单色 4.2" 屏。

---

## 4. 单色是否就能局刷？——能，但需要自定义 partial LUT

### 4.1 控制器支持，但面板 OTP 不一定有 partial LUT

- SSD1619A datasheet 第 5 页确实写了 “Support display partial update”。
- 但 datasheet 6.8 节说明：OTP 里存的是 **25 组按温度范围的 WS（LUT/电压/帧率）**，不是 partial 用的短波形 LUT。
- 要真局刷，面板 OTP 必须含 partial LUT，或由 MCU 通过 `0x32` 写入 70 字节 partial LUT。

### 4.2 本工程实测

对 120×40 区域测试了：

```text
0xC7 / 0xCF / 0x47 / 0x4F / 0x44 / 0x4C / 0x04 / 0x0C / 0xFF
```

结果全部为 **3.9–4.1 秒**，与整帧基线 4.1 秒同一量级；没有任何一种接近局刷常见的 0.3–1.5 秒。

另外还按 **SSD1683 的 partial 方案** 试了一次：

```text
BW = 新帧，RED = 上一帧基线，0x21=0x00，0x22=0xFF
```

结果 **4159 ms**，仍是全刷。Linux DRM 的 SSD16xx 驱动确实把 `0xFF` 作为 partial、`0xC7` 作为 fast、`0xF7` 作为 full，并且用 RED RAM 存上一帧作为基线（[LWN 补丁](https://lwn.net/ml/all/20260927182329.4193961-5-devarsht@ti.com/)），但这套机制不能直接套到这块 SSD1619A 上。

还强行套用了 [miklav/SSD1683-fast-partial-lut](https://github.com/miklav/SSD1683-fast-partial-lut) 的 227 字节自定义 LUT + `0x22=0xDC`：

- BUSY 从 4.1 秒降到 **1936 ms**；
- 但画面是 **整个屏幕闪一下再更新**，不是只刷 120×40 区域；
- 结论：它只是把“全刷”缩短了，不是 partial；且该仓库是 GPL-3.0，相关代码未并入本工程。

另外还移植了 [OSHWHub GaAs 的 SSD1619 项目](https://oshwhub.com/GaAs/mo-shui-ping-pai-zi)（MIT）的 70 字节 LUT：

- **Full LUT：2085 ms，清晰、黑白正确** → 可当“快速全刷”；
- **GS LUT（单相，RP0=8）：450 ms，但很淡/残影严重** → 只是灰度单相波形，不能当完整刷新；
- 两者仍然整屏扫描/整屏闪烁，不是 partial。

详见 [`局刷测试记录.md`](局刷测试记录.md)。

### 4.3 只靠 OTP 的结论

**单色是局刷的必要条件，但不是充分条件。**
只用 OTP/标准 `0x22` 时，这块 HINK 屏换 `0x22`、换 `0x21`、改窗口都不会变成真局刷。

### 4.4 EPaperDrive partial LUT：成功

参考项目：[某价签改装极简墨水屏开发板 手改灰度](https://oshwhub.com/shilihualang/han-jia-qian-gai-zhuang-ji-jian-kai-fa-ban) / [HalfSweetStudio/EPaperDrive](https://github.com/HalfSweetStudio/EPaperDrive)。屏幕丝印正是 HINK-E042A13-A0。

它提供了 OTP 里没有的 **70 字节 partial LUT**（`LUTDefault_part_opm42`）和 `EPD_init_Part()` / `EPD_Dis_Part()` 流程。本工程按该流程实现后实测：

| 步骤 | 时间 | 结果 |
| --- | --- | --- |
| 整屏底图 | **700 ms** | 底图正确 |
| 70 行带状更新 | **346 ms** | **带状外完全不动** |

**结论：这块屏可以实现真正的局部刷新，但必须使用自定义 partial LUT + 对应初始化序列，不能只靠 OTP。**

连续 30 次局刷实测：每次 **680 ms**，30 次后轻微残影、底图基本不变；只重画一次底图不够，“全白 → 全黑 → 底图”清洁周期（3 × 709 ms）可完全恢复。建议每 **20–30 次**清洁一次。
EPaperDrive 是 **AGPL-3.0**；上述 LUT/序列仅用于本地验证，发布/商用需遵守 AGPL 或取得授权，或从 datasheet 独立重写。

## 5. 已可用的局刷方案

| 方案 | 需要 | 成本 | 建议 |
| --- | --- | --- | --- |
| A. EPaperDrive partial LUT | 遵守 AGPL-3.0 或取得授权 | 低-中 | **已实测可用**，优先做连续刷新/残影测试 |
| B. 从 datasheet 独立重写 partial LUT | 波形参数 + 测试 | 高 | 不想受 AGPL 约束时做 |
| C. 换 2.9" 支持 partial 的单色屏 | 新屏 + 驱动适配 | 中 | 可作对照 |
| D. 继续只用全刷 | 无 | 低 | 不满足“局刷”指标 |

## 6. 初始化/驱动资料

- **GxEPD2**：`GxEPD2_420c_Z96` 类，基于 GDEH042Z96 示例。
  - PR: <https://github.com/ZinggJM/GxEPD2/pull/47>
  - 关键序列：`0x2B 0x04,0x63`、`0x0C 0x8B,0x9C,0x96,0x0F`、`0x22=0xB1 + 0x20` 加载 OTP 波形、刷新 `0x22=0xC7 + 0x20`。
- 本工程已按该序列实现并点亮：
  - [`components/epd_ssd1619a/epd_ssd1619a.c`](../components/epd_ssd1619a/epd_ssd1619a.c)
  - [`README.md`](../README.md)

| 项目 | 值 |
| --- | --- |
| SPI | Mode 0，4 线，4 MHz |
| CS/DC | 软件 CS；CS 必须贯穿命令+参数整个事务 |
| RAM 窗口 | X 用字节坐标（0x44），Y 用像素坐标（0x45） |
| 单色写 RAM | `0x24` |
| 全刷 | `0x22=0xC7` + `0x20` |
| 初始化 OTP | `0x22=0xB1` + `0x20` |
| 写 RAM 前 | `0x22=0x80` + `0x20` 开时钟 |

---

## 7. 结论

1. **HINK-E042A13-A0 是汉朔定制的 4.2" 400×300 单色 SSD1619A 屏**，没有公开规格书。
2. **GDEH042Z96 只是结构/驱动参考**；GDEH042Z96 是三色，HINK 实测是单色。
3. **只用 OTP/标准 `0x22` 不能局刷**：8/9 种模式实测全部约 4 秒全刷，OTP 没有 partial LUT。
4. **用 EPaperDrive 的 70 字节 partial LUT + MUX/gate start 可以做 70 行带状真局刷**：346 ms，带状外完全不动；X 方向仍全宽，但横带不闪；不是任意 2D 窗口。
5. 推荐：优先采用该 partial LUT 做连续刷新/残影/功耗测试；若要发布，遵守 AGPL-3.0 或独立重写 LUT。
6. 2.9" partial 屏仍可作为对照/备选，但不再是唯一路线。

---

## 8. 参考链接

- [GDEH042Z96 产品页](https://www.e-paper-display.cn/products_detail/productId=532.html)
- [GDEH042Z96 规格书](https://www.e-paper-display.cn/download_detail/downloadsId=1106.html)
- [SSD1619A 规格书](https://www.e-paper-display.cn/download_detail/downloadsId=1112.html)
- [GxEPD2 GDEH042Z96 PR](https://github.com/ZinggJM/GxEPD2/pull/47)
- [数码之家：汉朔 Stellar-XL 拆解与点亮](https://www.mydigit.cn/forum.php?mod=viewthread&tid=274369)
- [数码之家：盒马价签 HINK-E042A13-A0](https://www.mydigit.cn/forum.php?mod=viewthread&tid=454355)
- [数码之家：4.2 寸价签 8 灰阶测试](https://www.mydigit.cn/thread-295094-1-1.html)
- [数码之家：4.2 寸价签 16 灰阶测试](https://www.mydigit.cn/thread-295373-1-1.html)
- [OSHWHub：价签改装极简墨水屏开发板（HINK 屏，局刷灰度）](https://oshwhub.com/shilihualang/han-jia-qian-gai-zhuang-ji-jian-kai-fa-ban)
- [GitHub：HalfSweetStudio/EPaperDrive（AGPL-3.0）](https://github.com/HalfSweetStudio/EPaperDrive)
- 本工程：[`局刷测试记录.md`](局刷测试记录.md)、[`README.md`](../README.md)
