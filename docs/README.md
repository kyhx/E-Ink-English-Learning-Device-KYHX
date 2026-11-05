# 文档索引

顶层 [`README.md`](../README.md) 是项目入口；`docs/` 放深入说明、实验记录和论文材料。

> 最后整理：**2026-11-03**。当前状态以本页、[`交接文档.md`](交接文档.md)、M0/M2 记录为准；历史文档在 [`archive/`](archive/README.md)。

> **项目基调：主要目的就业；底线是毕设完整交付。**
> 技术定位是**端侧 AI / TinyML 工程实践**，不是算法理论创新。
> 取舍与交付物见 [`项目定位.md`](项目定位.md)；详细执行总纲见 [`../毕设基调.md`](../毕设基调.md)。

## 当前状态（速览）

| 层 | 状态 |
| --- | --- |
| 显示 | 70 行带状局刷 346 ms；整屏 709 ms；上电清屏 2.14 s |
| 存储 | 10 MB LittleFS；20/20 连续录音文件完整 |
| 音频 | 旧台架录音/VAD 通过；目标改为 LMD2718+NS4168 / 8Ω 1W |
| 采集 | 设备 5 键采集 + PC 同步已跑通；已采 53 条（基本 1 人，pilot），最低 200 条未达 |
| 词库 | 50 词已建（`data/words.csv`），**IPA 列仍为空** |
| 规则法 | 未实现（YIN/DTW），是 12-18 节点的另一半 |
| 评分 | PC OpenPronounce 可用；设备 TinyMelNet 未接 TFLM（就业关键） |
| 选型 | ESP32-S3-WROOM-1-N16R8 / CH343K / LMD2718+NS4168 / 8Ω 1W / HINA-E042A13-A0 |

## 先读

| 文档 | 内容 |
| --- | --- |
| [`项目定位.md`](项目定位.md) | 就业/毕设双目标、可砍/不可砍、交付物（先读） |
| [`thesis/进度安排.md`](thesis/进度安排.md) | **官方进度节点与剩余天数、关键路径、待确认事项** |
| [`../毕设基调.md`](../毕设基调.md) | 完整执行总纲：需求/验收/求职策略/流程建议 |
| [`交接文档.md`](交接文档.md) | 当前进度、踩坑、下一步 |
| [`技术方案.md`](技术方案.md) | 驱动方案、SPI/时序/局刷细节 |
| [`board_pinmap.md`](board_pinmap.md) | YD-ESP32-S3 P1/P2 引脚、音频/屏/按键接线 |
| [`pcb_references.md`](pcb_references.md) | 可借鉴的 ESP32-S3/EPD/音频/电源原理图与 PCB 项目网址 |
| [`M0测试记录.md`](M0测试记录.md) | M0 分区/LittleFS/按键/bench/E0 实测 |
| [`M2测试记录.md`](M2测试记录.md) | 旧台架 M2：录音/WAV/VAD/20 条/清屏/音量（历史） |

## 图表

| 文档 | 内容 |
| --- | --- |
| [`diagrams/README.md`](diagrams/README.md) | 系统框图、电源树、时钟树、设备拓扑（PNG/SVG/Mermaid） |

## 屏幕与硬件

| 文档 | 内容 |
| --- | --- |
| [`局刷测试记录.md`](局刷测试记录.md) | 全部刷新实验数据与波形分析 |
| [`真局部研究.md`](真局部研究.md) | 70 行带状真局刷的边界与 datasheet 依据 |
| [`HINK-E042A13-A0资料汇总.md`](HINK-E042A13-A0资料汇总.md) | 4.2" 屏体资料汇总 |
| [`datasheets/`](datasheets/) | 按 MCU/显示/音频/USB/电源/保护分类的芯片手册 |

## 数据与模型

| 文档 | 内容 |
| --- | --- |
| [`data_collection.md`](data_collection.md) | 采集协议、600 条设计、标注口径；含设备端采集固件 |
| [`../tools/collect/README.md`](../tools/collect/README.md) | 设备采集固件、按键、PC 同步 |
| [`../tools/audio/README.md`](../tools/audio/README.md) | M2 串口音频验收与 WAV 下载 |
| [`public_datasets.md`](public_datasets.md) | 公开语料调研（speechocean762、L2-ARCTIC 等） |
| [`public_test_speechocean762.md`](public_test_speechocean762.md) | 2000 条公开数据实测：69.7% / int8 98.3% |
| [`openpronounce.md`](openpronounce.md) | OpenPronounce 研究、本地部署、与本项目关联 |
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
| [`thesis/进度安排.md`](thesis/进度安排.md) | 官方进度节点、剩余天数、关键路径、缺口与待确认事项 |
| [`thesis/开题报告模板.doc`](thesis/开题报告模板.doc) | 学校模板（原根目录文件已移入） |

## 归档

| 文件 | 说明 |
| --- | --- |
| [`archive/README.md`](archive/README.md) | 历史文档索引 |
| [`archive/交接文档-M0-2026-10-25.md`](archive/交接文档-M0-2026-10-25.md) | M0 时期交接（只查历史） |

## 维护规则

- **状态**只写在 `README.md`、`交接文档.md` 和 M0/M2 测试记录里；其他文档引用，不重复维护。
- **实验数据**只追加到对应记录，不改旧结论；新结论用“追加/更正”标注日期。
- **路径**统一用仓库相对路径：生成物在 `artifacts/`，构建在 `build/`；旧路径只在归档里保留。
- **过时文档**移到 `archive/`，不要直接删除；归档文档不作为当前状态依据。

## 目录约定

```text
main/          固件源码
components/    可复用驱动组件
docs/          说明、实验记录、论文、动画
tools/         PC 工具（采集/训练/部署/参考音频）
data/          词表 + 本地数据（collected/reference 不进 Git）
artifacts/     生成物：模型/数据集/固件/日志（仅 README 入库）
build/         默认固件 IDF 构建（不进 Git）
build-collect/ 采集固件 IDF 构建（按需重建，不进 Git）
```

> 生成物、公开语料、自采录音、Oxford 音频都不要提交到 Git；见 [`artifacts.md`](artifacts.md)。
