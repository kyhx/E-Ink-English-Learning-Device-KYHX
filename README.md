# E-Ink 英语学习机（ESP32-S3 + 4.2" 墨水屏）

基于 **YD-ESP32-S3 / ESP32-S3-N16R8 + 墨水屏驱动板 V1.5 + 4.2 寸单色电子墨水屏（SSD1619A，400×300）** 的
电子墨水屏英语单词学习终端固件，使用 **ESP-IDF v6.1 / C** 开发。开发板为双 Type-C
（COM=CH343P 串口，USB=原生），板载 16 MB Flash / 8 MB Octal PSRAM；完整引脚见
[`docs/board_pinmap.md`](docs/board_pinmap.md)。

> 文档导航：[`docs/README.md`](docs/README.md)（硬件/数据/模型/论文/动画索引）。

> **项目基调（2026-11-03）**：**主要目的就业；底线是毕设完整交付**。
> 技术定位是**端侧 AI / TinyML 工程实践**，不是算法理论创新。
> 执行总纲见 [`毕设基调.md`](毕设基调.md)，取舍摘要见 [`docs/项目定位.md`](docs/项目定位.md)。

纸质单词书内容固定、检索不便，难以按遗忘规律安排复习，携带与翻阅成本较高；手机背单词 App
虽然检索与调度能力强，但依赖高亮背光屏幕，长时间近距离使用易造成视觉疲劳，且功能高度依赖
网络与云端服务，在教室、通勤、自习等场景下的专注度容易被消息推送打断。
电子墨水屏（E-Paper / E-Ink）具有双稳态特性：仅在刷新时消耗电能，断电后画面保持不变；
反射式成像接近纸张观感，无背光直射，适合长时间阅读。将其用于单词学习终端，
可在功耗与视觉舒适度上同时优于手机方案。

---

## 1. 当前状态

已在本项目的硬件上完成实测（2026-10/11）：

| 项目 | 结果 |
| --- | --- |
| 编译 | ESP-IDF v6.1 + xtensa-esp-elf，零 error |
| 面板初始化 / 全白 / 全黑 / 文字 | 正常 |
| OTP 全刷 | 4–6 s，全屏闪 |
| **70 行带状真局刷** | **约 346 ms/次；带状外不动、横带不闪，只有文字变化** |
| 清洁周期（全白→全黑→底图） | 3 × 709 ms，可完全恢复残影 |
| 上电演示 | 50 个单词，每词 346 ms 带状刷新 + 1 s 停留，第 25 词清洁一次，结束显示 DONE 并深睡 |
| **M0 分区 / LittleFS** | **10 MB `storage` 分区，首次自动格式化；64 KB 读写校验通过** |
| **M0 5 键驱动** | **GPIO 1/4/5/6/13，20 ms 消抖 + 800 ms 长按；初始化通过** |
| **M0 bench 控制台** | **UART0 115200，`bench info/fs/display/keys/selftest`** |
| **M0 局部刷新扩展** | **y=35/95/170/230 均可带状刷新；40 行带约 298 ms，70 行 346 ms** |
| **M2 音频采集（旧台架）** | **MS4030 / I2S0，16 kHz 单声道，2 s 32,000 帧，无削顶** |
| **M2 WAV / VAD** | **WAV 写入 LittleFS；VAD 去首尾静音；安静房间正确判“无语音”** |
| **M2 连续录音** | **20/20 文件完整；PC 可通过 `bench audio dump` 下载 WAV** |
| **M2 回放（旧台架）** | **已能出声但偏小；目标改为 NS4168 + 8Ω 1W** |
| **上电清屏** | **POWERON/EXT/BROWNOUT 后 全白→全黑→底图，约 2.14 s，再进 READY** |
| **设备采集固件** | **5 键采集页：S01–S08 / 50 词 / L0-L1 / T01-T99，WAV+labels.csv 落 LittleFS** |

> **当前方案：MUX=69 + gate start=95，只扫描 95..164 共 70 行；带状内写整条 400px 宽的数据，未变化像素不驱动。**
> 带状外完全不扫描、完全不动；横带不闪，只有单词/序号变化。
> 它不是任意 2D 窗口：X 方向仍全宽，但整宽数据保证横带不闪。详见 [`docs/局刷测试记录.md`](docs/局刷测试记录.md)、[`docs/真局部研究.md`](docs/真局部研究.md)。

### 1.1 M2 音频链（2026-10-26，旧台架）

- 新增 `audio_in / audio_out / audio_wav / audio_vad / audio_analysis`；I2S0 麦克风、I2S1 功放、PSRAM 2 s 缓冲、WAV 44 B 头。
- 采集任务独立于 UI，346 ms 带状刷新不会 overrun；上电先丢弃 100 ms 稳定期，再做 24→16 bit、增益、DC/削顶统计。
- VAD：20 ms 帧/10 ms hop，噪声底 20 分位，首尾留 100 ms，<300 ms 判无语音；加 ZCR 门限避免把电源哼声当语音。
- `bench audio info/rec/play/tone/gen/dump/list/analyze/soak/selftest`。
- 实测：2 s 录音 32,000 帧、无削顶、首个可用样本 121 ms；20/20 连续文件完整；1 kHz 算法自检 1000 Hz/−17.8 dBFS。
- 上电清屏：POWERON/EXT/BROWNOUT 复位后跑全白→全黑→底图，实测 709/710/710 ms，共 2.14 s；`CONFIG_APP_BOOT_CLEAN` 可关。
- 设备采集固件：独立 `artifacts/firmware/E-Ink-collect.bin`，5 键选说话人/词/标签/录音，WAV + `labels.csv` 存 `/storage/collect`；PC 用 `tools/collect/device_collect.py sync --delete` 分批下载。详见 [`tools/collect/README.md`](tools/collect/README.md)。
- **旧台架音量**：MS4030+MAX98357A 已能出声但偏小；目标改为 **LMD2718+NS4168 + 8Ω 1W**，需重新测 SPL/失真。旧数据见 [`docs/M2测试记录.md`](docs/M2测试记录.md)。

### 1.2 M0 已完成（2026-10-25）

- 新分区表：`nvs(24 KB) / phy / factory(3 MB) / storage(10 MB LittleFS)`；首次挂载自动格式化，`bench fs` 64 KB 写入 135–530 ms、读 17–18 ms、校验 OK。
- 5 键驱动：`PREV=4 / NEXT=5 / PLAY=6 / RECORD=13 / SYNC=1`，20 ms 消抖、短按释放触发、800 ms 长按按住触发、16 条事件队列。
- UART0 `bench` 控制台：`info / fs / display band <y0> <h> <n> / display full / display clean / keys [s] / selftest`。
- 默认启动 M0 home 页；旧 50 词演示保留，`CONFIG_APP_MODE_DEMO=y` 可回归。
- 实测：y=35/95/170 的 70 行带均 346 ms；170/40、230/40 约 298 ms；整屏 709 ms；清洁 3×709 ms + LUT reload。详见 [`docs/M0测试记录.md`](docs/M0测试记录.md)。

> 5 键的物理按下/长按事件还需人工按键确认：运行 `bench keys 5` 后依次按 5 个键即可。

---

## 2. 硬件与接线

### 2.1 开发板

本项目使用 **YD-ESP32-S3 / ESP32-S3-N16R8**（ESP32-S3-WROOM-1，16 MB Flash / 8 MB Octal PSRAM，双 Type-C）：

| 项 | 说明 |
| --- | --- |
| USB-C "COM" | CH343P USB-UART，烧录/串口监视用这个口（COM4，115200） |
| USB-C "USB" | ESP32-S3 原生 USB（GPIO19/20）；USB-OTG 需桥接焊盘 |
| RGB LED | WS2812，GPIO48，需桥接 `RGB` 焊盘才受控 |
| BOOT / RST | GPIO0 / EN |
| 供电 | USB-C，或 5V/G、3V3/G 排针；LDO 5V→3.3V 1A |
| 保留引脚 | GPIO35/36/37（Flash/PSRAM）、19/20（USB）、43/44（UART0）、48（RGB） |

完整 P1/P2 排针表、strapping/ADC 注意事项见 [`docs/board_pinmap.md`](docs/board_pinmap.md)。

> 本板 **GPIO13/14 在 P1 的 19/20 脚引出**，所以当前 `RECORD=13`、`MIC BCLK=14` 不需要改线；GPIO13/14 属 ADC2，只做数字 I/O。

### 2.2 墨水屏驱动板

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

### 2.3 音频与选型状态

**当前台架验证（旧音频，仅作历史）：**

| 件 | 型号 | 接线 | 状态 |
| --- | --- | --- | --- |
| 麦克风 | MS4030（INMP441 兼容） | 3.3V；BCLK=14 / WS=15 / SD=16 | 录音正常 |
| 功放 | MAX98357A | 5V；BCLK=17 / LRC=18 / DIN=21 / SD=47 | I2S 正常，音量偏小 |
| 喇叭 | 4Ω 3W | 差分输出 | 已能出声 |

**已确认的目标硬件（2026-11）：**

| 件 | 选型 | 说明 |
| --- | --- | --- |
| MCU | **ESP32-S3-WROOM-1-N16R8** | 16MB Flash / 8MB Octal PSRAM |
| USB-UART | **CH343K** | ESSOP10；DTR/RTS 自动下载能力待核对 |
| 音频 | **LMD2718 + NS4168 成品模块** | 麦克风+功放一体；接口/供电待厂商 pinout 确认 |
| 喇叭 | **8Ω 1W** | NS4168 需 5V 供电才能输出 1W |
| 墨水屏 | **SSD1619A + HINA-E042A13-A0 + 成品驱动板** | 400×300，与现有驱动匹配 |

> 现在固件里的 `MS4030/MAX98357A` 是台架验证代码，不是最终 BOM。
> 最终音频模块的 I2S/模拟接口、SD/GAIN、5V 供电确认后，再改 `audio_in/audio_out` 和引脚表。
> 电源按 **BQ24074 + TPS63020 + 5V/1A Boost** 设计，详见 [`docs/交接文档.md`](docs/交接文档.md)。


---

## 3. 目录结构

```text
E-Ink-English-Learning-Device-KYHX/
├── CMakeLists.txt                  # 工程顶层
├── sdkconfig.defaults              # esp32s3 / 16MB Flash / 8MB Octal PSRAM / 自定义分区
├── sdkconfig                       # 实测通过的完整配置（CPU 240MHz、FreeRTOS 1000Hz）
├── partitions.csv                  # nvs / phy_init / factory(3MB) / storage(10MB littlefs)
├── main/idf_component.yml          # 托管组件：littlefs / TFLM / esp-dsp
├── 毕设基调.md                     # 执行总纲：需求/验收/求职策略
├── plan.md                         # 实施计划与里程碑
├── .clangd                         # clangd 过滤 -f* -m*
├── artifacts/                      # 生成物（gitignore，仅 README 入库）
│   ├── README.md                   # 模型/数据/固件/日志地图
│   ├── models/                     # 训练/量化输出
│   ├── datasets/                   # 公开语料/清单
│   ├── firmware/                   # E-Ink-collect.bin
│   ├── openpronounce/              # PC 参考权重缓存
│   ├── captures/ logs/ reference/
├── build/                          # 默认固件 IDF 构建（gitignore）
├── build-collect/                  # 采集固件 IDF 构建（gitignore）
├── main/
│   ├── CMakeLists.txt
│   ├── main.c                      # app_main：NVS/EPD/LittleFS/按键/bench 初始化
│   ├── Kconfig.projbuild           # APP_MODE_DEMO / APP_BENCH
│   ├── app_state.c/.h              # M2 录音/回放页 + 按键事件循环
│   ├── word_view.c/.h              # 旧 50 词演示布局（demo 模式）
│   ├── board/
│   │   ├── board_pins.h            # 引脚唯一来源
│   │   └── buttons.c/.h            # 5 键消抖/长按
│   ├── collect/                    # 设备采集模式（5 键 + labels.csv）
│   ├── ui/                         # boot_clean 等 UI 公共件
│   ├── services/
│   │   ├── storage.c/.h            # LittleFS 挂载 + 64 KB 自检
│   │   ├── audio.c/.h              # M2 音频初始化
│   │   ├── audio_in.c/.h           # I2S0 麦克风采集任务
│   │   ├── audio_out.c/.h          # I2S1 功放播放
│   │   ├── audio_wav.c/.h          # WAV 读写/文件名
│   │   ├── audio_vad.c/.h          # 能量+ZCR VAD
│   │   └── audio_analysis.c/.h     # 电平/1 kHz/SNR
│   └── bench/
│       └── bench.c/.h              # UART0 bench 控制台
├── components/
│   └── epd_ssd1619a/               # 墨水屏驱动组件
│       ├── CMakeLists.txt
│       ├── epd_ssd1619a.c          # SPI / 初始化 / 刷新 / 绘图
│       ├── epd_font_5x7.h          # 5x7 ASCII 点阵字体（自动生成）
│       └── include/
│           ├── epd_ssd1619a.h      # 对外 API 与引脚配置
│           └── epd_lut_partial.h   # 70 字节 partial LUT（AGPL 来源，见第 8 节）
├── data/
│   ├── README.md                   # 数据目录约定
│   ├── words.csv                   # 50 词 + 中文释义（提交）
│   ├── collected/                  # 自采 WAV + labels.csv（本地，gitignore）
│   └── reference/                  # Oxford 参考发音（本地，gitignore）
├── docs/
│   ├── README.md                   # 文档索引（从这里找）
│   ├── 项目定位.md                 # 就业/毕设双目标与取舍（先读）
│   ├── 交接文档.md                 # 现状 / 踩坑 / 下一步
│   ├── 技术方案.md                 # 驱动方案与寄存器时序细节
│   ├── 局刷测试记录.md             # 全部刷新实验数据与波形分析
│   ├── 真局部研究.md               # 70 行带状真局刷的边界与 datasheet 依据
│   ├── HINK-E042A13-A0资料汇总.md  # 屏体资料汇总
│   ├── M0测试记录.md               # 分区/按键/bench/E0 实测
│   ├── M2测试记录.md               # 录音/WAV/VAD/20 条/回环实测
│   ├── diagrams/                   # 系统框图/电源树/时钟树/设备拓扑
│   ├── board_pinmap.md             # YD-ESP32-S3 P1/P2 引脚与接线
│   ├── data_collection.md          # 采集协议 / 600 条设计 / 标注口径
│   ├── public_datasets.md          # 公开语料调研
│   ├── public_test_speechocean762.md # 公开数据实测结果
│   ├── toolchain.md                # Python/IDF/转换链/字体/串口
│   ├── artifacts.md                # 模型/数据/视频生成物地图
│   ├── thesis/                     # 开题报告正文 + 学校模板
│   ├── animations/                 # 三个视频 + 索引
│   ├── wiring.png / demo_preview.png / font_preview.png
│   └── datasheets/                 # SSD1619A、GDEH042Z96 规格书
└── tools/
    ├── README.md                   # 工具索引
    ├── setup/                      # 训练环境安装 + 全量验证
    ├── train/                      # requirements + onnx→tflite 冒烟
    ├── pipeline/                   # 采集/标注 + 训练/量化/部署 + GUI
    ├── audio/                       # M2 串口音频验收 + WAV 下载
    ├── collect/                     # 设备采集固件构建 + PC 同步
    ├── openpronounce/               # 本地参考评分服务启停
    ├── reference/                  # Oxford 参考发音下载
    ├── fonts/check_fonts.py        # IPA/中文字形覆盖检查
    └── png_to_raw.py / preview_demo.py / gen_font.py 等旧脚本
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

默认启动 **M2 录音页 + UART0 bench 控制台**。旧 50 词演示通过 menuconfig 打开：

```text
idf.py menuconfig
  → E-Ink English Learning Device
      [ ] Run the legacy 50-word display demo at boot
```

烧录后在串口输入：

```text
bench info
bench fs
bench display band 95 70 3
bench display full
bench display clean
bench keys 5
bench audio info
bench audio rec 2000
bench audio play
bench audio soak 20
bench selftest
```

训练 / 模型转换工具链见 [`docs/toolchain.md`](docs/toolchain.md)：
`pwsh -File tools/setup/install-train.ps1`，然后
`.\.venv-train\Scripts\python.exe tools\train\smoke_convert.py`。

完整训练/部署验证管线（音频→log-Mel→CNN→ONNX→TFLite int8→parity→C 数组）：

```powershell
.\.venv-train\Scripts\python.exe tools\pipeline\run.py --toy --epochs 20 --out artifacts/models/pipeline_toy
```

图形界面：

```powershell
.\.venv-train\Scripts\python.exe tools\pipeline\gui.py
```

说明见 [`tools/pipeline/README.md`](tools/pipeline/README.md)。公开数据实测（speechocean762，2000 条）见 [`docs/public_test_speechocean762.md`](docs/public_test_speechocean762.md)。

PC 端音素级参考基线（OpenPronounce，自托管）的研究/部署/与本项目的关系见 [`docs/openpronounce.md`](docs/openpronounce.md)。

一次真实训练回放视频（74 秒，实际 loss/accuracy/混淆矩阵/样本/int8）：

```text
docs/animations/training-run/renders/training-run-explainer.mp4
```

CNN 内部可视化（44 秒，真实激活 + Grad-CAM）：

```text
docs/animations/cnn-visual/renders/cnn-visual-explainer.mp4
```

自采数据有两种方式，协议与 600 条设计见 [`docs/data_collection.md`](docs/data_collection.md)：

1. PC 麦克风：GUI 的 **采集 / 标注** 页；
2. 设备麦克风（推荐，频响与最终部署一致）：烧 `artifacts/firmware/E-Ink-collect.bin`，用 5 键录到 `/storage/collect`，再同步到 PC：

```powershell
. 'F:\esp\v6.1\esp-idf\export.ps1'
pwsh -File tools\collect\build_collect_firmware.ps1
python -m esptool --chip esp32s3 -p COM4 -b 460800 write-flash 0x0 artifacts\firmware\E-Ink-collect.bin
C:\venv-eink\Scripts\python.exe tools\collect\device_collect.py --port COM4 sync --delete
```

采集后检查：

```powershell
.\.venv-train\Scripts\python.exe tools\pipeline\check_collected.py --data-dir data\collected\device\v1.0
```

> 本次分区表新增了 10 MB `storage`；首次启动 LittleFS 会自动格式化。若之前烧过旧分区表且挂载异常，执行一次 `idf.py erase-flash` 再烧录。
>
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

完整第三方清单见 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)（含字体、托管组件、PC 工具链与数据集说明）。

---

## 9. 下一步（规则法保底，TFLM/PCB 做就业信号）

1. **音频模块收尾**：拿到 LMD2718+NS4168 的 pinout，确认 5V 与 I2S，8Ω 1W 测 SPL/失真；
2. **规则法保底**：YIN/DTW + 最小 SoftAP + CSV/JSON 同步，保证毕设有完整闭环；
3. **数据最低线**：200 条设备自采 + 说话人独立 + 标注记录；
4. **TFLM 闭环**：录音→VAD→Mel→int8→结果页，记录延迟/RAM/Flash；
5. **自绘 PCB**：WROOM-1-N16R8 + EPD + 音频模块 + BQ24074/TPS63020/5V Boost；
6. **测试报告**：3.3V/5V 纹波、1W 音频、边充边用、刷新/功耗、休眠；
7. **论文/求职**：两法对照、演示视频、简历、投递；
8. **发布前**：处理 AGPL partial LUT 许可证问题。

> 详细执行总纲与验收清单见 [`毕设基调.md`](毕设基调.md)。
