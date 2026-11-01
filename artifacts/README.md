# Artifacts 目录

`artifacts/` 放**生成物**，不进 Git；只有本 README 会被跟踪。
`build/` 和 `build-collect/` 只放 ESP-IDF/CMake 的构建中间文件。

```text
artifacts/
├── models/                 # 训练/转换输出
│   ├── pipeline_speechocean762/   # 当前公开数据模型（6,066 参数）
│   └── pipeline_toy2/             # 合成数据冒烟
├── datasets/               # 公开语料/清单
│   ├── speechocean762/             # 解压语料
│   ├── speechocean762.tar.gz       # 原始压缩包
│   └── speechocean762_pipeline/    # 2000 条平衡子集 labels.csv
├── firmware/               # 可直接烧录的合并镜像
│   └── E-Ink-collect.bin
├── openpronounce/          # PC 参考模型缓存
│   └── hf/hub/             # 两个 Wav2Vec2 权重，约 2.4 GB
├── captures/               # 测试 WAV/截图/串口 dump
├── logs/                   # 构建/OpenPronounce/固件日志
└── reference/              # 外部 PDF/手册
```

## 当前关键文件

准确路径与大小：

```text
artifacts/models/pipeline_speechocean762/
├── model.pt
├── model.onnx
├── model_float32.tflite
├── model_int8.tflite          # 设备部署模型，11.5 KB
├── model_data.cc
├── model_params.h
├── golden.npz
├── metrics.json / history.json / report.md
└── val_predictions.csv
```

```text
artifacts/datasets/
├── speechocean762/             # 686 MB
├── speechocean762.tar.gz       # 已删；需要时重新下载
└── speechocean762_pipeline/    # 2000 条平衡 manifest
```

```text
artifacts/openpronounce/hf/hub/
├── models--facebook--wav2vec2-large-960h/...
└── models--facebook--wav2vec2-lv-60-espeak-cv-ft/...
```

## 空间不够时

按顺序删：

1. `artifacts/datasets/speechocean762.tar.gz`（已删；需要时重新下载）
2. `artifacts/models/pipeline_toy2/`（23 MB，玩具演示）
3. `build/esp-idf/` 或 `build-collect/esp-idf/`（约 190 MB/个，重新 build 即可）
4. `artifacts/openpronounce/hf/`（2.4 GB，只在 PC 参考评分时需要）

**不要删**：

```text
artifacts/models/pipeline_speechocean762/model_int8.tflite
artifacts/models/pipeline_speechocean762/model_params.h
artifacts/models/pipeline_speechocean762/golden.npz
artifacts/firmware/E-Ink-collect.bin
```

## 重新生成

```powershell
# 训练/转换
.\.venv-train\Scripts\python.exe tools\pipeline\run.py --toy --epochs 20 --out artifacts\models\pipeline_toy
.\.venv-train\Scripts\python.exe tools\pipeline\run.py --data-dir artifacts\datasets\speechocean762_pipeline --out artifacts\models\pipeline_speechocean762

# 采集固件
pwsh -File tools\collect\build_collect_firmware.ps1

# OpenPronounce 权重
pwsh -File tools\openpronounce\start.ps1
```

`build/` 与 `build-collect/` 都可以由 `idf.py build` 重建。
