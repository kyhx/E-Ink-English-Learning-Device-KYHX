# 生成物与数据地图

`artifacts/`、`build/`、`build-collect/`、`data/collected/`、`data/reference/` 都不进 Git。
`artifacts/README.md` 是唯一例外，说明目录结构。

## 总览

```text
artifacts/
├── models/                 # 训练/量化/转换输出
├── datasets/               # 公开语料与清单
├── firmware/               # 合并后可烧录的镜像
├── openpronounce/          # PC 参考模型缓存
├── captures/               # 测试 WAV/截图/串口 dump
├── logs/                   # 构建、OpenPronounce、固件日志
└── reference/              # 外部 PDF/手册
```

## 当前模型

```text
artifacts/models/pipeline_speechocean762/
├── model.pt                            # PyTorch state_dict
├── model.onnx                          # ONNX
├── model_float32.tflite                # float TFLite
├── model_int8.tflite                   # 部署用 int8，11.5 KB
├── model_data.cc                       # TFLM C 数组
├── model_params.h                      # scale/zero
├── golden.npz                          # 设备 parity 向量
├── metrics.json / history.json         # 指标/曲线
├── val_predictions.csv                 # 逐条预测
├── parity.json                         # ONNX/float/int8 一致性
└── report.md                           # 汇总
```

> 这是 **speechocean762 句级代理任务**模型（val acc 69.7%），不是最终单词卡模型；固件还没有把 `model_data.cc` 编进去。

## 数据

| 路径 | 内容 | 大小 |
| --- | --- | --- |
| `artifacts/datasets/speechocean762/` | 解压公开语料（5000 句 + scores） | 686 MB |
| `artifacts/datasets/speechocean762.tar.gz` | 原始压缩包（已删，可重新下载） | — |
| `artifacts/datasets/speechocean762_pipeline/` | 2000 条平衡子集 `labels.csv` | 0.1 MB |
| `data/collected/v1.0/` | 自采 WAV + `labels.csv` | 占位 |
| `data/collected/device/v1.0/` | 设备采集同步目录 | 录音后增长 |
| `data/reference/oxford/` | 50 词 × 英/美音 | 约 3.3 MB |
| `data/words.csv` | 50 词 + 中文释义 | 3 KB（提交） |

## 固件

```text
artifacts/firmware/E-Ink-collect.bin             # 采集版合并镜像
build/E-Ink-English-Learning-Device-KYHX.bin     # 默认 M2 固件
build-collect/...                                # 采集版 IDF 构建目录
```

## 视频

| 目录 | 内容 |
| --- | --- |
| `docs/animations/model-training/` | 78 秒概念讲解 |
| `docs/animations/training-run/` | 74 秒真实训练回放 |
| `docs/animations/cnn-visual/` | 44 秒 CNN 激活/Grad-CAM |

## 构建目录

| 路径 | 说明 |
| --- | --- |
| `build/` | 默认固件 IDF/CMake 构建 |
| `build-collect/` | 采集固件 IDF/CMake 构建 |
| `managed_components/` | IDF 托管组件，自动生成 |

`build/`、`build-collect/`、`managed_components/` 都可以重建，不要手工改。

## 空间不够时

按顺序删：

1. `artifacts/datasets/speechocean762.tar.gz`（已删；需要时重新下载）
2. `artifacts/models/pipeline_toy2/`（23 MB）
3. `artifacts/openpronounce/hf/`（2.4 GB，只在 PC 参考评分时需要）
4. `build/esp-idf/` 或 `build-collect/esp-idf/`（约 190 MB/个）

**不要删**：

```text
artifacts/models/pipeline_speechocean762/model_int8.tflite
artifacts/models/pipeline_speechocean762/model_params.h
artifacts/models/pipeline_speechocean762/golden.npz
artifacts/firmware/E-Ink-collect.bin
```

## 重新生成

```powershell
# 模型
.\.venv-train\Scripts\python.exe tools\pipeline\run.py --data-dir artifacts\datasets\speechocean762_pipeline --out artifacts\models\pipeline_speechocean762

# 采集固件
pwsh -File tools\collect\build_collect_firmware.ps1

# 默认固件
idf.py build
```
