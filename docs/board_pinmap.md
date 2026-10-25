# YD-ESP32-S3 / ESP32-S3-N16R8 开发板引脚与接线

> 依据：`C:\Users\Administrator\Downloads\ESP32-S3-N16R8_User_Guide.pdf`（MICRO ROBOTICS，11 页）
>
> 板卡：ESP32-S3-WROOM-1，16 MB Flash + 8 MB Octal PSRAM，双 Type-C。
>
> 结论：**当前 M0 的引脚分配全部落在这块板的 P1/P2 排针上**；GPIO13/14 在 P1 的 19/20 脚，不需要改线。

---

## 1. 板卡要点

| 项 | 说明 |
| --- | --- |
| 模组 | ESP32-S3-WROOM-1，N16R8（16 MB Flash / 8 MB PSRAM） |
| USB-C "COM" | CH343P USB-UART；**烧录/串口监视用这个口**（COM4） |
| USB-C "USB" | ESP32-S3 原生 USB（GPIO19/20）；可做 USB-OTG/JTAG，需桥接 `USB-OTG` 焊盘 |
| RGB LED | WS2812，GPIO48；需桥接 `RGB` 焊盘才由 GPIO48 控制 |
| BOOT 键 | GPIO0；上电后可当普通输入 |
| RST 键 | 硬件复位 EN |
| PWR LED | 上电常亮，不可控 |
| TX/RX LED | 分别接 GPIO43/44（UART0），串口通信时闪烁 |
| LDO | 5 V → 3.3 V，1 A |
| 供电 | USB-C "COM"/"USB"，或 5V/G、3V3/G 排针 |
| 5V 输入/输出 | `IN-OUT` 焊盘：未桥接=输入；桥接=输出（用 COM 口供电时给 5V 排针供电） |

> **不要用 GPIO35/36/37**：手册明确它们用于模组内部 Flash/PSRAM 通信，外部不可用。
>
> **GPIO19/20** 是原生 USB D-/D+；**GPIO43/44** 是 UART0；**GPIO0** 是 BOOT；**GPIO45/46** 是 strapping；**GPIO48** 是 RGB LED。这些都不要分配给业务外设（除非明确知道后果）。

---

## 2. P1 排针（22 pin）

| P1 | 名称 | 功能 / 本项目 |
| --- | --- | --- |
| 1 | 3V3 | 3.3 V 电源 |
| 2 | 3V3 | 3.3 V 电源 |
| 3 | RST | EN 复位 |
| 4 | GPIO4 | **KEY_PREV**（RTC，上拉） |
| 5 | GPIO5 | **KEY_NEXT**（RTC，上拉） |
| 6 | GPIO6 | **KEY_PLAY**（RTC，上拉） |
| 7 | GPIO7 | **EPD BUSY** |
| 8 | GPIO15 | **MIC WS**（I2S0） |
| 9 | GPIO16 | **MIC SD**（I2S0） |
| 10 | GPIO17 | **AMP BCLK**（I2S1） |
| 11 | GPIO18 | **AMP LRC**（I2S1） |
| 12 | GPIO8 | **EPD RST** |
| 13 | GPIO3 | 空闲（RTC/ADC1_CH2/strapping；可作第 6 键，勿上电按住） |
| 14 | GPIO46 | 空闲但 **strapping**，不建议用 |
| 15 | GPIO9 | **EPD DC** |
| 16 | GPIO10 | **EPD CS** |
| 17 | GPIO11 | **EPD MOSI**（ADC2，勿做 ADC） |
| 18 | GPIO12 | **EPD SCK**（ADC2，勿做 ADC） |
| 19 | GPIO13 | **KEY_RECORD**（RTC；ADC2，仅数字） |
| 20 | GPIO14 | **MIC BCLK**（I2S0；ADC2，仅数字） |
| 21 | 5V | 5 V 电源 |
| 22 | G | GND |

> GPIO13/14 是 ADC2 通道；本项目只做数字输入/输出，不接 ADC，因此不受 Wi-Fi/ADC2 限制。

---

## 3. P2 排针（22 pin）

| P2 | 名称 | 功能 / 本项目 |
| --- | --- | --- |
| 1 | G | GND |
| 2 | GPIO43 | UART0 TX（CH343P，控制台；保留） |
| 3 | GPIO44 | UART0 RX（CH343P，控制台；保留） |
| 4 | GPIO1 | **KEY_SYNC**（RTC，上拉） |
| 5 | GPIO2 | **BAT_ADC**（ADC1_CH1，1M/1M 分压或 MOS 门控） |
| 6 | GPIO42 | 空闲；JTAG MTMS（不用外置 JTAG 时可用） |
| 7 | GPIO41 | 空闲；JTAG MTDI |
| 8 | GPIO40 | **I2C SCL**（INA219） |
| 9 | GPIO39 | **I2C SDA**（INA219） |
| 10 | GPIO38 | 空闲；可作 MIC BCLK 的备选 |
| 11 | GPIO37 | **保留**：Flash/PSRAM |
| 12 | GPIO36 | **保留**：Flash/PSRAM |
| 13 | GPIO35 | **保留**：Flash/PSRAM |
| 14 | GPIO0 | BOOT 键（保留） |
| 15 | GPIO45 | **strapping**，不用 |
| 16 | GPIO48 | RGB LED（需桥接；保留） |
| 17 | GPIO47 | **AMP SD**（SPICLK_P/SUBSPICLK_P_DIFF 备选；当前作输出） |
| 18 | GPIO21 | **AMP DIN**（I2S1） |
| 19 | GPIO20 | 原生 USB D+（保留） |
| 20 | GPIO19 | 原生 USB D-（保留） |
| 21 | G | GND |
| 22 | G | GND |

> GPIO39–42 同时是外置 JTAG 引脚；本项目用 CH343P 串口下载/监视，不用外置 JTAG，因此 39/40 可作 I2C。若以后接 JTAG，需要改 I2C 引脚（例如 41/42 或 38/47 组合）。
>
> GPIO47 手册标为 `SPICLK_P / SUBSPICLK_P_DIFF`，但 N16R8 的内部 Flash/PSRAM 只用 35/36/37；当前把 47 当 AMP SD 输出。若换用会占用 47 的 PSRAM 配置，把 AMP SD 改到 48（桥接 RGB 后）或其他空闲脚。

---

## 4. 当前项目引脚汇总

| 功能 | GPIO | 位置 | 备注 |
| --- | --- | --- | --- |
| EPD BUSY | 7 | P1-7 | 高=忙 |
| EPD RST | 8 | P1-12 | 低有效 |
| EPD DC | 9 | P1-15 | 低=命令 |
| EPD CS | 10 | P1-16 | 软件片选 |
| EPD MOSI | 11 | P1-17 | SPI2 |
| EPD SCK | 12 | P1-18 | SPI2，4 MHz |
| KEY_PREV | 4 | P1-4 | 上拉，RTC |
| KEY_NEXT | 5 | P1-5 | 上拉，RTC |
| KEY_PLAY | 6 | P1-6 | 上拉，RTC |
| KEY_RECORD | 13 | P1-19 | 上拉，RTC；ADC2 仅数字 |
| KEY_SYNC | 1 | P2-4 | 上拉，RTC |
| BAT_ADC | 2 | P2-5 | ADC1_CH1 |
| MIC BCLK | 14 | P1-20 | I2S0；ADC2 仅数字 |
| MIC WS | 15 | P1-8 | I2S0 |
| MIC SD | 16 | P1-9 | I2S0；INMP441 L/R=GND |
| AMP BCLK | 17 | P1-10 | I2S1 |
| AMP LRC | 18 | P1-11 | I2S1 |
| AMP DIN | 21 | P2-18 | I2S1 |
| AMP SD | 47 | P2-17 | 空闲拉低 |
| INA219 SDA | 39 | P2-9 | 与外置 JTAG 冲突 |
| INA219 SCL | 40 | P2-8 | 与外置 JTAG 冲突 |

5 个按键（1/4/5/6/13）都在 RTC GPIO 0–21 范围内，因此都能作为 EXT1 深睡唤醒源。

---

## 5. 供电与接线注意

- **烧录/串口**：用 "COM" Type-C 口（CH343P），`idf.py -p COM4 flash monitor`。
- **原生 USB**："USB" 口是 GPIO19/20；如果要用 USB-OTG/JTAG，需要桥接板背面的 `USB-OTG` 焊盘。本项目暂不需要。
- **5V 排针**：默认是输入；如果要用 COM 口供电同时在 5V 排针输出，需要桥接 `IN-OUT` 焊盘。
- **3V3**：LDO 1 A；EPD 刷新和音频功放瞬态电流较大，建议 3V3 轨加 470 µF 以上 bulk 电容。
- **RGB LED**：GPIO48 需要桥接 `RGB` 焊盘才受控；不桥接时不要把它当普通 GPIO 用。
- **strapping**：GPIO0/3/45/46。上电/复位时不要按住接在这些脚上的按键；本项目按键在 1/4/5/6/13，不在 strapping 脚上（GPIO3 没有使用）。
- **ADC**：电池用 GPIO2（ADC1_CH1）；GPIO11/12/13/14 属 ADC2，不要在这些脚上接模拟量。
- **EPD 驱动板**：仍按原接线 `GND/3V3/SCK/SDA/RST/DC/CS1/BUSY` 接 7–12；CS2 悬空。

### 低功耗特别注意

- **PWR LED 常亮且不可控**：它直接跨在电源上，可能吃 1–2 mA。要测 <100 µA 的深睡电流，必须把它拆掉/断开，或明确报告“整板含 PWR LED”的电流。
- **CH343P**：只要 5V/3V3 有电就可能有一定静态电流；用电池供电、USB 断开时也要把它算进去。
- **TX/RX LED**：接在 GPIO43/44，UART 空闲时通常熄灭；若常亮，检查串口电平。
- 因此本板更适合验证“功能/刷新/推理”，**<100 µA 的整机指标建议用自制定板或拆掉 PWR LED/CH343P 后测量**，论文里要写清楚测量边界。

---

## 6. 与通用 DevKitC-1 的差异

| 项 | 本板 YD-ESP32-S3 | 常见 DevKitC-1 |
| --- | --- | --- |
| USB | 双 Type-C：CH343P + 原生 USB | 通常双口或单口 |
| RGB LED | GPIO48，需桥接 | GPIO38/48 视版本 |
| GPIO13/14 | **P1 19/20 引出** | 通常引出 |
| GPIO35/36/37 | 排针有，但被 Flash/PSRAM 占用 | 同样保留 |
| TX/RX LED | GPIO43/44 | 视板子 |

> 结论：之前按通用 S3 开发板做的 7–12（EPD）、1/4/5/6/13（按键）、14/15/16（MIC）、17/18/21/47（AMP）、39/40（I2C）、2（电池）**全部可用**，不需要飞线或改脚。
