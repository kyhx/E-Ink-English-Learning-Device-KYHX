# 生成物与数据地图

`build/`、`data/collected/`、`data/reference/` 都不进 Git；这里说明每个目录是什么、哪些能删。

## 当前模型

```text
build/pipeline_speechocean762/          # 当前公开数据模型，28.6 MB
├── model.pt                            # PyTorch state_dict，继续训练用
├── model.onnx                          # ONNX，单文件
├── model_float32.tflite                # float TFLite
├── model_int8.tflite                   # 部署用 int8，11.5 KB
├── model_data.cc                       # TFLM C 数组
├── model_params.h                      # 输入/输出 scale/zero
├── golden.npz                          # 设备 parity 测试向量
├── metrics.json / history.json         # 指标/训练曲线
├── val_predictions.csv                 # 逐条预测
├── parity.json                         # ONNX/float/int8 一致性
└── report.md                           # 汇总报告
```

> 这是 **speechocean762 句级代理任务**模型（val acc 69.7%），不是最终单词卡模型；固件还没有把 `model_data.cc` 编进去。

## 数据

| 路径 | 内容 | 大小 | 是否保留 |
| --- | --- | --- | --- |
| `build/data/speechocean762/` | 解压后的公开语料（5000 句 + scores） | 686 MB | 保留 |
| `build/data/speechocean762.tar.gz` | 原始压缩包 | 497 MB | 可选；解压后空间紧张可删 |
| `build/data/speechocean762_pipeline/` | 2000 条平衡子集 `labels.csv` | 0.1 MB | 保留 |
| `data/collected/v1.0/` | 自采 WAV + `labels.csv` | 目前占位 | 保留，录音后增长 |
| `data/reference/oxford/` | 50 词 × 英/美音，MP3+WAV+metadata | 约 3.3 MB | 保留；版权音频不要提交 |
| `data/words.csv` | 50 词 + 中文释义 | 3 KB | **提交** |

## 视频

| 目录 | 内容 |
| --- | --- |
| `docs/animations/model-training/` | 78 秒概念讲解 |
| `docs/animations/training-run/` | 74 秒真实训练回放 |
| `docs/animations/cnn-visual/` | 44 秒 CNN 激活/Grad-CAM |

每个目录只保留 `index.html`、`assets/`、`README.md`、`renders/*.mp4`、`renders/preview.png`；`.hyperframes/` 是检查缓存，已忽略。

## 构建产物

| 路径 | 说明 |
| --- | --- |
| `build/esp-idf/` | ESP-IDF 增量构建，约 190 MB |
| `build/bootloader/`、`build/config/` 等 | `idf.py build` 生成 |
| `managed_components/` | ESP-IDF 托管组件，自动生成 |

这些都可以通过 `idf.py build` 重新生成，不要手工改。

## 2026-10-26 清理

已删除临时测试目录：

```text
build/gui_e2e
build/gui_e2e2
build/gui_toy_dataset
build/pipeline_cachetest
build/pipeline_gui
build/pipeline_realcheck
build/pipeline_toy       # 旧版；保留 pipeline_toy2
```

保留：`pipeline_speechocean762`（当前模型）、`pipeline_toy2`（合成数据冒烟）、`build/data`（公开语料/清单）、`build/esp-idf`（固件增量构建）。

## 空间不够时

按顺序删：

1. `build/data/speechocean762.tar.gz`（497 MB，可重新下载）
2. `build/esp-idf/`（190 MB，重新 build 即可）
3. `build/pipeline_toy2/`（23 MB，玩具演示）

**不要删** `build/pipeline_speechocean762/model_int8.tflite`、`model_params.h`、`golden.npz`；否则要重新训练/转换。
