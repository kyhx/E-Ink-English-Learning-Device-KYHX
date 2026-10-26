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

## 已验证

```text
GET  /health      -> {"status":"ok"}
GET  /languages   -> en/fr/es/de/it/pt/nl
POST /phonemes    -> {"phonemes":["həloʊ","wɜːld"],"words":["hello","world"]}
GET  /            -> 200，页面标题 OpenPronounce
```

## 当前限制

`POST /pronunciation` 需要两个 Wav2Vec2 权重（各约 1.2 GB）：

- `facebook/wav2vec2-large-960h`
- `facebook/wav2vec2-lv-60-espeak-cv-ft`

本机到 `huggingface.co` 连接超时，权重未下载，所以评分接口返回 500；
`/phonemes`、`/health`、页面本身可用。网络恢复后，启动服务并第一次请求会自动下载，
或先跑：

```powershell
$env:HF_HOME="$PWD\build\openpronounce\hf"
C:\venv-eink\Scripts\openpronounce.exe third_party\OpenPronounce\assets\developer.wav "hello I am a developer"
```

模型缓存目录：`build/openpronounce/hf`。
