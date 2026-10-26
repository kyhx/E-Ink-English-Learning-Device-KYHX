# 工具索引

```text
tools/
├── setup/       # Python/IDF/转换链安装与验证
├── train/       # requirements、ONNX→TFLite 冒烟、玩具训练
├── pipeline/    # 采集/标注 + 训练/量化/部署 + GUI
├── reference/   # Oxford 参考发音下载
├── fonts/       # IPA/中文字形覆盖检查
└── *.py         # 旧版图片/字库/接线辅助脚本
```

## 常用

| 任务 | 命令 |
| --- | --- |
| 安装训练环境 | `pwsh -File tools/setup/install-train.ps1` |
| 验证工具链 | `pwsh -File tools/setup/verify-toolchain.ps1` |
| 冒烟转换 | `.\.venv-train\Scripts\python.exe tools\train\smoke_convert.py` |
| 玩具训练 | `.\.venv-train\Scripts\python.exe tools\train\tutorial_train.py` |
| 采集/标注 GUI | `.\.venv-train\Scripts\python.exe tools\pipeline\gui.py` |
| 一键训练/部署 | `.\.venv-train\Scripts\python.exe tools\pipeline\run.py --toy ...` |
| 采集检查 | `.\.venv-train\Scripts\python.exe tools\pipeline\check_collected.py --data-dir data\collected\v1.0` |
| CNN 可视化 | `.\.venv-train\Scripts\python.exe tools\pipeline\visualize_cnn.py --wav ... --out ...` |
| Oxford 参考音 | `.\.venv-train\Scripts\python.exe tools\reference\fetch_oxford_audio.py --words-file data\words.csv` |
| 字库覆盖 | `.\.venv-train\Scripts\python.exe tools\fonts\check_fonts.py` |

## pipeline/

- `features.py`：训练/C 端唯一的 log-Mel 规范
- `dataset.py`：`labels.csv`、合成数据、按说话人划分
- `model.py` / `train.py`：TinyMelNet + 训练
- `convert.py`：ONNX/TFLite int8 + parity + C 数组
- `run.py`：一条命令串起来
- `gui.py` / `GUI.bat`：Tkinter 控制台
- `check_collected.py`：采集数据检查
- `prepare_speechocean762.py`：公开语料 → 平衡子集
- `visualize_cnn.py`：特征图 + Grad-CAM

详细用法：[`pipeline/README.md`](pipeline/README.md)。

## 旧版脚本

`png_to_raw.py`、`preview_demo.py`、`gen_font.py`、`font5x7_ascii.py`、`gen_wiring.py` 是早期屏显/字库/接线辅助脚本，保留用于复现，不是主流程。
