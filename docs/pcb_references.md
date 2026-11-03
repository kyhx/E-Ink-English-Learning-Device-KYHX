# 原理图 / PCB 参考项目

> 更新：2026-11-03  
> 用途：为 ESP32-S3 墨水屏学习机找可借鉴的**原理图、PCB、电源/EPD/音频**设计。  
> 注意：这些是参考设计，不是本项目原理图；EPD 排线、面板型号、电源轨必须按自己的物料核对。

## 1. 官方设计规范（优先看）

| 项目 | 地址 | 可借鉴 |
| --- | --- | --- |
| ESP32-S3 硬件设计指南 | <https://documentation.espressif.com/esp-hardware-design-guidelines/en/latest/esp32s3/esp-hardware-design-guidelines-en-master-esp32s3.pdf> | 天线净空、电源去耦、USB、strapping、晶振、RF 布局 |
| ESP32-S3-DevKitC-1 文档 | <https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/index.html> | 官方开发板原理图/PCB 结构、USB-UART、按键、电源 |
| ESP32-S3 开发板全套文档 PDF | <https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp-dev-kits-en-master-esp32s3.pdf> | 含 DevKitC-1 等板卡原理图，适合对照网络 |
| ESP32-S3-Korvo-2 音频板 | <https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/user-guide-esp32-s3-korvo-2-v3.0.html> | 麦克风阵列、音频 codec、功放、I2S 布局 |

## 2. 墨水屏 + 电池整机参考

### Inkplate 4 TEMPERA（3.8"）

- 仓库：<https://github.com/SolderedElectronics/Soldered-Inkplate-4-TEMPERA-with-glass-panel-hardware-design>
- 内容：KiCad 原理图/PCB、BOM、Gerber、3D STEP、电源/EPD 分页。
- 适合借鉴：
  - 单节锂电 + 充电 + 升压 + EPD 多路电源；
  - EPD 连接器与 FPC 固定；
  - 低功耗整机结构。
- 注意：3.8" 600×600，不是本项目 4.2" 400×300；EPD 连接器针序不能照抄。

### Inkplate 6（6"）

- 仓库：<https://github.com/SolderedElectronics/Soldered-Inkplate-6-hardware-design>
- 内容：完整 KiCad、原理图 PDF、Gerber、3D、BOM。
- 适合借鉴：大屏 EPD 电源、电池充电、整机地平面/屏蔽。
- 注意：6" 800×600，EPD 驱动时序和本项目不同。

### LILYGO T5S3 4.7 e-paper PRO

- 仓库：<https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO>
- 硬件目录：<https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO/tree/H752-01/hardware>
- 内容：ESP32-S3 + 4.7" EPD + 电池 + 充电 + EPD PMIC（TPS65185）原理图。
- 适合借鉴：**ESP32-S3 + 墨水屏 + 锂电** 的一体化架构、天线位置、USB、电池仓。
- 注意：4.7" 屏、分辨率/排线不同；TPS65185 方案比本项目复杂。

### M5Paper v1.1

- 原理图 PDF：<https://docs.rs-online.com/09d1/A700000012839932.pdf>
- 适合借鉴：4.7" 墨水屏、ESP32、锂电、充电、紧凑结构。
- 注意：M5Paper 是整机产品，原理图只作参考；音频部分很弱。

### epdiy

- 仓库：<https://github.com/vroland/epdiy>
- 适合借鉴：高分辨率 EPD 的 TPS65185 多路电源、升压/负压/时序、ESP32 并行驱动。
- 注意：它是 EPD 驱动板，不是完整学习机；不适合直接当 4.2" 单色屏方案。

### Waveshare E-Paper ESP32 Driver Board

- Wiki：<https://www.waveshare.com/wiki/E-Paper_ESP32_Driver_Board>
- 用户手册 PDF：<http://files.waveshare.com/upload/4/4a/E-Paper_ESP32_Driver_Board_user_manual_en.pdf>
- 适合借鉴：4.2" EPD 排线、SPI 接口、驱动板电源。
- 注意：这是驱动板，不含电池/音频；面板针序以你手上屏为准。

## 3. 音频 / 电源电路参考

| 项目 | 地址 | 可借鉴 |
| --- | --- | --- |
| Adafruit MAX98357A | <https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp/downloads> | I2S 功放外围、滤波、喇叭接口、增益设置 |
| Adafruit BQ24074 | <https://learn.adafruit.com/adafruit-bq24074-universal-usb-dc-solar-charger-breakout/design-notes> | **带电源路径**的锂电充电、负载共享、USB/电池切换 |
| Adafruit INA219 | <https://learn.adafruit.com/adafruit-ina219-current-sensor-breakout/downloads> | 0.1Ω shunt、I²C 上拉、电流/电压测量 |
| TI BQ24074 | <https://www.ti.com/lit/ds/symlink/bq24074.pdf> | 充电/电源路径芯片级设计 |
| TI TPS63020 | <https://www.ti.com/lit/ds/symlink/tps63020.pdf> | 3.3V 升降压、电感/电容布局 |
| Aerosemi MT3608 | <https://www.olimex.com/Products/Breadboarding/BB-PWR-3608/resources/MT3608.pdf> | 5V 升压、功放供电 |

## 4. 建议的借鉴组合

不要只抄一个项目，按模块拆：

```text
MCU/USB/天线     -> ESP32-S3-DevKitC-1 + Espressif 硬件设计指南
EPD 连接器/电源  -> Waveshare 4.2 驱动板 + Inkplate 4/6 的 EPD 电源页
ESP32-S3+EPD+电池 -> LILYGO T5S3 4.7 PRO
紧凑电池/结构    -> M5Paper / Inkplate 4
音频             -> ESP32-S3-Korvo-2 + Adafruit MAX98357A
充电/电源路径    -> Adafruit BQ24074 + TI BQ24074
电流检测         -> Adafruit INA219
```

## 5. 不能直接照抄的地方

1. **EPD 排线针序**：不同面板/连接器完全不同，必须对照屏体规格书和 FPC。
2. **EPD 电源**：4.2" 单色屏通常只需 3.3V 逻辑 + 面板驱动板；大屏/高分辨率才需要 TPS65185 多路电源。
3. **天线**：不要抄模块位置；按 ESP32-S3-WROOM-1 的 keepout 和外壳重新仿真/实测。
4. **音频**：MAX98357A 是数字功放，别把模拟 codec 的差分输入电路照搬。
5. **充电**：TP4056 没有电源路径；要边充边用，选 BQ24074/IP5306 或加理想二极管。
6. **许可证**：Inkplate/LILYGO 等硬件开源许可证不同，参考/复用前看仓库 LICENSE，论文中注明来源。

## 6. 推荐阅读顺序

```text
1. Espressif 硬件设计指南
2. ESP32-S3-DevKitC-1 原理图
3. Waveshare 4.2 EPD 驱动板手册
4. LILYGO T5S3 4.7 PRO 原理图
5. Inkplate 4 电源/EPD 分页
6. Korvo-2 + Adafruit MAX98357A
7. BQ24074 + INA219
```
