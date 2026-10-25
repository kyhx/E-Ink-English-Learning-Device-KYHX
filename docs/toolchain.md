# 工具链准备记录（2026-10-25）

> 目标：把“固件构建 + 语音/数据处理 + 模型训练 + 量化转换 + 串口采集”所需的工具一次性装好，并留下可复现的验证脚本。
>
> 结论：**Python 3.12 训练环境、PyTorch→ONNX→TFLite int8 转换链、ESP-IDF v6.1 + 三个托管组件均已验证通过**。固件 build 通过，样例模型转换 PASS。

---

## 1. 已安装与已验证

### 1.1 固件侧

| 项 | 版本 / 路径 | 状态 |
| --- | --- | --- |
| ESP-IDF | v6.1，`F:\esp\v6.1\esp-idf` | ✅ `idf.py --version` |
| Python（IDF 内置） | 3.14.7 | ✅ |
| CMake | 4.0.3 | ✅ |
| Ninja | 1.12.1 | ✅ |
| Xtensa GCC | 15.2.0 | ✅ |
| 目标 | ESP32-S3 / 16 MB Flash / 8 MB Octal PSRAM | ✅ |
| 串口 | **COM4**（USB-Enhanced-SERIAL CH343） | ✅ 当前已连接 |

**托管组件**（`main/idf_component.yml`，版本已精确锁定）：

| 组件 | 版本 | 用途 |
| --- | --- | --- |
| `joltwallet/littlefs` | 1.23.1 | 词库/音频/记录/采集临时文件 |
| `espressif/esp-tflite-micro` | 1.4.1 | 端侧 int8 推理 |
| `espressif/esp-dsp` | 1.8.2 | FFT / Mel 前端加速 |
| `espressif/esp-nn` | 1.4.1 | TFLM 的 int8 算子加速（传递依赖） |

`idf.py build` 已通过；当前 M0 app 约 **334 KB**，3 MB factory 分区剩余 89%。

> 组件下载到 `managed_components/`（已 gitignore）；首次在新机器构建需要能访问 `components.espressif.com`。

### 1.2 训练 / 转换侧

真实 venv 放在 **`C:\venv-eink`**（短路径），项目内 `.venv-train` 是指向它的 **junction**。

> 为什么不用项目内真实 venv：TensorFlow wheel 的 `include/external/...` 路径很深，项目路径 + venv 路径会超过 Windows 260 字符限制，`pip install tensorflow` 会报 `No such file or directory`。短路径 + junction 已解决；脚本仍然使用 `.venv-train\Scripts\python.exe`。

| 包 | 版本 | 用途 |
| --- | --- | --- |
| Python | 3.12.4 | 训练/转换 |
| torch | 2.14.1+cpu | 训练（CPU 足够，模型只有几 k 参数） |
| tensorflow | 2.21.0 | TFLite 转换/解释器 |
| tf_keras | 2.21.0 | onnx2tf 的 `tf_converter` 后备路径 |
| onnx | 1.20.1 | PyTorch → ONNX |
| onnxruntime | 1.26.0 | PC 端推理对照 |
| onnx2tf | 2.6.9 | ONNX → TFLite + int8 |
| ai-edge-litert | 2.1.2 | PC 端 TFLite 解释器 |
| numpy / scipy | 2.2.6 / 1.18.1 | 数值/重采样 |
| pandas / scikit-learn | 3.1.0 / 1.9.1 | 数据表/指标/基线 |
| matplotlib | 3.11.2 | 图表；自带 DejaVuSans（IPA 字形） |
| librosa / soundfile | 1.0.0 / 0.14.0 | 音频读取、重采样、Mel |
| Pillow | 13.0.0 | 字库/页面 PNG |
| fonttools | 4.67.0 | 字库 cmap 覆盖检查 |
| pyserial | 3.5 | 串口采集/验收 |
| pytest | 9.0.2 | 单测 |
| PyYAML / tqdm | 6.0.3 / 4.67.1 | 配置/进度 |

**锁文件**：`tools/train/requirements.lock.txt`（`pip freeze` 生成，仅用于记录精确版本；不要直接 `pip install -r` 它，因为 `torch==2.14.1+cpu` 来自 PyTorch 专用 index）。复现请运行 `tools/setup/install-train.ps1`。

### 1.3 字体

| 用途 | 路径 | 说明 |
| --- | --- | --- |
| IPA / 拉丁 | `C:\venv-eink\Lib\site-packages\matplotlib\mpl-data\fonts\ttf\DejaVuSans.ttf` | 随 matplotlib 安装，IPA 覆盖好 |
| 中文 | `C:\Windows\Fonts\NotoSansSC-VF.ttf` | 思源黑体可变字体，OFL |
| 中文后备 | `C:\Windows\Fonts\msyh.ttc` / `Deng.ttf` | 微软雅黑/等线 |

> 生成点阵字库时用 **DejaVuSans 出 IPA、Noto Sans SC 出中文**，不要用 Arial 硬凑 IPA。许可证单独记录到 `THIRD_PARTY_NOTICES.md`。

---

## 2. 一键验证

从仓库根目录：

```powershell
# 只检查（Python 包 + IDF + 组件 + 串口）
pwsh -File tools/setup/verify-toolchain.ps1

# 检查并重新 build 固件
pwsh -File tools/setup/verify-toolchain.ps1 -Build
```

单独检查 Python：

```powershell
.\.venv-train\Scripts\python.exe tools\setup\verify_python.py
```

预期最后输出 `PASS` / `"failed": []`。

### 2.1 模型转换冒烟测试

```powershell
$env:PYTHONUTF8='1'
.\.venv-train\Scripts\python.exe tools\train\smoke_convert.py
```

本机实测结果：

```text
ONNX vs PyTorch max abs diff: 2.980e-08
float TFLite vs PyTorch max abs diff: 5.960e-08
int8 TFLite vs PyTorch max abs diff (logits): 1.379e-01
int8 vs PyTorch max abs diff (softmax): 5.109e-02
[PASS] PyTorch -> ONNX -> TFLite float32 + int8 -> LiteRT
```

样例模型：`1×40×64 mel → Conv8 → Conv16 → GAP → Dense2`；float TFLite 约 **7.9 KB**，full-int8 约 **5.5 KB**。

> 该测试的 int8 校准数据是 0–1 随机数，只用于验证“转换链路能跑通”。**真实模型必须用真实 log-Mel 校准数据**，否则输入 scale 会按 1/255 算，部署时严重饱和。

### 2.2 音频链路冒烟测试

```powershell
@'
import tempfile, numpy as np, soundfile as sf, librosa
sr=16000; t=np.arange(sr)/sr
x=(0.1*np.sin(2*np.pi*440*t)).astype(np.float32)
p=tempfile.mktemp(suffix='.wav'); sf.write(p,x,sr,subtype='PCM_16')
y,sr2=sf.read(p,dtype='float32')
mel=librosa.feature.melspectrogram(y=y,sr=sr2,n_fft=512,hop_length=160,
    win_length=400,n_mels=40,fmin=20,fmax=7600,power=2.0)
print(y.shape, sr2, librosa.power_to_db(mel).shape)
'@ | .\.venv-train\Scripts\python.exe -
```

本机输出：`(16000,) 16000 (40, 101)`，全有限值。

### 2.3 字库覆盖检查

```powershell
.\.venv-train\Scripts\python.exe tools\fonts\check_fonts.py
```

本机结果：DejaVuSans 的 36 个 IPA 字符全部覆盖；Noto Sans SC 的 37 个示例汉字全部覆盖。正式词库生成后，应把 `words.csv` 里出现过的所有 IPA/汉字都加入检查，缺字直接让构建失败。

---

## 3. 常用命令

### 3.1 固件

```powershell
$env:IDF_PATH='F:\esp\v6.1\esp-idf'
. 'F:\esp\v6.1\esp-idf\export.ps1'
idf.py build
idf.py -p COM4 flash monitor
```

> 建议在 PowerShell 里同时设置 `$env:PYTHONUTF8='1'`，避免 IDF 的中文/Unicode 警告。

### 3.2 Python

```powershell
.\.venv-train\Scripts\Activate.ps1
$env:PYTHONUTF8='1'
python tools\setup\verify_python.py
python tools\train\smoke_convert.py
```

### 3.3 重新安装

```powershell
pwsh -File tools\setup\install-train.ps1
```

脚本会：
1. 用 `py -3.12` 创建/复用 `C:\venv-eink`；
2. 建立 `.venv-train` junction；
3. 安装 torch CPU + `requirements-convert.txt`；
4. 写出 `tools/train/requirements.lock.txt`；
5. 做 import 检查。

---

## 4. 模型转换的关键注意点

### 4.1 必须加 `-kat mel`

40×64 不是 RGB 图像；onnx2tf 默认把 NCHW 自动转 NHWC，会在 `GlobalAveragePool + Flatten` 处把 `[1,10,1,1]` reshape 成 `[1,16]` 而报错。加：

```bash
onnx2tf -i model.onnx -o out -kat mel -n
```

TFLite 输入会保持 **NCHW `[1,1,40,64]`**，C 端按 NCHW 填即可；不要再手动转 NHWC。

### 4.2 int8 转换

```bash
onnx2tf -i model.onnx -o out_int8 -kat mel -oiqt \
  -cind mel calib_nchw.npy 0.0 1.0 -n
```

- `calib_nchw.npy` 形状 `[N,1,40,64]`，float32。
- **必须用真实 log-Mel**（VAD/CMVN 之后的值），覆盖男女、正确/错误、安静/噪声；不要用 0–1 随机数。
- 如果 CMVN 后数据范围约 `[-3,3]`，建议直接用真实数据，`mean=0, std=1`；不要套 ImageNet 的 mean/std。
- 输出文件：`*_full_integer_quant.tflite`（int8 输入/输出）和 `*_integer_quant.tflite`（float 输入 / int8 输出）等。部署优先 `full_integer_quant`。

### 4.3 转换后的自检

- 用 `ai_edge_litert.interpreter.Interpreter` 或 `tensorflow.lite.Interpreter` 读 `input_details` / `output_details`，记录：
  - 输入 shape（本项目应为 `[1,1,40,64]`）
  - 输入/输出 scale、zero-point
  - 输出 shape（`[1,2]`）
- float 与 PyTorch 对齐：max abs diff < 1e-4。
- int8 与 PyTorch 对齐：标签一致，softmax 概率差建议 < 0.05–0.10。
- 设备端再跑 `bench parity`（M6）。

### 4.4 备选路径

如果 `flatbuffer_direct` 对某个算子不支持：

```bash
onnx2tf -i model.onnx -o out -tb tf_converter -kat mel -n
```

需要 `tf_keras`（已装）。我们已验证 **flatbuffer_direct** 足够跑当前 TinyMelNet 的 float + full-int8。

---

## 5. 还没装 / 不需要现在装

| 项 | 说明 |
| --- | --- |
| CUDA 版 PyTorch | 当前模型只有几 k 参数，CPU 训练足够；如以后加大模型再装 |
| ffmpeg / sox | 当前统一用 16 kHz 单声道 WAV，`soundfile` 足够；如需 MP3/M4A 批量转码再装 |
| TTS 工具 | 参考发音优先真人录制；TTS 只作临时占位 |
| 手机 App / 云服务 | 非目标，同步用 SoftAP + 浏览器 |
| 触摸驱动 | 非目标，5 键方案 |

---

## 6. 常见问题

| 现象 | 处理 |
| --- | --- |
| `pip install tensorflow` 报长路径错误 | 已用 `C:\venv-eink` + `.venv-train` junction 解决；不要改回项目内真实 venv |
| `torch.onnx` 打印 emoji 报 `UnicodeEncodeError: gbk` | 设 `$env:PYTHONUTF8='1'`；`smoke_convert.py` 也会把 stdout 切 UTF-8 |
| onnx2tf 报 `Cannot reshape ... [1,10,1,1]` | 加 `-kat mel` |
| int8 转换报缺少 `tf_keras` | `pip install tf_keras==2.21.0`；或改用默认 flatbuffer_direct |
| `ninja: CreateProcess: Access is denied`（`sections.ld-*.bat`） | 多为杀毒/索引临时锁住 `build/` 里的批处理；先重试 `idf.py build`，仍失败则在 `build/` 手动 `cmd /c CMakeFiles\sections.ld-*.bat`，或把 `build/` 加入杀毒排除 |
| 组件下载失败 | 检查 `components.espressif.com` 网络；`managed_components/` 可删掉重下 |
| `idf.py` 中文乱码/警告 | `$env:PYTHONUTF8='1'`，必要时 `chcp 65001` |
| 找不到 COM4 | 板子未插/驱动未装；`verify-toolchain.ps1` 会列出当前串口 |
| PowerShell 禁止运行脚本 | `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`（按学校/公司策略） |

---

## 7. 下一步

工具链已经够跑 M0–M2 和 M5 的模型实验：

1. **M0**：分区表 + LittleFS 挂载 + 按键 + `bench` 控制台。
2. **M1**：`words.csv` → `words.bin`/`audio.pak` + 字库生成。
3. **M2**：I2S 录音/播放 + WAV 落盘。
4. **M5**：`features.py` + 小 CNN + LOSO；转换时用 `-kat mel` 和真实 Mel 校准。
5. **M6**：把 `*_full_integer_quant.tflite` 转成 C 数组，接 `esp-tflite-micro`，跑 `bench parity`。

> 工具链验证脚本和 lock 文件建议一起提交；`managed_components/`、`.venv-train/`、`*.log` 已在 `.gitignore` 中。
