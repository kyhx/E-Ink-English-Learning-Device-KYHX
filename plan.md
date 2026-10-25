# 基于边缘 AI 的墨水屏英语学习机 · 实施计划（plan.md）

| 项    | 内容                                                                          |
| ---- | --------------------------------------------------------------------------- |
| 文档版本 | v0.1                                                                        |
| 日期   | 2026-10-25                                                                  |
| 代码基线 | `2d1bc85`（SSD1619A 70 行带状真局刷，346 ms）                                        |
| 硬件基线 | YD-ESP32-S3 / ESP32-S3-N16R8（双 Type-C）+ 4.2" 单色 SSD1619A（400×300）+ 驱动板 V1.5 |
| 开发环境 | ESP-IDF v6.1（`F:\esp\v6.1\esp-idf`）、COM4、Python 3.12 训练环境                   |
| 计划周期 | 24 周（2026-10-26 → 2027-04-11）+ 2 周缓冲                                        |
| 适用对象 | 本人、导师、下一位接手工程师                                                              |

> **一句话现状**：显示子系统已经打通并实测——70 行带状局刷 346 ms、整屏底图约 700 ms、清洁周期 2.1 s、50 词演示稳定；**音频、词库、数据集、模型、规则法、记录、无线同步、功耗实测和论文数据都还没有**。
>
> **一句话策略**：先用 M0–M2 把“能学、能录音、能显示”的闭环跑通，再用 M3–M8 把“能评测、能对照、能同步、能写论文”补齐；每个里程碑有硬门禁，不通过不进入下一阶段。

---

## 0. 这份计划怎么用

1. **按里程碑推进，不按“功能点”发散**：M0–M8 每阶段都有交付物和门禁，门禁不过就停下来修，不把问题带到下一阶段。
2. **先闭环，后指标**：先做到“翻词 → 播放 → 录音 → 出结果 → 记录 → 同步”，再优化准确率、耗时和功耗。
3. **论文数据与开发同步产出**：每个实验（E0–E8）都在对应里程碑里落表，避免最后两周补数据。
4. **所有结论必须可复现**：脚本、随机种子、数据划分、固件 commit、测量条件都写进 `results/` 和论文附录。
5. **不确定就降级**：每个关键决策都写了备选方案（见第 6 节），例如 TFLM 不可用就回退手写 int8 推理，不阻塞主线。

---

## 1. 现状盘点

### 1.1 已经验证的能力（有实测证据）

| 能力                | 证据                              | 实测值                         |
| ----------------- | ------------------------------- | --------------------------- |
| 面板初始化、全白、全黑、文字    | `docs/交接文档.md`、`docs/局刷测试记录.md` | 正常                          |
| OTP 全刷            | `docs/局刷测试记录.md`                | 4–6 s，全屏闪                   |
| 70 行带状真局刷         | `epd_band_update()`，y=95..164   | **约 346 ms/次**              |
| 整屏底图（partial LUT） | `epd_partial_show_full()`       | 约 700 ms                    |
| 清洁周期（白→黑→底图）      | `clean_cycle()`                 | 3 × 709 ms ≈ 2.1 s          |
| 50 词连续演示          | `main/main.c`                   | 346 ms + 1 s/词，第 25/50 词清洁  |
| 编译                | ESP-IDF v6.1 + xtensa-esp-elf   | 0 error，app 约 230 KB        |
| 硬件链路              | `docs/wiring.png`               | EPD 7/8/9/10/11/12，COM4 可烧录 |

### 1.2 现有代码资产（可直接复用，不要重写）

| 资产          | 位置                                                             | 现在能做什么                                       | 计划中的用法                     |
| ----------- | -------------------------------------------------------------- | -------------------------------------------- | -------------------------- |
| EPD 驱动核心    | `components/epd_ssd1619a/epd_ssd1619a.c`                       | SPI/GPIO、OTP 初始化、全刷、partial 初始化、LUT 重载、深睡    | 原样复用；只做小重构和 API 扩展         |
| 带状局刷        | `epd_band_update(frame, y0, y1, timeout)`                      | 只扫 `[y0..y1]`，写整宽 400 px，transition-only LUT | 单词卡/结果卡的唯一高频刷新路径           |
| 整屏刷新        | `epd_partial_show_full(fb, timeout)`                           | partial LUT 整屏约 700 ms                       | 页面切换、底图重画                  |
| 清洁周期        | `epd_partial_reload_lut()` + 白/黑/底图                            | 完全恢复残影                                       | 每 20–30 次局刷一次，不自适应前先用固定阈值  |
| 1bpp 绘图     | `epd_fb_clear/pixel/hline/vline/rect/fill_rect`                | 内存帧缓冲绘制，1=白、0=黑                              | UI 页面合成                    |
| 5×7 文本      | `epd_fb_text/text_width`                                       | ASCII 32–126，1 倍字                            | 调试信息、状态标签                  |
| 放大文本        | `epd_fb_text_scaled/text_width_scaled`                         | 1–8 倍像素放大                                    | 现有演示；正式 UI 改用点阵字库          |
| 单词卡布局       | `main/word_view.c/.h`                                          | 标题/序号/单词/页脚，纯内存绘制                            | 保留布局思路，数据源换成词库和 view model |
| 演示流程        | `main/main.c` 的 `show_word()`/`clean_cycle()`                  | 50 词循环 + 清洁                                  | 收进 `APP_MODE_DEMO`，作为回归测试  |
| partial LUT | `components/epd_ssd1619a/include/epd_lut_partial.h`            | 70 字节 transition-only LUT                    | **AGPL 来源，发布前必须处理**（见 D9）  |
| 离线预览        | `tools/preview_demo.py`                                        | 用 PIL 复刻帧缓冲，输出 PNG                           | 扩展成页面 golden preview       |
| PNG→RAW     | `tools/png_to_raw.py`                                          | PNG 转 15000 字节 1bpp                          | 生成测试底图/对照图                 |
| 字体生成        | `tools/gen_font.py` + `font5x7_ascii.py`                       | 5×7 字模 + 预览                                  | 复用“生成 + 预览”模式，新增 TTF 子集生成器 |
| 接线图         | `tools/gen_wiring.py`                                          | 生成 `docs/wiring.png`                         | 新增模块后更新接线图                 |
| 文档          | `docs/技术方案.md`、`docs/交接文档.md`、`docs/局刷测试记录.md`、`docs/真局部研究.md` | 方案、踩坑、实验数据                                   | 论文第 4 章的主要素材               |

### 1.3 硬约束（违反会直接返工）

| 约束                   | 具体内容                                                           | 影响                                           |
| -------------------- | -------------------------------------------------------------- | -------------------------------------------- |
| 帧格式                  | 400×300，1 bpp，每行 50 B，整帧 15000 B，MSB 在左，**1=白、0=黑**            | 所有 UI/字库/模型输入必须遵守                            |
| 带状局刷                 | `epd_band_update()` 只做 **Y 方向**局部扫描，X 仍全宽；带状内必须写整条 400 px      | 布局必须把高频变化内容压进一条带                             |
| 不是任意 2D 窗口           | SSD1619A 无 source X 窗口，无法只扫 128 列                              | 论文只能写“带状真局刷”，不能写“任意矩形局刷”                     |
| 全刷/清洁代价              | 整屏约 700 ms；清洁 2.1 s                                            | 页面切换才全刷，不能每词全刷                               |
| 深睡语义                 | `epd_deep_sleep()` 后画面保持，但**再写屏必须硬件复位 + `epd_partial_init()`** | **不能**每次刷新后都让面板深睡（见 D7）                      |
| `epd_display_area()` | 仍是 OTP 全刷路径，不是 partial                                         | 正式 UI 不要用它做局刷                                |
| 字体                   | 只有 5×7 ASCII；无 IPA、无中文                                         | 必须生成 32/16 px 点阵子集 + UTF-8 解码                |
| RAM                  | `g_part_tmp` 静态占 15000 B；帧缓冲 2×15000 B                         | 内部 RAM 要留给 TFLM arena 和 Wi-Fi 栈              |
| 许可证                  | partial LUT/序列来自 AGPL-3.0 的 EPaperDrive                        | README 已注明 MIT 与 AGPL 冲突，但尚未给出发布方案；发布/商用前三选一 |
| 分区                   | 目前只有 nvs/phy/factory，**没有 storage**                            | 词库、音频、记录无处可放                                 |
| 板级睡眠电流               | YD-ESP32-S3 的 **PWR LED 常亮且不可控**、CH343P/LDO 也可能吃电流；很可能 mA 级    | <100 µA 目标要实测，可能只能报“板级”并说明；必要时拆 PWR LED/自制定板 |

### 1.4 课题要求 vs 现状 vs 计划

| 课题要求                    | 现状                    | 差距                       | 里程碑   |
| ----------------------- | --------------------- | ------------------------ | ----- |
| 自建词库 ≥50 词，含音标/释义/例句/音频 | 50 个硬编码英文单词，无音标/释义/音频 | 词库、资源打包、音频               | M1/M2 |
| 分级复习安排每日学习量             | 无                     | Leitner 调度 + NVS 持久化     | M1    |
| 按键完成切换/播放/跟读            | 无按键代码                 | 5 键驱动 + 事件               | M1/M2 |
| 采集 ≥200 条真实跟读语音并标注      | 无                     | 采集固件 + PC 工具 + 标注协议      | M3/M4 |
| 提取 Mel 特征，训练轻量分类器       | 无                     | log-Mel + CNN + 量化       | M5    |
| 量化后在单片机本地推理             | 无                     | TFLM int8 + arena + 耗时   | M6    |
| 规则法（基频）对照               | 无                     | YIN + DTW + 评分           | M3    |
| 两法评分一致性/准确率/耗时对比        | 无                     | E5 对照实验                  | M8    |
| 墨水屏单词卡 + 局部刷新           | **已完成 346 ms 带状局刷**   | 页面管理器、字号、双 band（可选）      | M0/M1 |
| 学习记录无线同步                | 无                     | SoftAP + HTTP + CSV/JSON | M7    |
| 实测待机/录音/推理电流            | 无                     | INA219/PPK2 + 测量协议       | M7    |
| 推理耗时/刷新耗时               | 只有刷新耗时                | 推理分段计时                   | M6/M7 |
| 论文                      | 技术方案草案                | 实验数据、图表、成稿               | M8    |

---

## 2. 目标与验收

### 2.1 功能验收（必须全部满足）

| 编号  | 功能                                    | 验收方式                       |
| --- | ------------------------------------- | -------------------------- |
| F1  | 100 词自建词库（最低 50），含单词、IPA、中文释义、例句、音频索引 | 上电可浏览全部词条；资源校验脚本通过         |
| F2  | Leitner 6 盒分级复习，每日“到期复习 + 新词”队列       | 模拟 100 天调度，等级/到期日正确；重启不丢   |
| F3  | 单词卡：单词、音标、进度、等级                       | 400×300 下清晰可读；带状刷新 ≤400 ms |
| F4  | 按键播放单词发音                              | 5 键可用；播放无破音；功放空闲关断         |
| F5  | 按键录 2 s 跟读，16 kHz/16 bit 单声道          | WAV 可下载、可回放；VAD 能去掉首尾静音    |
| F6  | 模型法输出“通过/不通过”+ 置信度/分数 + 耗时            | 结果页显示；与 PC 推理一致            |
| F7  | 规则法 F0+DTW 输出分数 + 二分类 + 耗时            | 同一录音可切换两法；结果可记录            |
| F8  | 结果页显示两法结果、分数、耗时、免责声明                  | 截图/照片归档                    |
| F9  | 学习记录（词、时间、结果、分数、方法、耗时）                | 断电不丢；CSV/JSON 可导出          |
| F10 | SoftAP + 浏览器同步记录和录音                   | 手机/PC 各 3 次下载成功            |
| F11 | 深睡/轻睡/录音/推理/同步分级唤醒                    | 按键唤醒；状态电流可测                |
| F12 | 统计页：今日进度、正确率、连续天数                     | 数据与记录一致                    |

### 2.2 性能与实验验收（目标值，最终以实测为准）

| 编号  | 指标         | 目标                      | 备注                             |
| --- | ---------- | ----------------------- | ------------------------------ |
| P1  | 带状局刷       | ≤400 ms（当前 346 ms）      | 70 行；更窄带另测                     |
| P2  | 整屏底图       | ≤800 ms（当前约 700 ms）     | partial LUT                    |
| P3  | 清洁周期       | ≤2.5 s                  | 3 × 709 ms                     |
| P4  | 模型准确率      | ≥75%（说话人独立 LOSO）        | 样本少，必须报 95% CI                 |
| P5  | 模型大小       | <100 KB（int8）           | 目标 20–40 KB                    |
| P6  | 推理耗时       | <200 ms（mel + CNN，含预处理） | 目标 <100 ms                     |
| P7  | 规则法耗时      | <100 ms                 | 8 kHz 降采样 YIN                  |
| P8  | TFLM arena | <64 KB                  | 实测 `tensor_arena_used_bytes()` |
| P9  | 深睡电流       | <100 µA（整机）             | 板级可能不达标，如实报告                   |
| P10 | 同步成功率      | ≥95%（50 次）              | 同一热点/局域网                       |
| P11 | 连续运行       | ≥30 min 无崩溃/无泄漏         | soak test                      |
| P12 | 残影         | 500 次带状刷新后清洁可恢复         | 每 50 次拍照记录                     |

### 2.3 论文/答辩交付物


1. 可复现固件（tag + 构建说明 + 分区/资源生成脚本）。
2. 标注数据集说明（≥200，目标 600–800 条 + 80 条词泛化；说话人独立划分；隐私处理）。
3. log-Mel + int8 CNN 训练/量化/导出脚本；`model.tflite` + C 数组。
4. C 端 mel/CNN/规则法实现 + 与 PC 的 parity 报告。
5. 规则法 F0+DTW 实现与对照数据（准确率、kappa、相关性、耗时）。
6. 刷新/功耗/同步实测数据与图表。
7. 论文成稿、答辩 PPT、演示视频、复现实验脚本。

### 2.4 非目标（明确不做，避免发散）

- 不做手机原生 App（浏览器足够）。
- 不做离线 TTS（用预录/生成音频）。
- 不做任意 2D 窗口局刷（硬件不支持）。
- 不做云端训练/云端推理。
- 不做触摸（5 键足够；触摸作为可选扩展）。
- 不做 OTA（答辩前不引入额外风险）。
- 不把发音结果用于语言能力评定（界面和论文都要写明）。

---

## 3. 总体架构

### 3.1 硬件框图

```text
                  ┌────────────────────────────────────┐
                  │        ESP32-S3-N16R8              │
                  │  16 MB Flash / 8 MB Octal PSRAM    │
                  │  ESP-IDF v6.1 / FreeRTOS           │
                  └───┬──────────┬──────────┬──────────┘
                      │ SPI2     │ I2S0 RX  │ I2S1 TX
                      ▼          ▼          ▼
              ┌────────────┐ ┌─────────┐ ┌──────────────┐
              │ 4.2" EPD   │ │ INMP441 │ │ MAX98357A    │
              │ 400×300    │ │ 数字麦  │ │ + 4Ω 喇叭    │
              └────────────┘ └─────────┘ └──────────────┘
                      ▲
                      │ 5 键 + 电池 ADC + INA219
              ┌───────┴───────────────────────────────┐
              │ 5 键 / TP4056 / 3.7V 锂电 / 3.3V 升降压 │
              └───────────────────────────────────────┘
```

### 3.2 最终引脚分配（已对照 YD-ESP32-S3 / N16R8 用户手册）

> 板卡：YD-ESP32-S3（ESP32-S3-WROOM-1，16 MB Flash / 8 MB PSRAM），双 Type-C（COM=CH343P，USB=原生 GPIO19/20），RGB LED=GPIO48。完整 P1/P2 排针表见 [`docs/board_pinmap.md`](docs/board_pinmap.md)。

| 功能         | GPIO | 方向  | 说明                                                        |
| ---------- | ---- | --- | --------------------------------------------------------- |
| EPD BUSY   | 7    | 输入  | 高=忙                                                       |
| EPD RST    | 8    | 输出  | 低有效                                                       |
| EPD DC     | 9    | 输出  | 低=命令                                                      |
| EPD CS     | 10   | 输出  | 软件片选                                                      |
| EPD MOSI   | 11   | 输出  | SPI2                                                      |
| EPD SCK    | 12   | 输出  | SPI2，当前 4 MHz                                             |
| KEY_PREV   | 4    | 输入  | 上拉，低有效，RTC                                                |
| KEY_NEXT   | 5    | 输入  | 上拉，低有效，RTC                                                |
| KEY_PLAY   | 6    | 输入  | 上拉，低有效，RTC                                                |
| KEY_RECORD | 13   | 输入  | P1-19，上拉，低有效，RTC；ADC2 仅数字                                 |
| KEY_SYNC   | 1    | 输入  | 上拉，低有效，RTC；**5 键都能 EXT1 唤醒**                              |
| BAT_ADC    | 2    | 模拟  | ADC1_CH1，1M/1M 分压（或 MOS 门控）+ 100 nF；100k/100k 常接会漏约 21 µA |
| MIC BCLK   | 14   | 输出  | P1-20，I2S0；ADC2 仅数字                                       |
| MIC WS     | 15   | 输出  | I2S0                                                      |
| MIC SD     | 16   | 输入  | I2S0；INMP441 L/R 接地选左声道；**供电用 MOS/负载开关，录音时才开**            |
| AMP BCLK   | 17   | 输出  | I2S1                                                      |
| AMP LRC    | 18   | 输出  | I2S1                                                      |
| AMP DIN    | 21   | 输出  | I2S1                                                      |
| AMP SD     | 47   | 输出  | P2-17，使能；空闲拉低；SPICLK_P 备选                                 |
| INA219 SDA | 39   | I/O | P2-9，I²C；与外置 JTAG 冲突，调试用 CH343P 串口；**仅测量用，产品态跳线/MOS 断电**  |
| INA219 SCL | 40   | 输出  | P2-8，I²C                                                  |

> 说明：GPIO 1/4/5/6/13 都在 S3 的 RTC GPIO 0–21 范围内，因此 5 个按键都能作为深睡唤醒源。
>
> 避开：0（BOOT）、3/45/46（strapping）、19/20（原生 USB）、35/36/37（Flash/PSRAM，手册明确保留）、43/44（UART0/CH343P）、48（RGB LED，需桥接）。GPIO39–42 是外置 JTAG；本项目不用外置 JTAG，39/40 可作 I2C。
>
> 已复核：GPIO13/14 在 **P1 的 19/20 脚**引出；GPIO47/48 在 P2 的 17/16 脚；GPIO35/36/37 在 P2 排针上但被内部 Flash/PSRAM 占用，不可用。
>
> **低功耗静态负载**：INA219 约 1 mA、INMP441 约 1.6 mA、100k/100k 分压约 21 µA，任何一个常接都会让深睡 >100 µA。测量/产品态要分别用跳线、MOS 负载开关、1M/1M 分压处理。

### 3.3 软件分层与任务

| 层      | 模块                                              | 职责                         |
| ------ | ----------------------------------------------- | -------------------------- |
| 板级     | `board_pins.h`、`buttons`、`battery`、`ina219`     | 引脚、按键消抖、电池、电流              |
| 驱动     | `epd_ssd1619a`、`audio_in/out`、`storage`、`wifi`  | 屏、I2S、LittleFS、SoftAP/HTTP |
| DSP/AI | `dsp_mel`、`infer_tflm`、`rule_f0`                | log-Mel、int8 推理、F0+DTW     |
| 服务     | `wordbank`、`scheduler`、`records`、`sync`、`power` | 词库、Leitner、记录、同步、功耗        |
| UI     | `ui_page`、`ui_font`、`ui_widgets`                | 页面状态机、字库、dirty band        |
| 应用     | `app_state`、`app_main`                          | 事件循环、按键映射、错误页              |

**任务模型（先简单，后加并发）**：

- `app_task`：唯一主循环，处理按键事件、页面切换、录音/推理/播放同步调用。
- `sync_task`：仅在同步时创建/唤醒，跑 `esp_http_server`。
- 按键 ISR → 队列；显示刷新在 `app_task` 内阻塞等待 BUSY（`vTaskDelay` 会让出 CPU）。
- 推理 <200 ms，阻塞 UI 可接受；不做复杂流水线。

### 3.4 应用状态机

```text
BOOT
  └─ INIT（屏/存储/词库/按键/电源）
       └─ HOME ──NEXT/PREV──> CARD ──PLAY──> PLAYBACK ──> CARD
                         │        └─REC──> RECORDING ──> INFERENCE ──> RESULT
                         │                                              │
                         │                         NEXT=记住 / PREV=没记住
                         │                                              ▼
                         └────────────────────────────────────────── CARD/NEXT
HOME ──SYNC──> WIFI_SYNC ──超时/完成──> HOME
HOME ──长按 NEXT──> STATS ──PREV──> HOME
HOME ──长按 SYNC──> SETTINGS ──PLAY 保存──> HOME
任意状态 ──30 s 无操作──> LIGHT_SLEEP
任意状态 ──5 min 无操作──> DEEP_SLEEP（面板深睡 + 系统深睡）
任意状态 ──错误──> ERROR（显示错误码 + 重启提示）
```

### 3.5 数据流（采集 → 评测 → 呈现 → 同步）

```text
词库/音频（LittleFS）
   │
   ├─ 播放：ADPCM/WAV → I2S1 → MAX98357A
   │
   └─ 跟读：INMP441 → I2S0 → PSRAM(2.2 s) → 去直流/VAD
                                   │
                    ┌──────────────┴──────────────┐
                    ▼                             ▼
             log-Mel 40×64                 F0 轮廓（8 kHz）
                    │                             │
             int8 CNN (TFLM)                DTW vs 参考轮廓
                    │                             │
                    └──────────┬──────────────────┘
                               ▼
                     结果页（分数/通过/耗时）
                               │
                               ▼
                 记录环（LittleFS）+ Leitner 更新
                               │
                               ▼
                  SoftAP + HTTP → 手机/PC CSV/JSON/WAV
```

### 3.6 存储与资源预算

**分区（16 MB Flash）**：

| Name     | Type | SubType  | Offset   | Size   | 用途                    |
| -------- | ---- | -------- | -------- | ------ | --------------------- |
| nvs      | data | nvs      | 0x9000   | 0x6000 | 设置、Leitner 状态、启动计数    |
| phy_init | data | phy      | 0xF000   | 0x1000 | RF 校准                 |
| factory  | app  | factory  | 0x10000  | 3 M    | 固件                    |
| storage  | data | littlefs | 0x310000 | 10 M   | 词库、音频、参考 F0、记录、采集临时文件 |

> 当前 `partitions.csv` 只有前 3 项；M0 增加 storage。若 `esp_littlefs` 拉取失败，回退 IDF 自带 FATFS + wear_levelling，`storage` 层 API 不变。
>
> 若加入 Wi-Fi/TFLM 后 app 超过 2.5 MB，把 factory 扩到 4 M（0x10000–0x410000），storage 改为 9 M（0x410000 起），总空间仍够；分区表改动后必须 `idf.py erase-flash` 并重新生成 LittleFS 镜像。

**空间预算（storage 10 MB）**：

| 内容                                            | 估算                |
| --------------------------------------------- | ----------------- |
| `words.bin`（100 词，定长记录 + 字符串池）                | 30–50 KB          |
| `audio.pak`（100 词 × 1.5 s，IMA ADPCM 4 bit）    | 约 1.2 MB          |
| `ref_f0.bin`（100 词 × 64 点 int16 +  voiced 标志） | 约 15 KB           |
| `records.bin`（1024 条 × 24 B 环形）               | 约 24 KB           |
| 采集临时 WAV（2 s/条，PCM16）                         | 64 KB/条，可放约 120 条 |
| 余量                                            | 约 8 MB            |

**RAM 预算（内部 SRAM 约 512 KB）**：

| 内容                             | 估算       |
| ------------------------------ | -------- |
| 2 × 15000 B 帧缓冲                | 30 KB    |
| band 临时缓冲 3500 B（替代现在 15000 B） | 3.5 KB   |
| TFLM arena                     | 32–64 KB |
| 音频 DMA/控制                      | 8 KB     |
| Wi-Fi/HTTP（仅同步时）               | 50–80 KB |
| 任务栈                            | 20 KB    |
| 余量                             | 充足       |

**PSRAM（8 MB）**：录音 2.2 s（70 KB）、数据集临时缓冲、模型输入/中间可选放大；帧缓冲和 arena 优先放内部 RAM。

### 3.7 刷新策略（沿用已验证方案）

| 场景                                  | 方式                              | 预算                    |
| ----------------------------------- | ------------------------------- | --------------------- |
| 开机/恢复                               | partial 初始化 + 底图（必要时清洁）         | 0.7–2.8 s             |
| 页面切换（HOME/CARD/STATS/SETTINGS/SYNC） | 整屏 `epd_partial_show_full()`    | 约 700 ms              |
| 单词切换、序号、进度、结果数字                     | 70 行带状 `epd_band_update()`      | 约 346 ms              |
| 录音电平、播放动画                           | 同一 band 内更新，节流到 ≤2 Hz           | 346 ms/次，实际上限约 2.9 Hz |
| 残影清洁                                | 白→黑→底图 + 重载 LUT                 | 约 2.1 s，每 20–30 次     |
| 深睡前                                 | 确保最后一次刷新完成，再 `epd_deep_sleep()` | —                     |

**UI 布局约束**：当前 band 为 y=95..164。正式卡片建议：

```text
y= 96..103  序号/进度（8 px 字体）
y=106..137  单词（32 px 字体）
y=141..156  音标（16 px 字体）
y=160..163  进度条（4 px）
```

即 95–164 的 70 行全部覆盖。若 E0 证明任意 y0 都稳定，可再拆一条 40 行的“结果 band”（y=170..209），把分数/耗时放在第二条带，避免和单词挤在一起；否则结果页复用 95–164 或整屏 700 ms。

### 3.8 功耗状态

| 状态          | CPU        | 外设                | 唤醒源             | 目标（实测为准）          |
| ----------- | ---------- | ----------------- | --------------- | ----------------- |
| DEEP_SLEEP  | 关          | 面板深睡、Wi-Fi/BT/音频关 | 5 键 EXT1、RTC 定时 | <100 µA（整机）       |
| LIGHT_SLEEP | 80 MHz     | 面板保持、音频/Wi-Fi 关   | 按键              | 0.5–2 mA          |
| IDLE        | 80–160 MHz | 面板保持              | 按键              | 20–40 mA          |
| REFRESH     | 240 MHz 峰值 | SPI + 面板升压        | 自动              | 20–40 mA × 0.35 s |
| RECORDING   | 240 MHz    | I2S0 + PSRAM      | 录音键             | 40–80 mA          |
| INFERENCE   | 240 MHz    | CPU + PSRAM       | 自动              | 40–80 mA × <0.2 s |
| PLAYBACK    | 160 MHz    | I2S1 + 功放         | 播放键             | 50–150 mA         |
| WIFI_SYNC   | 240 MHz    | Wi-Fi + HTTP      | 同步键             | 100–200 mA        |


> **重要修正**：`docs/技术方案.md` 5.5 节写“每次刷新完成后调用 `epd_deep_sleep()`”，这在连续交互场景下不可行——面板深睡后再写屏必须硬件复位并重新 `epd_partial_init()`。本计划改为**会话级深睡**：一次学习会话内面板保持供电/空闲，会话结束或 5 min 无操作才面板深睡 + 系统深睡；唤醒后重新初始化面板，但不做开机清洁（除非残影明显）。

---

## 4. 目录结构与模块接口

### 4.1 建议目录（新增文件，尽量不改驱动）

```text
main/
├── app_main.c                 # 入口：初始化 + 主循环
├── app_state.c/.h             # 状态机、按键映射、页面调度
├── board/
│   ├── board_pins.h           # 最终引脚表（唯一来源）
│   ├── buttons.c/.h           # ISR + 队列 + 20 ms 消抖 + 长按
│   ├── battery.c/.h           # ADC 电池电压
│   └── ina219.c/.h            # 电流/电压/功率
├── ui/
│   ├── ui.c/.h                # 页面管理、dirty band、flush
│   ├── ui_font.c/.h           # UTF-8 解码 + 点阵字库查找
│   ├── ui_widgets.c/.h        # 文本、进度条、图标、边框
│   └── pages/                 # home/card/result/stats/settings/sync/error
├── services/
│   ├── wordbank.c/.h
│   ├── scheduler.c/.h         # Leitner
│   ├── records.c/.h           # LittleFS 环形记录
│   ├── storage.c/.h           # LittleFS/FATFS 抽象
│   ├── audio_in.c/.h
│   ├── audio_out.c/.h
│   ├── sync_wifi.c/.h
│   └── power.c/.h
├── dsp/
│   ├── dsp_mel.c/.h           # log-Mel 40×64
│   ├── dsp_fft.c/.h           # 512 点实数 FFT（或 esp-dsp）
│   └── rule_f0.c/.h           # YIN + DTW + 评分
├── ai/
│   ├── infer_tflm.cc/.h       # TFLM 封装（C++）
│   ├── model_data.cc          # 生成的 int8 模型数组
│   └── model_params.h         # 输入/输出量化参数
└── Kconfig.projbuild          # APP_MODE_DEMO / APP_BENCH / 每日新词数

components/
├── epd_ssd1619a/              # 现有，保留
├── ui_fonts/                  # 生成的 32/16/16 px 字库（C 数组）
└── (managed) esp_littlefs, esp-tflite-micro

tools/
├── assets/                    # words.csv → words.bin/audio.pak/ref_f0.bin
├── fonts/                     # gen_fonts.py + 字库清单
├── collect/                   # PC 采集/下载/校验
├── train/                     # dataset/features/model/train/quantize/eval
├── eval/                      # 指标、图表、显著性检验
├── power/                     # INA219/PPK2 CSV 分析
├── preview/                   # 离线页面渲染 + golden PNG
└── acceptance/                # 串口自动化验收脚本

test/
├── golden/                    # PCM/mel/prob golden 向量
├── host/                      # 可选：PC 端 C 单测（需装 LLVM/MinGW）
└── device/                    # bench selftest 脚本

data/
├── words.csv                  # 100 词源数据（UTF-8）
├── audio_src/                 # 参考发音 WAV（16 kHz）
├── dataset/                   # 采集数据（默认不入库，隐私）
└── results/                   # 实验结果 CSV/图
```

### 4.2 关键 API 草案

```c
/* wordbank.h */
typedef struct {
    uint16_t id;
    char word[24];
    char phonetic[40];
    char meaning[48];
    char example[80];
    uint32_t audio_off;
    uint32_t audio_len;
    uint16_t f0_off;        /* ref_f0.bin 中的 64 点偏移 */
    uint8_t  level_default;
    uint8_t  flags;
} word_t;

esp_err_t   wordbank_init(void);
int         wordbank_count(void);
const word_t *wordbank_get(int id);
esp_err_t   wordbank_audio(int id, const uint8_t **data, size_t *len);
esp_err_t   wordbank_ref_f0(int id, int16_t *out, int *n);

/* scheduler.h：Leitner 6 盒，间隔 1/2/4/7/15/30 天 */
esp_err_t sched_init(void);
int       sched_build_daily(int new_words, int *ids, int max_ids);
esp_err_t sched_grade(int id, bool remembered);
int       sched_due_count(void);

/* audio_in/out.h */
esp_err_t audio_init(void);
esp_err_t audio_record_start(uint32_t ms);
esp_err_t audio_record_stop(int16_t **pcm, size_t *samples); /* PSRAM */
esp_err_t audio_play_word(int id);
esp_err_t audio_play_pcm(const int16_t *pcm, size_t samples);

/* dsp_mel.h */
esp_err_t mel_init(void);
esp_err_t mel_compute(const int16_t *pcm, size_t n,
                      int8_t out[40 * 64]);   /* 已量化，scale 见 params */

/* infer_tflm.h */
typedef struct {
    float    prob_correct;   /* 反量化后的概率 */
    int      label;          /* 0/1 */
    uint32_t latency_us;
    size_t   arena_used;
} infer_result_t;

esp_err_t infer_init(void);
esp_err_t infer_run(const int8_t *mel, infer_result_t *out);

/* rule_f0.h */
typedef struct {
    float    score;          /* 0–100 */
    int      label;          /* 0/1 */
    uint32_t latency_us;
} rule_result_t;

esp_err_t rule_run(const int16_t *pcm, size_t n, int word_id, rule_result_t *out);

/* ui.h：view model + 页面 + dirty band */
typedef struct {
    int learned_today, total_today, due_count, streak;
    int battery_mv;
} home_vm_t;

typedef struct {
    const char *word;
    const char *phonetic;
    int index, total, level;
} card_vm_t;

typedef struct {
    int cnn_label, rule_label;
    int cnn_score, rule_score;
    uint32_t cnn_us, rule_us;
    const char *hint;        /* “仅作学习参考” */
} result_vm_t;

void ui_init(void);
void ui_show_home(const home_vm_t *vm);
void ui_show_card(const card_vm_t *vm);
void ui_show_result(const result_vm_t *vm);
void ui_flush(void);          /* 自动选 band/full */

/* records.h */
typedef struct {
    uint32_t ts;
    uint16_t word_id;
    uint8_t  result;          /* 0/1 */
    uint8_t  method;          /* 0=cnn 1=rule 2=both */
    uint8_t  score;
    uint8_t  rule_score;
    uint8_t  rule_result;
    uint16_t latency_ms;
    uint16_t rule_latency_ms;
    uint8_t  level_before;
    uint8_t  level_after;
    uint16_t crc;
} record_t;

esp_err_t records_init(void);
esp_err_t records_append(const record_t *r);
size_t    records_count(void);
esp_err_t records_export_csv(const char *path);   /* 写 LittleFS，HTTP 再流式读取 */

/* power.h */
esp_err_t power_init(void);
void      power_idle(void);        /* 轻睡/降频 */
void      power_deep_sleep(void);  /* 面板深睡 + 系统深睡 */
```

### 4.3 数据格式

**`words.bin`**（LittleFS，小端）：

```text
magic "EWB1" (4 B) | version u16 | count u16 | record_size u16 | reserved u16
audio_pack_offset u32 | audio_pack_len u32 | ref_f0_offset u32 | ref_f0_len u32
crc32 u32
word_t records[count]
```

**`audio.pak`**：每词一段 IMA ADPCM（4 bit，16 kHz 单声道），头部 16 B：`magic "EAP1" (4 B) | sample_rate u16 | channels u8 | bits u8 | samples u32 | reserved u32`。播放时解码到 int16。

**`ref_f0.bin`**：每词 64 点，`int16` 存“半音 × 100”，最高位为 voicing 标志；或分两个数组（f0 + voiced），实现时二选一，但 PC/固件必须共用同一规范。

**`records.bin`**：`magic "ERC1" | version u16 | head u16 | count u16 | entry_size u16 | reserved` + 1024 条 `record_t` 环形；每条带 CRC16/CRC32，写入后 `fsync` 语义（LittleFS 本身掉电安全）。

**采集 manifest**：

```csv
utt_id,speaker_id,gender,word_id,label,rating,filename,ts,take,split
u0001,S01,M,12,1,5,s01_w012_c_01.wav,1760000000,1,train
```

- `label`：0/1（人工共识二分类）。
- `rating`：1–5（人工发音质量评分，用于连续分数相关性）。
- `split`：由说话人独立划分生成，训练时只读，不允许随机重分。

---

## 5. 里程碑

> 每个里程碑的“门禁”是硬条件；未通过则在本阶段内修复，不带入下一阶段。

### M0（W1，10/26–11/01）基线冻结与工程骨架

> **状态：已完成（2026-10-25）**。分区/LittleFS、5 键驱动、bench 控制台、band 位置扫描、训练环境均已落地；实测见 [`docs/M0测试记录.md`](docs/M0测试记录.md)。物理按键按下/长按仍需人工确认。

**目标**：把现有显示 demo 变成可扩展的工程，验证存储/按键/band 位置，建立训练环境。

- [ ] 打 tag `v0.1-display`；从 `main` 开分支 `feat/m0-skeleton`。
- [ ] 写 `docs/decisions.md`，落定 D1–D12（见第 6 节）。
- [ ] `partitions.csv` 增加 `storage`（littlefs，10 M），`nvs` 扩到 0x6000；`idf.py build` 通过。
- [ ] 接入 `joltwallet/esp_littlefs`（`idf_component.yml`）；挂载、写 64 KB、读回校验；断电 10 次不损坏。若拉取失败，2 h 内切 FATFS。
- [ ] 新建 `main/board/board_pins.h`；5 键驱动（ISR + 队列 + 20 ms 消抖 + 长按），串口打印键值。
- [ ] `Kconfig.projbuild` 增加 `APP_MODE_DEMO`（默认 n）；重构 `main.c` → `app_main.c` + `app_state.c`；demo 仍可一键编译回归。
- [ ] `bench` 控制台（USB-Serial-JTAG）：`bench info/fs/display band/full/clean`。
- [ ] **E0**：band 位置/高度扫描：`y0 ∈ {35, 95, 170, 230}` × `h ∈ {40, 70, 100}`，跳过越界组合（如 230/100），各 3 次；记录耗时、带状外是否静止、残影、MUX 切换是否异常；选最终布局。
- [ ] E0 阶段先保留 15000 B 缓冲；布局确定后把 `g_part_tmp` 按最大 band 改为 70 行（3500 B）或动态分配，回归 demo。
- [ ] 修正许可证：新增 `THIRD_PARTY_NOTICES.md`，把 AGPL LUT 单独标注；与导师确认发布方式（D9）。
- [ ] 训练环境：运行 `pwsh -File tools/setup/install-train.ps1`（真实 venv 在 `C:\venv-eink`，项目内 `.venv-train` 为 junction；原因见 `docs/toolchain.md`），验证 `import torch`。
- [ ] 起草 `data/words.csv`（至少 50 词，目标 100），含 IPA/释义/例句；写覆盖检查脚本（IPA/汉字是否都在字库计划内）。

**门禁**：`idf.py build` 零 error；demo 仍 346 ms；LittleFS 10 次断电测试通过；5 键可用；E0 表完整；torch 可 import。

**风险**：`esp_littlefs` 首次拉取需要网络；GPIO1/2/47/48 可能被开发板占用；band 在 y=35/230 可能不稳定。对应兜底见第 9 节。

### M2（W5–W6，11/23–12/06）音频链路

**目标**：录音 2 s、回放清晰，UI 有录音/播放反馈。

- [ ] `audio_in`：I2S0 RX，16 kHz/16 bit 单声道，INMP441；DMA 8×256 B；PSRAM 缓冲 2.2 s。
- [ ] INMP441 供电门控：MOS/负载开关，录音前 10 ms 上电、录完断电；否则约 1.6 mA 静态电流会让深睡目标失效。
- [ ] INMP441 24 bit 左对齐数据 → 取高 16 bit；去直流；增益校准；削顶检测。
- [ ] WAV 写入：44 B 头 + PCM16；文件名 `rec_YYYYmmdd_HHMMSS.wav`；写 LittleFS。
- [ ] `audio_out`：I2S1 TX，16 kHz/16 bit 单声道，MAX98357A；WAV/ADPCM 解码；`AMP_SD` 播放前 10 ms 拉高、结束后 50 ms 拉低。
- [ ] 录放互锁：播放时关麦克风，录音时关功放，避免啸叫。
- [ ] VAD：短时能量 + 过零率；噪声底估计；去掉首尾静音，保留 100 ms；有效语音 <0.3 s 提示“未检测到语音”。
- [ ] UI：录音中电平条（band 内 ≤2 Hz 节流，346 ms/次）、播放图标、剩余空间提示。
- [ ] 声学自测：手机播 1 kHz 正弦，录 2 s，FFT 看峰值/SNR/DC/削顶；记录麦克风距离和增益；`bench audio rec/play`。
- [ ] 连续录 20 条，检查文件完整、回放无爆音、存储余量正确。

**门禁**：20/20 条录音可懂、无削顶；回放清晰；录音启动 <100 ms；100 条 2 s WAV 能存下；电流粗测记录。

**风险**：INMP441 位对齐/声道选错；MAX98357A 在 3.3 V 下音量不足；I2S 时钟与 Wi-Fi 共存。兜底见第 9 节。

---

### M3（W7–W8，12/07–12/20）规则法 + 采集工具

**目标**：先有“不依赖数据”的评分基线，同时把数据采集通道打通。

- [ ] `rule_f0`：16 kHz → 8 kHz 降采样；32 ms 窗/16 ms  hop；YIN/自相关，70–400 Hz；voicing 判决；得到 F0 轮廓。
- [ ] 参考轮廓：资产构建时对每个参考词音频算 64 点 F0（半音归一化 + voiced 标志），存 `ref_f0.bin`；最终用真人参考发音，TTS 仅作临时占位并在论文中说明。
- [ ] DTW：Sakoe-Chiba band ±10；代价 = 半音差 + voicing 不一致惩罚；归一化距离。
- [ ] 规则特征：平均半音偏差、轮廓相关、voiced 比例、时长比、能量比；固定权重 + 训练折选阈值 → 0–100 分 + 二分类。
- [ ] Python 镜像实现（`tools/train/rule_ref.py`）；C/Python 在 20 条 pilot 上对齐：F0 中位数差 <2%，标签一致 ≥95%。
- [ ] 结果页显示规则法分数/耗时；方法切换（CNN/规则/两者）。
- [ ] 采集模式固件：选择 speaker/word/label/take；录音 2 s；写 WAV + `collect_meta.csv`；显示已录条数/剩余空间。
- [ ] HTTP 采集接口：`GET /api/collect/list`、`GET /api/collect/file?name=`、`POST /api/collect/delete`。
- [ ] PC 采集器 `tools/collect/collect.py`：连 SoftAP，列文件、下载、校验 WAV、落 `dataset/raw/`、生成 manifest、可选删除。
- [ ] 标注规范（1–5 分 + 二分类）和知情同意书模板；2 名标注员培训。

**门禁**：规则法在 20 条 pilot 上能给出合理分数；PC 能下载 20 条 WAV + 元数据；C/Python F0 对齐达标。

---

### M4（W9–W11，12/21–01/10）数据集采集与标注

**目标**：≥200 条（目标 600–800 + 80 条词泛化）真实跟读、说话人独立、双人标注。

- [ ] 招募 6–8 名说话人（男女均衡、不同英语水平）；匿名编号 S01–S08；签同意书。
- [ ] Pilot 1 人：校准麦克风距离（20 cm）、房间噪声、录音增益；确认 50 词清单覆盖难音（/θ/ <u>ð</u> <u>ʃ</u> <u>ʒ</u> <u>ŋ</u> <u>r</u> <u>l</u> <u>v</u> /w/，含 10 组最小对立体）。
- [ ] 采集协议：50 核心词 × 每人 2 条（自然朗读 + 指定错误类型），6 人时 600 条、8 人时 800 条；**交错录制正确/错误**，避免顺序/疲劳偏置；每条 2 s。
- [ ] 词泛化补充：另选 10 个不在训练集的词，采 4 人 × 2 条 = 80 条；只用于“词泛化”测试，不参与训练。
- [ ] 分批下载（每批 ≤100 条，约 6.4 MB），双备份；WAV 头/时长/削顶自动校验。
- [ ] 标注：2 名标注员盲听，给 1–5 分 + 二分类；分歧由第三人裁决；计算 Cohen's kappa（二分类）和 ICC（1–5）。
- [ ] 数据清洗：去掉 <0.3 s、削顶、强噪声、标签边界样本；记录剔除原因和数量。
- [ ] 数据集冻结 v1.0：`dataset/v1.0/` + `labels.csv` + `splits.json` + SHA256 + README；按说话人 LOSO 划分，绝不随机按样本划分。
- [ ] 数据集统计图：说话人×标签、词×标签、时长分布、SNR 分布、评分分布。


**门禁**：≥200 条（目标 600–800 + 80 词泛化）标注完成；≥5 名说话人；二分类 kappa ≥0.6；划分文件冻结；隐私处理完成。

**风险**：招募不足、错误样本不自然、标注疲劳。兜底：先做 4 人×50 词×2=400 条；错误类型用提示卡控制；分 3 次标注、每次 ≤30 min。

---

### M5（W12–W14，01/11–01/31）特征、模型、量化

**目标**：PC 端 log-Mel + int8 CNN 跑通，LOSO 评估，导出可部署模型。

- [ ] `features.py`：去直流 → VAD/裁剪/补齐 2 s → 预加重 0.97 → 25 ms Hamming/10 ms hop → 512 点 FFT → 40 Mel（20–7600 Hz）→ log → 每句 CMVN → 时间轴自适应池化到 64 帧 → int8 量化。
- [ ] **特征规范唯一来源**：把窗长/hop/Mel 公式/池化/量化参数写进 `tools/train/FEATURES.md`；C 端逐条照抄，不“凭感觉实现”。
- [ ] 数据集统计 + 基线：多数类、逻辑回归（可选）、小型 CNN。
- [ ] CNN：Conv16-3×3 → BN/ReLU → MaxPool → Conv32 → MaxPool → Conv64 → GAP → Dropout0.3 → Dense2；参数量约 2 万。
- [ ] 训练：AdamW 1e-3、batch 32、≤80 epochs、cosine、early stopping、class weight、label smoothing 0.05；增强（增益 ±6 dB、时移 ±100 ms、加噪 SNR 15–30 dB、轻微变速 0.9–1.1）只用于训练。
- [ ] LOSO 6 折（或 8 折）：内层验证选阈值/温度缩放；外层测试只评一次；报告 mean±std + bootstrap 95% CI；混淆矩阵、AUC、F1、per-speaker。
- [ ] 转换路径：PyTorch → ONNX → onnx2tf → TFLite（或 `ai-edge-torch` 直接导出）；M5 第 1 周先做 2 h spike；失败则 Keras 复现或手写 int8。
- [ ] 量化：TFLite full int8；代表性数据集 200 条；比较 float/int8 的 acc/F1；掉点 >3% 做 QAT。
- [ ] 校准：温度缩放/Platt；报告 ECE/Brier；分数 = 校准后 p(correct) × 100。
- [ ] 消融（至少 3 组）：Mel 40 vs 64、时间 64 vs 96、无增强、通道 16/32/64 vs 8/16/32。
- [ ] 错误分析：按词/音素/说话人/性别；听 FP/FN；检查是否学到说话人/信道捷径（同词不同人、同人不同词）。
- [ ] 防标签泄漏：标签只从 `labels.csv` 读，不从文件名/目录名推断；清洗/划分脚本与训练脚本分离；测试集在训练完成前不加载。
- [ ] 导出：`model.tflite`、`model_data.cc`、`model_params.h`、`golden.npz`（PCM/mel/prob/label，20 条）。

**门禁**：int8 LOSO 准确率 ≥75%（达不到则如实报告 + 分析原因）；模型 <100 KB；PC 端 golden 可复现；FEATURES.md 冻结。

---

### M6（W15–W16，02/01–02/14）单片机部署与 parity

**目标**：同一段录音在设备上得到与 PC 一致的结果，耗时达标。

- [ ] 接入 `esp-tflite-micro`（固定版本）；`infer_init()` 加载模型、OpResolver、arena 32–64 KB；`AllocateTensors` 检查。
- [ ] `dsp_mel`：C 实现 512 点实数 FFT + Mel 滤波器组 + log + CMVN + 池化 + int8；优先用 `esp-dsp`，否则自带 radix-2。
- [ ] `rule_f0` 上板：8 kHz 降采样、YIN、DTW；与 Python 对齐。
- [ ] Parity：设备读 `/test/golden.bin`（或串口下发）→ 算 mel/推理/规则 → 打印 max abs diff；验收：mel ≤1 LSB、prob ≤0.02、标签 20/20。
- [ ] 基准：100 次推理，记录 mel/CNN/rule 分段耗时 mean/P50/P95/max；arena used；CPU 240/160 MHz 对比。
- [ ] 集成结果页：CNN 分数/标签/耗时 + 规则分数/标签/耗时 + 免责声明；两法不一致时明确显示“结果仅供参考”。
- [ ] 优化：`-O2`、热点放 IRAM、FFT 缓存、避免 PSRAM 随机访问；必要时降低 Mel 帧数。
- [ ] **兜底**：若 TFLM 在 IDF 6.1 上不可用/算子缺失/arena 超限，3 天内切手写 int8 推理（conv/pool/GAP/dense），保持同一 `infer_run()` API；论文中说明实现方式。

**门禁**：设备与 PC 标签一致 ≥95%；mel/prob parity 达标；推理 <200 ms；100 次稳定；arena <64 KB。

> 注：W15 含春节（2027-02-06），预留低强度；若进度紧，M6 可顺延 1 周，从 M8 的缓冲借时间。

---

### M7（W17–W18，02/15–02/28）无线同步、功耗、整机联调

**目标**：形成完整闭环，拿到功耗/刷新/同步实测数据。

- [ ] Wi-Fi SoftAP：`EPD-Word-XXXX`，WPA2；IP 192.168.4.1；3 min 无操作自动关 Wi-Fi。
- [ ] HTTP：`GET /` 仪表盘、`GET /api/status`、`GET /api/records.csv|json`、`GET /api/audio/list|file`、`POST /api/ack`、`POST /api/time`（浏览器回传 epoch）。
- [ ] 记录同步：标记已同步、断点续传（学习记录按记录序号/CRC，采集音频按 utt_id）、失败重试；CSV 列见 4.3。
- [ ] 时间：有 STA 凭据时 SNTP；仅 SoftAP 时用浏览器 `POST /api/time`；无网络时用“开机天数 + 单调时钟”，同步后校正。
- [ ] 功耗：`CONFIG_PM_ENABLE` + tickless idle + DFS（240/80 MHz）；空闲 30 s 轻睡；5 min 深睡；5 键 EXT1 唤醒；唤醒后重初始化面板、恢复页面。
- [ ] 面板策略：会话内不深睡；会话结束/深睡前 `epd_deep_sleep()`；避免每次刷新后深睡（D7）。
- [ ] 电池：ADC 分压读数、低电提示（3.4 V）、充电状态（TP4056 的 CHRG/STDBY 可选 GPIO）。
- [ ] INA219：I²C 39/40，0.1 Ω shunt；读总线电压/电流/功率；`bench power`；同步/录音/推理时打点。
- [ ] 静态负载排查：INA219（约 1 mA）、INMP441（约 1.6 mA）、电池分压（若用 100k/100k 约 21 µA；本设计用 1M/1M）都会让深睡超标；测量时按需供电，产品态用跳线/MOS 断开。
- [ ] **E7 测量**：电池供电、USB 断开；状态：深睡、轻睡、空闲、band 刷新、整屏、清洁、录音 2 s、推理、播放、Wi-Fi 同步；每状态 3 次；PPK2/µA 表测深睡；USB 电流表做整机平均电流交叉校验；Python 积分出 mJ/次、mAh/天。
- [ ] 整机流程：HOME → CARD → PLAY → RECORD → RESULT → 自评 → 下一词 → STATS → SYNC；错误页/低电/存储满/无语音。
- [ ] 30 min soak：200 词 + 20 条录音 + 1 次同步；记录 heap/PSRAM 水位、看门狗、重启原因。
- [ ] 500 次 band 刷新残影测试：每 50 次拍照；清洁后对比；记录可接受的最大连续次数。
- [ ] 掉电测试：记录追加过程中断电 20 次；重启后记录完整/可恢复，LittleFS 不损坏。

**门禁**：50/50 次同步成功；深睡电流有实测值（不达标也如实记录）；30 min 无崩溃/无泄漏；E7 表完整；500 次残影可恢复。

---

### M8（W19–W24，03/01–04/11）对照实验、论文、答辩

**目标**：把“做了”变成“可证明、可复现、可答辩”。

- [ ] 冻结实验代码 `v1.0-experiment`；所有实验脚本固定随机种子；结果写 `data/results/`。
- [ ] E1 数据集统计；E2 模型 LOSO；E3 量化；E4 规则法；E5 两法对照；E6 消融；E7 功耗/耗时；E8 系统/可靠性。
- [ ] 统计检验：McNemar（两法配对准确率）、Cohen's kappa（与人工/两法）、Pearson/Spearman（分数）、Bland–Altman、bootstrap CI。
- [ ] 图表：系统框图、状态机、页面照片、band 时序、刷新对比、数据集分布、Mel 示例、模型结构、混淆矩阵、ROC、分数相关、功耗时间线、能量拆分、耗时箱线图、残影照片。
- [ ] 论文：按“绪论 → 需求/方案 → 硬件 → 显示与局刷 → 音频与模型 → 规则法 → 软件实现 → 实验 → 总结”组织；每章对应实验/表。
- [ ] 写作节奏：W20 完成 1–3 章；W21 完成 4–6 章；W22 完成 7–8 章；W23 全文整合/查重/格式；W24 导师审阅/修改。
- [ ] 演示视频脚本：开机 → 翻词 → 播放 → 跟读 → 结果 → 自评 → 统计 → 同步 → 功耗测量；≤5 min。
- [ ] 答辩 PPT + Q\&A：为什么 ESP32-S3、为什么带状不是 2D、样本少怎么办、模型是否学到说话人、功耗是否达标、AGPL 怎么处理。
- [ ] 代码清理：README 更新、`THIRD_PARTY_NOTICES.md`、删除临时实验代码、保留 demo/bench、资源生成脚本可一键复现。

**门禁**：论文初稿完成；所有结论有数据/图支撑；演示可复现；导师审阅通过。

---

## 6. 关键设计决策（含备选）

| 编号  | 决策   | 选择                                                                                       | 理由                                   | 备选/触发                                        |
| --- | ---- | ---------------------------------------------------------------------------------------- | ------------------------------------ | -------------------------------------------- |
| D1  | 显示刷新 | 单词/结果用 70 行 band；页面切换用整屏；结果 band 仅在 E0 通过时启用                                             | 已有 346 ms 实测；整屏 700 ms；不引入未验证的 2D 窗口 | E0 失败 → 单 band + 结果整屏                        |
| D2  | 端侧推理 | TFLM int8；arena 32–64 KB                                                                 | 论文主流、可复现、算子够用                        | 3 天集成失败/算子缺失 → 手写 int8 推理                    |
| D3  | 文件系统 | LittleFS 10 MB `storage`                                                                 | 掉电安全、适合记录/音频；分区工具支持 `littlefs` 子类型   | 组件拉取失败 → FATFS + wear_levelling，API 不变       |
| D4  | 字库   | TTF 子集：32 px 单词、16 px 音标/中文、12 px 标签；每字 1 bpp                                            | 400×300 下可读；只生成用到的字，体积小              | 中文/ IPA 覆盖不足 → 英文释义 + 5×7 兜底                 |
| D5  | 数据集  | 50 核心词 × 6–8 人 × 2 条 = 600–800；LOSO；另采 10 个未训练词 × 4 人 × 2 条 = 80 条做词泛化                   | 满足 ≥200，且能按说话人独立评估；控制采集量             | 招募不足 → 4 人 400 条；报告 CI 和限制                   |
| D6  | 评分   | 模型：校准后 p(correct)×100；规则：加权特征 + 训练折阈值；人工 1–5 + 二分类（≥4 为正）；Leitner 由“记住/没记住”自评驱动，发音分数只作反馈 | 同时支持二分类指标和连续分数相关性；避免误判直接影响复习调度       | 若 1–5 标注不可行 → 只做二分类 + 两法分数相关                 |
| D7  | 深睡   | 会话级深睡；会话内面板保持；会话结束/5 min 无操作才面板+系统深睡                                                     | 面板深睡后再写屏需硬件复位/重初始化；每刷深睡不可行           | 若待机功耗必须极低 → 每次交互后重初始化，接受 1–2 s 唤醒            |
| D8  | 时间   | 浏览器 `POST /api/time` 为主；STA 时 SNTP；无网络用单调时钟+开机天数                                         | SoftAP 无互联网，SNTP 不可用                 | 加 RTC 芯片（如 PCF8563）→ 硬件时间                    |
| D9  | 许可证  | 隔离 AGPL LUT，保留 `THIRD_PARTY_NOTICES.md`；论文注明；发布前二选一：整项目 AGPL 或重写 LUT                     | 学术使用可接受；不能继续声称整体 MIT                 | 导师要求 MIT/商用 → 从 datasheet 重写 LUT（高风险，预留 1 周） |
| D10 | 词库规模 | 100 词（50 核心 + 50 扩展）；数据集只用 50 核心词；扩展词先只用规则法/显示，模型通过词泛化后再启用                               | 满足 ≥50；控制采集/标注量；避免未验证词误判             | 时间紧 → 50 词；模型/字体按 50 词先闭环                    |
| D11 | 交互   | 5 键；不做触摸                                                                                 | BOM 简单、低功耗、可靠                        | 后续可加电阻触摸（需 ADC + 校准）                         |
| D12 | 同步   | SoftAP + HTTP；不做 App/云                                                                   | 无需路由器、手机浏览器即可；答辩演示稳定                 | 需要外网 → STA 模式 + 局域网服务器（非必需）                  |

**几个容易踩的坑，提前写死：**

- `epd_display_area()` 不是局刷，别拿它做单词切换。
- 带状刷新必须写整条 400 px 宽；只写单词框会横带闪。
- 每次写 RAM 前重新下发 `0x44/0x45/0x4E/0x4F`。
- `0xC7` 刷新后时钟关闭，写 RAM 前要 `0x22=0x80 + 0x20`。
- 深睡后 BUSY 一直高，唤醒必须硬件复位 + 重新初始化。
- 不要在 USB 连接/充电时测深睡电流。
- 数据集不能按样本随机划分，否则说话人泄漏，准确率虚高。

---

## 7. 实验设计

### 7.1 实验总表

| 编号 | 问题               | 方法                                | 主要指标                        | 产出          |
| -- | ---------------- | --------------------------------- | --------------------------- | ----------- |
| E0 | band 位置/高度是否都稳定？ | y0×h 扫描，各 3 次                     | 耗时、带状外是否静止、残影、MUX 切换        | 表 + 照片；最终布局 |
| E1 | 数据集质量如何？         | 统计 + 双人标注                         | 人数/词数/标签/时长/SNR、kappa/ICC   | 表 + 分布图     |
| E2 | 模型 LOSO 准确率？     | 6–8 折说话人独立                        | acc/prec/rec/F1/AUC/混淆矩阵/CI | 表 + 混淆矩阵    |
| E3 | int8 量化掉点多少？     | float vs int8                     | acc/F1、模型大小、耗时              | 表           |
| E4 | 规则法准确率/阈值敏感性？    | 同折，阈值扫描                           | acc/F1、阈值曲线、耗时              | 表 + 曲线      |
| E5 | 两法差异？            | McNemar、kappa、相关、Bland–Altman     | 准确率差、一致性、分数相关、耗时            | 表 + 图       |
| E6 | 哪些设计有效？          | 消融：Mel 维度/帧数/增强/宽度                | acc/F1 变化                   | 表           |
| E7 | 功耗/耗时多少？         | INA219/PPK2 + `esp_timer`         | 电流、能量、mJ/次、mAh/天、耗时         | 表 + 时间线     |
| E8 | 系统可靠吗？           | 30 min soak、500 次刷新、50 次同步、20 次掉电 | 崩溃/泄漏/残影/成功率/记录完整性          | 表 + 照片      |

### 7.2 E0：band 位置/高度扫描（M0 必做）

**目的**：确认 `epd_band_update()` 不只对 y=95..164 有效；为“结果 band”和更窄带优化提供依据。

**步骤**：

1. 画统一测试底图（外框 + 标题 + 测试文字）。
2. 对每个 `(y0, h)`：先整屏底图，再写交替黑白棋盘/文字，调用 `epd_band_update(frame, y0, y0+h-1, 5000)`。
3. 记录 BUSY 时间（`esp_timer`），重复 3 次。
4. 拍照/目视：带状外是否完全不动；带内是否只有文字变化；是否有横带闪；残影程度 1–5。
5. 连续 50 次同带刷新；再切到整屏；再切回另一位置；检查是否错位/花屏。

**记录表**（至少覆盖以下组合，其余有效组合可按同样方法补测；越界组合跳过）：

| y0  | h   | 次数 | 平均耗时/ms | 带状外 | 带内闪 | 残影(1–5) | 备注         |
| --- | --- | -- | ------- | --- | --- | ------- | ---------- |
| 35  | 40  | 3  |         |     |     |         |            |
| 35  | 70  | 3  |         |     |     |         |            |
| 95  | 70  | 3  |         |     |     |         | 当前基线       |
| 95  | 100 | 3  |         |     |     |         |            |
| 170 | 40  | 3  |         |     |     |         | 结果 band 候选 |
| 170 | 70  | 3  |         |     |     |         | 双 band 候选  |
| 170 | 100 | 3  |         |     |     |         |            |
| 230 | 40  | 3  |         |     |     |         |            |
| 230 | 70  | 3  |         |     |     |         |            |

**判定**：若 y=170 的 40 行或 70 行都稳定，则结果页用双 band（40 行更省扫描）；若不稳定，结果页复用 95–164 或整屏 700 ms。若 h=40 稳定且残影可接受，则评估把 70 行压到 40–50 行，刷新时间可能进一步下降。

### 7.3 E2：模型 LOSO

- 外层：留一说话人（6–8 折）；内层：从训练说话人中再留 1 人做阈值/校准。
- 每个外层测试折只评一次；不根据测试结果调参。
- 指标：Accuracy、Precision、Recall、F1、AUROC、混淆矩阵；mean±std；bootstrap 95% CI（按说话人重采样）。
- 分组：性别、母语/口音（如可标注）、单词难度、录音 SNR。
- 额外：**词泛化**——在 10 个未参与训练的词上测试（4 人×2 条=80 条），单独报告。


### 7.4 E4：规则法

- 同 LOSO 折；阈值只在训练折选（最大化 F1 或 Youden）。
- 阈值敏感性：画 threshold–F1/accuracy 曲线，报告选择点和邻域稳定性。
- 特征消融：只用 F0 偏差、只用相关性、全特征。
- 耗时：C 端 8 kHz YIN + DTW，100 次 mean/P95。
- 失败案例分析：声调/重音、静音裁剪、F0 八度错误、童声/低音。

### 7.5 E5：两法对照（论文核心表）

**样本**：同一批测试录音，同一人工标签。

| 指标                              | 模型法 | 规则法 | 差异/检验     |
| ------------------------------- | --- | --- | --------- |
| Accuracy（mean±std）              |     |     | McNemar p |
| Precision / Recall / F1         |     |     |           |
| 与人工标签 kappa                     |     |     |           |
| 两法决策一致 kappa                    | —   | —   |           |
| 分数 vs 人工 1–5：Pearson / Spearman |     |     |           |
| 两法分数：Pearson / Spearman         | —   | —   |           |
| Bland–Altman 偏差/限               | —   | —   |           |
| 推理耗时 mean/P95（ms）               |     |     |           |
| 模型/代码大小（KB）                     |     |     |           |
| 分性别/说话人准确率                      |     |     |           |

**预期（不是结论）**：模型法准确率可能更高，但样本少、方差大；规则法可解释、耗时低，但对性别/口音/八度错误更敏感。无论结果如何，如实报告；不要为了“模型更好”调测试集。

### 7.6 E7：功耗与耗时

**测量条件**：电池供电、USB 断开、室温；INA219 串在电池/3.3 V 轨；深睡用 PPK2 或 µA 表；USB 电流表用于整机平均电流/充电电流的交叉校验；每个状态 3 次，取中位数；记录固件 commit、亮度/音量、Wi-Fi 信道。

**状态与记录**：

| 状态           | 持续时间        | 平均电流 | 峰值电流 | 能量/mJ | 备注        |
| ------------ | ----------- | ---- | ---- | ----- | --------- |
| 深睡（面板深睡）     | 60 s        |      |      |       | PPK2/µA 表 |
| 轻睡           | 60 s        |      |      |       |           |
| 空闲（屏静态）      | 60 s        |      |      |       |           |
| band 刷新 70 行 | 346 ms × 10 |      |      |       | 单次能量      |
| 整屏底图         | 700 ms × 5  |      |      |       |           |
| 清洁周期         | 2.1 s × 3   |      |      |       |           |
| 录音 2 s       | 2 s × 10    |      |      |       |           |
| 推理（mel+CNN）  | 100 次       |      |      |       | 分段耗时      |
| 播放 1.5 s     | 1.5 s × 10  |      |      |       |           |
| Wi-Fi 同步     | 1 次 100 条   |      |      |       |           |

**换算**：`E = Σ V(t)·I(t)·Δt`（梯形积分）；报告 mJ/次、mAh/天（按每天 20 词 + 10 次录音估算）、理论续航；注明板级/电芯电压。

**耗时表**：mel 预处理、CNN、规则法、显示、WAV 写盘、同步，各自 mean/P50/P95/max；用 `esp_timer`，100 次。

### 7.7 E8：系统与可靠性

- 30 min soak：200 词 + 20 条录音 + 1 次同步；每 5 min 记录 heap/PSRAM/任务栈；看门狗/重启原因。
- 500 次 band 刷新：每 50 次拍照；清洁后对比；记录残影等级和可接受阈值。
- 同步 50 次：10/100/500 条记录；成功率、时间、失败原因。
- 掉电 20 次：记录追加中/文件写入中/同步中；重启后记录完整性、FS 挂载、词库校验。
- 低温/高温（可选）：0/25/40 °C 下刷新对比度和残影；不做也不影响主线。





---

## 8. 测试计划

### 8.1 测试层级

| 层级        | 工具                     | 内容                               | 门禁                            |
| --------- | ---------------------- | -------------------------------- | ----------------------------- |
| Python 单测 | `pytest`               | 特征形状/数值、Mel 滤波器、指标、数据划分、资产校验     | 全绿                            |
| 资产校验      | `build_all.py --check` | 100 词唯一、字段长度、UTF-8、字库覆盖、音频时长/采样率 | 0 error                       |
| UI 预览     | `pages.py --check`     | 页面 PNG 与 golden 差异 < 阈值          | 通过                            |
| 设备自检      | `bench selftest`       | 屏/存储/按键/音频/模型/规则/电池              | 全 pass                        |
| Parity    | `bench parity`         | mel/prob/label 与 PC golden 对齐    | mel ≤1 LSB、prob ≤0.02、标签 ≥95% |
| 集成        | 手工脚本                   | 完整学习流程、异常路径                      | 通过                            |
| 验收        | `acceptance/run.py`    | 自动跑 F1–F12 + P1–P12              | 通过                            |

### 8.2 `bench` 命令（M0 起逐步补齐）

```text
bench info                      # 版本、reset reason、heap/PSRAM、uptime
bench fs                        # 挂载、剩余空间、读写/删除 64KB
bench display band <y0> <h> <n> # n 次 band 刷新，打印 mean/min/max
bench display full              # 整屏耗时
bench display clean             # 清洁周期耗时
bench key                       # 打印 5 键状态/长按
bench audio rec <sec>           # 录音到 LittleFS，打印峰值/DC/SNR
bench audio play <id>           # 播放词条音频
bench mel <path>                # 计算 mel，打印校验和/前 16 字节
bench infer <path>              # CNN 推理，打印 prob/label/latency/arena
bench rule <path>               # 规则法，打印 score/label/latency
bench parity <path>             # 与 golden 对比，打印 max diff
bench power                     # INA219 电压/电流/功率
bench sync                      # 启动 SoftAP + HTTP，打印 IP
bench selftest                  # 依次跑上述关键项
```

### 8.3 Parity 规范

- **Golden 生成**：Python 从测试集抽 20 条（覆盖男女/正确错误/长短），导出 `golden.bin`：`header + PCM int16 + mel int8 + prob float + label`。
- **设备**：`bench parity` 读文件，C 端算 mel/推理，打印 `mel_max_abs_diff`、`prob_max_abs_diff`、`label_match`。
- **验收**：mel ≤1 LSB；prob ≤0.02；label 20/20（或 ≥19/20 并解释）。
- **规则法**：Python/C 的 F0 中位数差 <2%，DTW 分数差 ≤3 分，标签一致 ≥95%。
- **失败处理**：先查 FFT 窗/hop/Mel 公式/池化/量化 zero-point，再查 int8 乘加饱和；不要直接“调参对齐”。

### 8.4 回归清单（每次合并前）

- [ ] `idf.py build` 零 error/warning（至少不新增 warning）。
- [ ] `APP_MODE_DEMO` 仍能 50 词 346 ms 跑完。
- [ ] `bench selftest` 全 pass。
- [ ] 新页面在 `pages.py` 有 golden。
- [ ] 记录/词库格式版本号未破坏旧数据（或提供迁移）。
- [ ] 功耗/耗时没有被无意改坏（至少跑一次 `bench display band`）。
- [ ] 文档/README 更新。

### 8.5 验收脚本（M7–M8）

`tools/acceptance/run.py`：串口连 COM4，自动执行：

1. 复位 → 等 `bench info`。
2. 检查词库 count≥50（目标 100）、FS 剩余空间。
3. 翻 10 词，读 band 耗时。
4. 录音 3 条，检查 WAV 头/时长。
5. 推理 10 次，记录标签/耗时。
6. 规则法 10 次。
7. 追加 10 条记录，导出 CSV，校验行数。
8. 启动同步，HTTP 下载 CSV，校验内容。
9. 输出 JSON 报告 + 退出码。

---

## 9. 风险登记册

| 风险                        | 概率 | 影响 | 触发信号                                   | 应对                                                     | 兜底                                     |
| ------------------------- | -- | -- | -------------------------------------- | ------------------------------------------------------ | -------------------------------------- |
| TFLM 在 IDF 6.1 集成失败/算子缺失  | 中  | 高  | `AllocateTensors` 失败、算子未注册、arena 超限    | M0 确认组件可用、M5 第 1 周 spike；固定版本；最小模型                     | 手写 int8 推理（conv/pool/GAP/dense），保持 API |
| PyTorch→TFLite 转换失败/量化掉点大 | 中  | 高  | ONNX 算子不支持、输出与 PyTorch 不一致、int8 掉点 >3% | M5 第 1 周 2 h spike；优先 `onnx2tf`，备选 `ai-edge-torch`；QAT | Keras 复现；手写 int8 引擎；PC 端推理保底           |
| 样本少/说话人泄漏导致准确率虚高          | 高  | 高  | LOSO 与随机划分差距大；per-speaker 波动大          | LOSO、双人标注、CI、增强；词泛化测试                                  | 如实报告限制；扩大到 800 条                       |
| 模型学到说话人/信道而非发音            | 中  | 高  | 同人不同词易分、同词不同人难分；混淆矩阵异常                 | 同麦、交错录制、CMVN、per-speaker 评估、词泛化                        | 加入说话人对抗/更严格增强；论文讨论                     |
| 音频硬件噪声/回声/削顶              | 中  | 中  | SNR 低、DC 大、播放啸叫                        | INMP441 数字麦、录放互锁、VAD、固定距离、屏蔽线                          | 换 MAX9814/外置声卡做对照；先保证可懂                |
| band 在 y=35/170/230 不稳定   | 中  | 中  | 带状外闪、错位、花屏                             | E0 先测；MUX/gate 每次重设；限制常用位置                             | 只用 95–164 单 band；结果整屏                  |
| 深睡电流 >100 µA（YD 板 PWR LED/CH343P） | 高  | 中  | 实测几百 µA–mA；拔掉 PWR LED 后明显下降        | 断 USB、拆/断 PWR LED、PPK2 定位；报告板级                          | 说明开发板限制；自制定板/换模块                       |
| 静态负载拉高深睡电流                | 高  | 中  | 深睡 >100 µA，断开 INA219/麦克风后下降            | INA219 跳线、麦克风 MOS 门控、分压 1M/1M；逐项断电定位                   | 产品态移除 INA219；麦克风/分压按需供电                |
| AGPL LUT 发布冲突             | 中  | 高  | 论文/代码公开/商用                             | 隔离 + NOTICE + 论文注明；导师确认                                | 整项目 AGPL；或重写 LUT（预留 1 周）               |
| 字库缺字（IPA/中文）              | 中  | 中  | 显示 `?`、方框                              | 构建时覆盖检查；只生成用到的字                                        | 英文释义 + 5×7 兜底；减少词表                     |
| 采集招募不足/标注疲劳               | 中  | 高  | <4 人、标注一致性低                            | 先 4 人 400 条；分 3 次标注；远程录音协议                             | 用公开语音 + 自录补充；报告样本量限制                   |
| 时间不足                      | 高  | 高  | M4/M5 延期 >1 周                          | 保闭环：先 50 词 + 规则法 + 模型小跑；砍触摸/OTA/外壳                     | 降低词数/样本数，保论文核心对照                       |
| Wi-Fi 同步不稳定               | 低  | 中  | 下载中断、IP 冲突                             | 固定信道、AP 模式、重试、分块下载                                     | USB 串口导出 CSV 作为备份                      |
| 记录/FS 掉电损坏                | 中  | 中  | 重启后挂载失败/记录丢失                           | LittleFS、环形 + CRC、追加写、掉电测试                             | 双缓冲/备份文件；导出前校验                         |
| 面板长期残影/寿命                 | 中  | 中  | 500 次后明显残影                             | 每 20–30 次清洁；拍照记录；限制连续刷新                                | 提高清洁频率；论文如实说明                          |
| Python 3.14 无 torch 轮子    | 低  | 中  | `pip install torch` 失败                 | 用 3.12 venv（已确认存在）                                     | 3.13/3.12 + CPU 版；避免 3.14              |
| 开发板 GPIO1/2/47/48 被占用     | 低  | 中  | 按键/ADC/功放使能异常                          | M0 对照原理图；飞线/换脚                                         | SYNC 改 GPIO38（放弃该键深睡唤醒）或换板             |

---

## 10. 时间表（24 周 + 2 周缓冲）

| 周       | 日期          | 里程碑 | 主要产出                          | 门禁               |
| ------- | ----------- | --- | ----------------------------- | ---------------- |
| W1      | 10/26–11/01 | M0  | 骨架、分区、按键、bench、E0、venv        | build/demo/FS/E0 |
| W2      | 11/02–11/08 | M1  | 资产构建器、100 词、字库                | 词库/字库校验          |
| W3      | 11/09–11/15 | M1  | UI 页面、dirty band、Leitner      | 100 词可浏览         |
| W4      | 11/16–11/22 | M1  | 记录、统计、预览 golden               | 重启不丢/CSV         |
| W5      | 11/23–11/29 | M2  | I2S RX/TX、WAV、VAD             | 录放可用             |
| W6      | 11/30–12/06 | M2  | 声学自测、电平 UI、20 条验证             | 音频门禁             |
| W7      | 12/07–12/13 | M3  | YIN/DTW、C/Python 镜像           | 规则法 pilot        |
| W8      | 12/14–12/20 | M3  | 采集模式、PC 采集器、标注规范              | 20 条下载           |
| W9      | 12/21–12/27 | M4  | 招募、pilot、协议                   | 1 人 pilot        |
| W10     | 12/28–01/03 | M4  | 采集 300 条                      | 数据备份             |
| W11     | 01/04–01/10 | M4  | 采集/标注/冻结                      | ≥200 条、kappa     |
| W12     | 01/11–01/17 | M5  | 特征、数据集统计、基线                   | 特征冻结             |
| W13     | 01/18–01/24 | M5  | CNN LOSO、调参                   | LOSO 结果          |
| W14     | 01/25–01/31 | M5  | 量化、消融、导出                      | int8/模型/golden   |
| W15     | 02/01–02/07 | M6  | TFLM / mel C / parity（春节，低强度） | 设备 parity        |
| W16     | 02/08–02/14 | M6  | 性能优化、结果页                      | 推理 <200 ms       |
| W17     | 02/15–02/21 | M7  | SoftAP/HTTP/记录同步              | 同步 50/50         |
| W18     | 02/22–02/28 | M7  | 功耗/深睡/整机/soak                 | E7/E8            |
| W19     | 03/01–03/07 | M8  | E1–E4 复跑                      | 数据冻结             |
| W20     | 03/08–03/14 | M8  | E5–E6、图表、论文 1–3 章             | 对照表              |
| W21     | 03/15–03/21 | M8  | 论文 4–6 章                      | 初稿前半             |
| W22     | 03/22–03/28 | M8  | 论文 7–8 章、演示视频                 | 初稿后半             |
| W23     | 03/29–04/04 | M8  | 全文整合、查重、格式                    | 完整初稿             |
| W24     | 04/05–04/11 | M8  | 导师审阅、修改、答辩材料                  | 审阅通过             |
| W25–W26 | 04/12–04/25 | 缓冲  | 补实验、答辩演练、最终提交                 | 答辩               |


> 缓冲周用于吸收春节、硬件返修、招募延迟。若 M4/M5 延期，优先压缩 M6 优化和 E6 消融，不压缩 E5/E7（论文核心）。

---

## 11. 本周立即执行（M0 前 10 项）

1. **冻结基线**：`git tag v0.1-display`；开分支 `feat/m0-skeleton`。
2. **写决策记录**：`docs/decisions.md`，把 D1–D12 的“选择/理由/备选”落下来，找导师确认 D5/D9。
3. **改分区**：`partitions.csv` 加 `storage`（littlefs，10 M）、nvs 扩到 0x6000；`idf.py build` 验证。
4. **接 LittleFS**：`idf_component.yml` 加 `joltwallet/esp_littlefs`；挂载 + 64 KB 读写 + 10 次断电测试；失败则 2 h 内切 FATFS。
5. **按键 bring-up**：`board_pins.h` + `buttons.c`；5 键（1/4/5/6/13）消抖/长按；串口打印。
6. **重构入口**：`Kconfig` 加 `APP_MODE_DEMO`；`main.c` 拆成 `app_main.c` + `app_state.c`；demo 仍可编译运行。
7. **加 bench**：USB-Serial-JTAG 控制台；`bench info/fs/display band/full/clean`。
8. **跑 E0**：y=35/95/170/230 × h=40/70/100；记录耗时/残影/带外；确定最终 band 布局。
9. **省 RAM**：E0 用现有 15000 B 缓冲跑完；最终布局确定后再把 `g_part_tmp` 按最大 band 改为 3500 B（70 行）或动态分配；回归 demo 346 ms。
10. **训练环境**：运行 `pwsh -File tools/setup/install-train.ps1` 并执行 `verify-toolchain.ps1`；同时起草 `data/words.csv` 前 50 词。

**本周命令**：

```powershell
# 固件
$env:IDF_PATH='F:\esp\v6.1\esp-idf'
. 'F:\esp\v6.1\esp-idf\export.ps1'
idf.py build
idf.py -p COM4 flash monitor

# 训练环境（与 IDF 的 Python 3.14 环境隔离；短路径 + junction，见 docs/toolchain.md）
pwsh -File tools\setup\install-train.ps1
pwsh -File tools\setup\verify-toolchain.ps1
.\.venv-train\Scripts\python.exe tools\train\smoke_convert.py
```

**本周结束时应能看到**：新的分区表、LittleFS 挂载日志、5 个按键的键值、`bench display band 95 70 10` 的耗时表、E0 的位置/高度结论、demo 仍 346 ms。

---

## 12. 附录

### 12.1 词库源数据格式（`data/words.csv`）

```csv
id,word,phonetic,meaning,example,audio,level
1,apple,/ˈæp.əl/,苹果,I ate an apple.,apple.wav,0
2,think,/θɪŋk/,想；认为,I think it is right.,think.wav,0
...
```

**选词原则**：

- CEFR A1–A2，1–3 音节，常见名词/动词/形容词。
- 覆盖中国学习者难音：/θ/ <u>ð</u> <u>ʃ</u> <u>ʒ</u> <u>ŋ</u> <u>r</u> <u>l</u> <u>v</u> /w/；至少 10 组最小对立体（think/sink、ship/sheep、light/right、rice/lice、west/vest 等）。
- 100 词中 50 个核心词用于数据集；另选 10 个不在训练集的词，采 4 人×2 条做“词泛化”测试，不参与训练。
- 释义中文 ≤12 字；例句 ≤60 字符；音标只使用字库覆盖的 IPA 字符。



### 12.2 数据集目录与命名

```text
data/dataset/v1.0/
├── audio/
│   ├── s01_w012_c_01.wav     # speaker, word, c=correct/i=incorrect, take
│   └── ...
├── labels.csv                # utt_id,speaker_id,gender,word_id,label,rating,...
├── splits.json               # LOSO 折；train/val/test 的 utt_id
├── stats.json                # 人数/词数/标签/时长/SNR
├── README.md                 # 采集条件、标注规范、隐私说明
└── SHA256SUMS
```

**隐私**：只保留匿名 speaker_id；不收集姓名/学号；公开数据集前征得同意；论文中不放可识别个人的音频。

### 12.3 论文结构映射

| 章节         | 内容                            | 主要素材/实验         |
| ---------- | ----------------------------- | --------------- |
| 1 绪论       | 背景、现状、意义、贡献                   | 文献、README/技术方案  |
| 2 需求与总体方案  | 功能/非功能、系统框图、指标                | 第 2/3 节         |
| 3 硬件设计     | ESP32-S3、EPD、音频、电源、引脚         | 3.1/3.2、BOM、接线图 |
| 4 显示子系统与局刷 | SSD1619A、LUT、MUX/gate、band、功耗 | E0/E7、现有局刷记录    |
| 5 音频与模型    | 采集、VAD、log-Mel、CNN、量化、TFLM    | E1–E3、E6        |
| 6 规则法对照    | YIN、DTW、特征、阈值                 | E4              |
| 7 软件实现     | 状态机、Leitner、UI、记录、同步、低功耗      | F1–F12、E8       |
| 8 实验与结果    | 两法对比、功耗、耗时、可靠性                | E5/E7/E8        |
| 9 总结与展望    | 局限、改进                         | 风险/讨论           |

### 12.4 论文中必须如实写的限制

1. 4.2" SSD1619A 只能做 **70 行带状 Y 局部扫描**，不是任意 2D 窗口；X 仍全宽。
2. 数据集规模有限（600–800，另 80 条词泛化），说话人独立结果方差大；报告 CI，不夸大。
3. “正确/错误”是**跟读可接受度**的粗粒度二分类，不是音素级诊断，也不作语言能力评定。
4. 深睡电流受开发板（**PWR LED 常亮**、CH343P、LDO）限制；若 >100 µA，报告板级值并说明；必要时拆 PWR LED 或自制定板。
5. AGPL LUT 的来源与许可；发布方式的选择。
6. 规则法依赖参考发音和阈值；对性别/口音/八度错误敏感。
7. 模型可能受录音设备/环境影响；部署时使用同一麦克风和固定距离。
8. 模型只在 50 个核心词上训练；扩展词/未登录词的模型输出需经词泛化验证，未验证前界面只给规则法结果或明确标注“未验证”。

### 12.5 待导师确认的问题

1. 词库最终 50 还是 100？（计划 100，数据集用 50 核心词）
2. 是否必须用 STM32Cube.AI？若强制，需重新评估平台迁移成本（当前已选 ESP32-S3 + TFLM）。
3. “局部刷新”是否接受“70 行带状真局刷”的表述？（现有实测支持，但不能写任意 2D 窗口）
4. 数据集规模/标注工作量是否接受 600–800 条 + 80 条词泛化、2 人标注？是否可以用“指定错误类型”补足错误类？
5. 是否要求触摸？计划 5 键，触摸作为可选。
6. 论文格式、篇幅、查重、提交时间节点。
7. AGPL LUT 的处理方式：论文注明即可，还是必须重写/整项目 AGPL？

### 12.6 相关文档索引

- [`README.md`](README.md)：构建、接线、驱动 API、帧格式。
- [`docs/toolchain.md`](docs/toolchain.md)：本机已装工具链、版本、验证脚本、模型转换注意事项。
- [`docs/board_pinmap.md`](docs/board_pinmap.md)：YD-ESP32-S3 / N16R8 的 P1/P2 排针、保留脚、strapping 与接线。
- [`docs/交接文档.md`](docs/交接文档.md)：现状、踩坑、下一步、功耗 TODO。
- [`docs/技术方案.md`](docs/技术方案.md)：需求、架构、BOM、模型/规则法方案（本计划在其基础上细化并修正深睡策略）。
- [`docs/局刷测试记录.md`](docs/局刷测试记录.md)：全部刷新实验、2.8 无闪机理、2.9 带状真局刷。
- [`docs/真局部研究.md`](docs/真局部研究.md)：扫描级/像素级局部的边界与 datasheet 依据。
- [`docs/HINK-E042A13-A0资料汇总.md`](docs/HINK-E042A13-A0资料汇总.md)：屏体资料。
- [`components/epd_ssd1619a/include/epd_ssd1619a.h`](components/epd_ssd1619a/include/epd_ssd1619a.h)：驱动 API。
- [`main/word_view.c`](main/word_view.c)：现有布局/绘制参考。

---

> **最后一句**：这份计划的核心不是“把功能列全”，而是**先用 M0–M2 保证设备真的能学、能录、能显示，再用 M3–M8 保证论文真的有数据、有对照、有结论**。任何阶段如果时间不够，优先保“闭环 + E5 对照 + E7 功耗”，砍掉触摸、OTA、外壳和可选消融。
