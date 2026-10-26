# 文档索引

顶层 [`README.md`](../README.md) 是项目入口；`docs/` 放深入说明、实验记录和论文材料。

## 先读

| 文档 | 内容 |
| --- | --- |
| [`交接文档.md`](交接文档.md) | 当前进度、踩坑、下一步（新人先读） |
| [`技术方案.md`](技术方案.md) | 驱动方案、SPI/时序/局刷细节 |
| [`board_pinmap.md`](board_pinmap.md) | YD-ESP32-S3 P1/P2 引脚、音频/屏/按键接线 |
| [`M0测试记录.md`](M0测试记录.md) | M0 分区/LittleFS/按键/bench/E0 实测 |
| [`M2测试记录.md`](M2测试记录.md) | M2 音频链：录音/WAV/VAD/20 条/回环实测 |

## 屏幕与硬件

| 文档 | 内容 |
| --- | --- |
| [`局刷测试记录.md`](局刷测试记录.md) | 全部刷新实验数据与波形分析 |
| [`真局部研究.md`](真局部研究.md) | 70 行带状真局刷的边界与 datasheet 依据 |
| [`HINK-E042A13-A0资料汇总.md`](HINK-E042A13-A0资料汇总.md) | 4.2" 屏体资料汇总 |
| [`datasheets/`](datasheets/) | SSD1619A、GDEH042Z96 规格书 |

## 数据与模型

| 文档 | 内容 |
| --- | --- |
| [`data_collection.md`](data_collection.md) | 采集协议、600 条设计、标注口径 |
| [`../tools/audio/README.md`](../tools/audio/README.md) | M2 串口音频验收与 WAV 下载 |
| [`public_datasets.md`](public_datasets.md) | 公开语料调研（speechocean762、L2-ARCTIC 等） |
| [`public_test_speechocean762.md`](public_test_speechocean762.md) | 2000 条公开数据实测：69.7% / int8 98.3% |
| [`toolchain.md`](toolchain.md) | Python/IDF/ONNX/TFLite/字体/串口工具链 |
| [`artifacts.md`](artifacts.md) | 模型、数据、视频到底在哪个目录 |

## 动画

[`animations/README.md`](animations/README.md) 汇总三个视频：

- `model-training`：78 秒概念讲解
- `training-run`：74 秒真实训练回放
- `cnn-visual`：44 秒真实激活 + Grad-CAM

## 论文

| 文件 | 内容 |
| --- | --- |
| [`thesis/开题报告.md`](thesis/开题报告.md) | 开题报告正文（5 个部分） |
| [`thesis/开题报告模板.doc`](thesis/开题报告模板.doc) | 学校模板（原根目录文件已移入） |

## 目录约定

```text
main/          固件源码
components/    可复用驱动组件
docs/          说明、实验记录、论文、动画
tools/         PC 工具（采集/训练/部署/参考音频）
data/          词表 + 本地数据（collected/reference 不进 Git）
build/         生成物（不进 Git）
```

> 生成物、公开语料、自采录音、Oxford 音频都不要提交到 Git；见 [`artifacts.md`](artifacts.md)。
