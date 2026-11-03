# 芯片手册库

按功能分类存放本项目硬件设计需要的芯片/模组手册。下载时间：**2026-11-03**。
所有文件均检查过 PDF 头/页数；SHA256 见文末。

## 目录

```text
docs/datasheets/
├── 01-mcu/            # 主控/模组/参考手册
├── 02-display/        # 墨水屏控制器/屏体
├── 03-audio/          # 麦克风/功放
├── 04-usb/            # USB-UART
├── 05-power/          # 充电/升降压/电流检测
└── 06-protection/     # 锂电池保护
```

## 01 MCU

| 文件 | 内容 | 来源 |
| --- | --- | --- |
| [`esp32-s3_datasheet.pdf`](01-mcu/esp32-s3_datasheet.pdf) | ESP32-S3 芯片手册，87 页 | [Espressif](https://documentation.espressif.com/esp32-s3_datasheet_en.pdf) |
| [`esp32-s3-wroom-1_datasheet.pdf`](01-mcu/esp32-s3-wroom-1_datasheet.pdf) | ESP32-S3-WROOM-1/1U 模组，53 页 | [Espressif](https://documentation.espressif.com/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf) |
| [`esp32-s3_technical_reference_manual.pdf`](01-mcu/esp32-s3_technical_reference_manual.pdf) | ESP32-S3 TRM，1531 页 | [Espressif](https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf) |

## 02 Display

| 文件 | 内容 | 来源 |
| --- | --- | --- |
| [`SSD1619A_spec.pdf`](02-display/SSD1619A_spec.pdf) | SSD1619A 控制器，48 页 | 原有本地资料 |
| [`GDEH042Z96_spec.pdf`](02-display/GDEH042Z96_spec.pdf) | GDEH042Z96 4.2" 屏体规格 | 原有本地资料 |

> HINK-E042A13-A0 的厂商原始规格书暂未拿到；现有资料见
> [`../HINK-E042A13-A0资料汇总.md`](../HINK-E042A13-A0资料汇总.md)。

## 03 Audio

| 文件 | 内容 | 来源 |
| --- | --- | --- |
| [`max98357a_datasheet.pdf`](03-audio/max98357a_datasheet.pdf) | MAX98357A/98357B I2S D 类功放，38 页 | [Analog Devices](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf) |
| [`msm261s4030h0r_datasheet.pdf`](03-audio/msm261s4030h0r_datasheet.pdf) | MS4030 模块实际芯片 MSM261S4030H0R，I2S MEMS 麦克风，12 页 | [Core Electronics 镜像](https://core-electronics.com.au/attachments/localcontent/ce307ae54346de48b38fe2a3bf730aba_16949206143.pdf) |
| [`inmp441_datasheet.pdf`](03-audio/inmp441_datasheet.pdf) | INMP441 参考手册，21 页 | [datasheet4u 镜像](https://datasheet4u.com/pdf/1350167/INMP441.pdf) |

> MS4030 是模块型号，板上芯片为 **MSM261S4030H0R**，与 INMP441 引脚/时序兼容；两份都保留作对照。

## 04 USB

| 文件 | 内容 | 来源 |
| --- | --- | --- |
| [`ch343_datasheet.pdf`](04-usb/ch343_datasheet.pdf) | CH343 USB 转高速串口，v2.0，6 页 | [Waveshare 镜像](https://files.waveshare.com/wiki/ESP32-S3-LCD-1.3/CH343DS1_EN.pdf)；官方页 [WCH](https://www.wch-ic.com/downloads/CH343DS1.PDF.html) |

## 05 Power

| 文件 | 内容 | 来源 |
| --- | --- | --- |
| [`tp4056_datasheet.pdf`](05-power/tp4056_datasheet.pdf) | TP4056 1A 线性锂电充电，3 页 | [SparkFun/CDN 镜像](https://dlnmh9ip6v2uc.cloudfront.net/datasheets/Prototyping/TP4056.pdf) |
| [`bq24074_datasheet.pdf`](05-power/bq24074_datasheet.pdf) | BQ24074 带电源路径的锂电充电，10 页 | [TI](https://www.ti.com/lit/ds/symlink/bq24074.pdf) |
| [`tps63020_datasheet.pdf`](05-power/tps63020_datasheet.pdf) | TPS63020 升降压，34 页 | [TI](https://www.ti.com/lit/ds/symlink/tps63020.pdf) |
| [`mt3608_datasheet.pdf`](05-power/mt3608_datasheet.pdf) | MT3608 2A 升压，7 页 | [Olimex 镜像](https://www.olimex.com/Products/Breadboarding/BB-PWR-3608/resources/MT3608.pdf) |
| [`ina219_datasheet.pdf`](05-power/ina219_datasheet.pdf) | INA219 电流/电压监测，38 页 | [TI](https://www.ti.com/lit/ds/symlink/ina219.pdf) |

> TP4056 没有负载共享/电源路径；电池方案建议用 **BQ24074**（带电源路径）或 IP5306，TP4056 只适合做简单充电。

## 06 Protection

| 文件 | 内容 | 来源 |
| --- | --- | --- |
| [`dw01a_datasheet.pdf`](06-protection/dw01a_datasheet.pdf) | DW01A 单节锂电保护，8 页 | [Unikeyic 镜像](https://www.unikeyic.com/media/datasheet/c2/73/0c0a/c2730c0ae725d5ba56eee6c1beafffd5.pdf) |
| [`fs8205a_datasheet.pdf`](06-protection/fs8205a_datasheet.pdf) | FS8205A 双 N-MOS，6 页 | [Fortune](https://www.ic-fortune.com/upload/Download/FS8205A-DS-17_EN.pdf) |

## 还没收录

这些不是“芯片手册”，需要按最终选型再补：

- HINK-E042A13-A0 原厂屏体规格书；
- USB-C 连接器、EPD FPC/连接器的机械图；
- 具体电芯的规格书/MSDS/UN38.3；
- 喇叭、电池保护板、电感/电容等无源件规格；
- 若改用 BQ24074/IP5306/MAX17048，再补对应手册。

## SHA256

```text
27d71971da07c280c6068d08c74720d1a25b8f20cf8494dc1765bdd28d40d435  01-mcu/esp32-s3-wroom-1_datasheet.pdf
2d5a7cb7fd559d8d972bd88db32669c0196d23f22d7afaafb0f63d099b589a3f  01-mcu/esp32-s3_datasheet.pdf
4484bf8a69035ec42a731c58c64ada6fbd1f1618c5559409f134d9ea083f444f  01-mcu/esp32-s3_technical_reference_manual.pdf
3b73103d4022bfd12ee35478d44b285e347e7ad5152eadbf9e943f22e8b648c4  02-display/GDEH042Z96_spec.pdf
6159529d30dd3700d623536d649077ffb978373d17b9d0b8e48ee30cfd71fa6b  02-display/SSD1619A_spec.pdf
b09f1e902210135477966899638b2217e12116e3b70783fef907ee5398252af4  03-audio/inmp441_datasheet.pdf
dea228ad619901d34c2f5f106243bdb9b9e95e38989d4d66fd6c6c489bfa6f1c  03-audio/max98357a_datasheet.pdf
bd2a5507eea390942ea60707b418bcfddb3ff41e561ed2e69c63a80ca2390cd5  03-audio/msm261s4030h0r_datasheet.pdf
d1dba2658f17b842a14a1911e65ce1a19fa8d1fdb8f777183e4cde4febb34865  04-usb/ch343_datasheet.pdf
58004eda854d07478e6fc6f4398c114f60a3bcf18d4877471c7c1a306d1fa1cb  05-power/ina219_datasheet.pdf
fe848b6e4af5cf5a8245b98a14d677f923e47799b47fb83e0d1d02dc817ddede  05-power/mt3608_datasheet.pdf
cdafa22618e7221fa93b8d432237cafb246a515606eef2672f50230d5ae205ce  05-power/tp4056_datasheet.pdf
4fe4e4f9855501f83b400c6932e3098439e51c50539e40dc45b44a8d509e8db1  05-power/bq24074_datasheet.pdf
d117773bb7370fd79377bc70ce987eec3469ea5979547a09df52fca5f95b2e69  05-power/tps63020_datasheet.pdf
750b7878c9bcd0bb5273c0e3a912e235cc4fb20d4add81309f10c8677db1e3cd  06-protection/dw01a_datasheet.pdf
520c98ca46f8fd7e216c3b80b09e9c8487f72d27baa17752419ecd9f784be564  06-protection/fs8205a_datasheet.pdf
```
