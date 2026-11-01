# OpenPronounce 本地部署

上游：<https://github.com/Halleck45/OpenPronounce>，MIT，v0.3.0，克隆在
`third_party/OpenPronounce`（不提交 Git）。

## 启动

```powershell
pwsh -File tools\openpronounce\start.ps1
```

然后打开 <http://127.0.0.1:8000>。

依赖：

- Python 3.12（`C:\venv-eink`）
- torch 2.14.1 CPU、transformers 5.17、fastapi/uvicorn、phonemizer、gTTS
- ffmpeg 9.0.2、eSpeak NG 1.52（`C:\Program Files\eSpeak NG`）

## 已下载模型

HuggingFace 直连超时，权重改从 **ModelScope 同名仓库**下载，已按
HuggingFace cache 布局放好：

```text
artifacts/openpronounce/hf/hub/
├── models--facebook--wav2vec2-large-960h/
│   └── snapshots/c4a07f.../pytorch_model.bin   1,262,009,187 B
└── models--facebook--wav2vec2-lv-60-espeak-cv-ft/
    └── snapshots/c4a07f.../pytorch_model.bin   1,263,535,127 B
```

两个文件的 SHA256 已和 ModelScope API 返回值核对通过。

`start.ps1` 固定设置：

```text
HF_HOME=<repo>\artifacts\openpronounce\hf
HF_HUB_OFFLINE=1
TRANSFORMERS_OFFLINE=1
```

这样服务不会再尝试连 HuggingFace。

## 已验证

```text
GET  /health      -> {"status":"ok"}
GET  /languages   -> en/fr/es/de/it/pt/nl
POST /phonemes    -> {"phonemes":["həloʊ","wɜːld"],"words":["hello","world"]}
GET  /            -> 200，页面标题 OpenPronounce
POST /pronunciation -> 200
  developer.wav          score 99.01, acoustic 6.297, WER 0, PER 0
  developer_error_an.wav score 87.22, acoustic 6.533, WER 0.2, PER 0.125
pytest            -> 99 passed
```

## 重装模型

删除了 `artifacts/openpronounce/hf` 时，先去掉启动脚本里的两个 `*_OFFLINE=1`，
再让第一次评分下载，或按 `docs/openpronounce.md` 的 ModelScope 地址重新放入
cache。模型不要提交进 Git。
