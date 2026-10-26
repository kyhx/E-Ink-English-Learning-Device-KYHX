# Python 模型 / 数据管线

一条命令验证：**WAV → log-Mel 40×64 → 小 CNN → PyTorch → ONNX → TFLite float/int8 → parity → C 数组/golden**。

它不是“设备模拟器”，而是 **PC 端训练/部署验证工具**：先保证特征、模型、量化在 PC 上闭环，再上 ESP32-S3。

## 快速跑通（合成数据）

```powershell
$env:PYTHONUTF8='1'
.\.venv-train\Scripts\python.exe tools\pipeline\run.py --toy --epochs 20 --out build/pipeline_toy
```

本机实测结果：

```text
features: (300, 1, 40, 64)
split: speaker_independent  train=200 val=100  val_speakers=['S04','S05']
model: TinyMelNet, 6066 params
best epoch 18, val acc 1.000, F1 1.000
onnx:        max_logit=1.19e-06, label_agreement=1.000
float TFLite: max_logit=1.67e-06, label_agreement=1.000
int8 TFLite:  max_prob=0.122, label_agreement=1.000
result: PASS
```

> 合成数据的两类只是“高频共振峰不同”，准确率 100% 只说明**管线通了**，不是真实发音准确率。真实数据请用 `--data-dir`。

## 真实数据

目录结构：

```text
data/dataset/v1.0/
├── labels.csv
└── audio/
    ├── u0001.wav
    └── ...
```

`labels.csv`：

```csv
utt_id,file,speaker_id,word_id,label,split
u0001,audio/u0001.wav,S01,12,1,train
u0002,audio/u0002.wav,S01,12,0,train
u0003,audio/u0003.wav,S09,12,1,test
```

- `label`：1=正确/可接受，0=错误
- `speaker_id`：用于**按说话人独立划分**；不要随机按样本切
- `word_id`：可选，当前 mel-only 基线不参与计算，留给后续词条件化
- `split`：可选，`train` / `test`（或 `val`）。有则直接沿用官方划分，优先级高于自动留出说话人
- `file`：相对 `data-dir` 的路径；也支持只写文件名（会在 `audio/` 下找）

运行：

```powershell
.\.venv-train\Scripts\python.exe tools\pipeline\run.py `
  --data-dir data/dataset/v1.0 `
  --out build/pipeline_v1 `
  --val-speakers S05,S06 `
  --epochs 40 --augment
```

不写 `--val-speakers` 时，会按说话人随机留出约 25%；只有 1 个说话人时会退化成随机切分并打印警告。若 `labels.csv` 带 `split` 列，则直接使用该列。

### 公开数据测试

已用 speechocean762（CC BY 4.0）跑过 2000 条平衡子集：官方 125/125 说话人划分，val accuracy 69.7%，int8 标签一致率 98.3%。复现：

```powershell
.\.venv-train\Scripts\python.exe tools\pipeline\prepare_speechocean762.py `
  --corpus build\data\speechocean762 `
  --out build\data\speechocean762_pipeline --limit-per-class 500

.\.venv-train\Scripts\python.exe tools\pipeline\run.py `
  --data-dir build\data\speechocean762_pipeline `
  --out build\pipeline_speechocean762 --epochs 20 --batch-size 32 --augment
```

详见 [`docs/public_test_speechocean762.md`](../../docs/public_test_speechocean762.md)。注意这是**句级评分**的代理任务，不是 2 秒单词卡准确率。

## 输出

| 文件 | 用途 |
| --- | --- |
| `features.npz` | 缓存特征 `x[N,1,40,64]`、`y`、`speaker` |
| `split.json` | 可复现的训练/验证 utt 列表 |
| `model.pt` | PyTorch 权重 |
| `history.json` / `metrics.json` | 训练曲线、accuracy/F1/混淆矩阵 |
| `val_predictions.csv` | 逐条验证结果，方便错误分析 |
| `model.onnx` | ONNX 模型，单文件 |
| `model_float32.tflite` | float TFLite |
| `model_int8.tflite` | **full int8** TFLite（部署用） |
| `model_params.h` | 输入/输出 scale、zero、shape、类别名 |
| `model_data.cc` | TFLite 权重 C 数组 |
| `golden.npz` | 测试特征 + PyTorch 输出 + int8 输出，供设备 parity |
| `parity.json` | ONNX/float/int8 与 PyTorch 的误差 |
| `report.md` | 汇总报告（先看这个） |

## 特征规范（训练与 C 端必须一致）

`features.py` 是唯一来源：

```text
16 kHz mono，2.0 s（32000 samples）
VAD：20 ms 帧 / 10 ms hop，首尾留 100 ms
n_fft=512，win=400（25 ms），hop=160（10 ms），Hamming
40 Mel，fmin=20，fmax=7600，power=2
log(max(mel, 1e-6))
每句 CMVN，clip [-5,5]
时间轴自适应平均池化到 64 帧
输出 float32 [40,64]；int8 量化用 model_params.h 的 input scale/zero
```

> 不要在不同文件里“凭感觉”改窗长/hop/Mel 数；改 `features.py` 后必须重新生成 golden 并做设备 parity。

## 部署

1. 把 `model_data.cc`、`model_params.h` 放进 `main/ai/`。
2. TFLM 侧 `input` 形状是 **NCHW `[1,1,40,64]`**（因为转换用了 `onnx2tf -kat mel`），C 端按同样顺序填。
3. 输入量化：`q = clip(round(x / MODEL_INPUT_SCALE + MODEL_INPUT_ZERO), -128, 127)`。
4. 输出反量化：`prob_logit = (y - MODEL_OUTPUT_ZERO) * MODEL_OUTPUT_SCALE`，再 softmax。
5. 用 `golden.npz` 做设备端 `bench parity`：标签一致率 ≥95%，概率差 <0.02–0.05（设备端可能因 TFLM 算子略有差异）。

## 这个管线能验证什么

- 音频读取/重采样/VAD/log-Mel 的形状和数值；
- 小 CNN 是否能过拟合/收敛；
- 按说话人划分是否真的把“人”分开了；
- int8 量化后标签是否一致、概率差多大；
- ONNX/TFLite 转换是否保持单文件、输入布局是否正确；
- C 数组和 golden 向量是否可直接交给固件。

## 不能验证什么

- 设备端 **TFLM 实际延迟、arena、PSRAM 访问**；
- 麦克风噪声/回声/削顶；
- 屏幕带状刷新 346 ms、残影；
- 深睡电流、Wi-Fi 同步；
- 真实发音的泛化能力（合成数据 100% 没有意义）。

这些必须用 `bench` 和真实硬件测。

## GUI 控制台

不想敲命令时用 Tkinter 界面：

```powershell
.\.venv-train\Scripts\python.exe tools\pipeline\gui.py
```

也可以直接双击 `tools/pipeline/GUI.bat`。

- **采集 / 标注**：选说话人/单词/麦克风，录 2 秒，试听 + 看 Mel，标正确/错误与错误类型，保存到 `data/collected/v1.0`；已采集列表可改标注。
- **数据 / 预览**：选择目录、检查样本、生成合成数据、点样本看 40×64 Mel。
- **训练 / 指标**：epochs/batch/lr/验证说话人/增强；开始/停止；实时日志；训练后自动读 `metrics.json`、`parity.json`。
- **部署 / 试推理**：产物列表、report、parity 详情；选一条 WAV 用 int8 TFLite 试推理并显示 mel/概率/耗时。

GUI 只是 `run.py` 的前端，不重复实现训练/转换；关闭窗口时若训练仍在跑会提示。

采集协议、600 条设计和标注口径见 [`docs/data_collection.md`](../../docs/data_collection.md)。采集后先跑：

```powershell
.\.venv-train\Scripts\python.exe tools\pipeline\check_collected.py --data-dir data\collected\v1.0
```

## 文件

```text
tools/pipeline/
├── features.py    # WAV -> log-Mel，训练/C 端唯一规范
├── dataset.py     # labels.csv、合成数据、按说话人切分
├── model.py       # TinyMelNet
├── train.py       # 训练循环 + 指标
├── convert.py     # ONNX/TFLite + parity + C 导出
├── run.py         # 一条命令串起来
├── gui.py         # Tkinter 控制台（采集/标注 + 训练 + 部署）
├── GUI.bat        # Windows 双击启动
├── check_collected.py  # 采集数据完整性/静音/削顶/重复检查
├── visualize_cnn.py    # 特征图 + Grad-CAM 可视化
├── prepare_speechocean762.py  # 公开语料 -> labels.csv
└── README.md
```

## 已知限制

- 当前是 **mel-only 基线**，没有词 ID 条件化/参考 Mel；这是下一步实验。
- 合成数据只用于管线验证，不能写进论文结果。
- int8 的 logits 可能和 PyTorch 差 0.5–1.0，但 softmax 概率和标签通常仍一致；论文里报告**概率差/标签一致率**，不要只报 logits。
- 真实数据若只有 1 个说话人，脚本会警告；这时结果不能叫“说话人独立”。
