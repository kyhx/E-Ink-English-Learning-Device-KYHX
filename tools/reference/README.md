# Oxford 参考发音下载

从 Google 词典静态端点下载 Oxford 英/美音，并转成 16 kHz 单声道 WAV。

## URL 模板

```text
https://ssl.gstatic.com/dictionary/static/sounds/oxford/<word>--_<accent>_1.mp3
```

实测：

```text
example--_gb_1.mp3   200
example--_us_1.mp3   200
example--_gb_2.mp3   404
```

- `accent`：`gb` / `us`
- `variant`：默认 `1`；有些词可能有 `2`，但 example 没有，所以默认只下 `1`
- 单词中的空格会转成 `_`；其他字符做 URL 编码

## 用法

```powershell
# 先看 URL，不下载
.\.venv-train\Scripts\python.exe tools\reference\fetch_oxford_audio.py `
  --words apple,book,cat --dry-run

# 下载全部 50 词，英音 + 美音
.\.venv-train\Scripts\python.exe tools\reference\fetch_oxford_audio.py `
  --words-file data\words.csv `
  --out data\reference\oxford `
  --accents both

# 只下英音；已有文件跳过
.\.venv-train\Scripts\python.exe tools\reference\fetch_oxford_audio.py `
  --words-file data\words.csv --accents gb --out data\reference\oxford
```

### 常用参数

| 参数 | 说明 |
| --- | --- |
| `--words-file` | `data/words.csv`（默认） |
| `--words` | 逗号分隔，覆盖词表 |
| `--accents` | `gb` / `us` / `both` |
| `--variants` | 默认 `1`；可试 `1,2`，404 会进 `missing.csv` |
| `--out` | 默认 `data/reference/oxford` |
| `--overwrite` | 重新下载 |
| `--dry-run` | 只打印 URL |
| `--no-wav` | 只存 MP3，不转 WAV |
| `--sleep` | 请求间隔，默认 0.15 s |

## 输出

```text
data/reference/oxford/
├── mp3/
│   ├── gb/apple.mp3
│   └── us/apple.mp3
├── wav/
│   ├── gb/apple.wav     # 16 kHz mono PCM16
│   └── us/apple.wav
├── metadata.csv
└── missing.csv
```

本次实测：**50 词 × 2 口音 = 100 条，100/100 成功，0 缺失**；100 个 SHA256 全部不同。时长约 0.57–1.03 s。

`metadata.csv` 字段：

```text
word_id, word, meaning, ipa, accent, variant, url,
mp3, wav, status, error, bytes, sha256,
duration_sec, sample_rate, channels, wav_status, downloaded_at
```

## 用途

- 作为单词的**参考发音**：显示/播放、参考 Mel、TTS 对照。
- 不能当“学生发音对错”标签；标签仍来自自采标注。
- 可和 `data/words.csv` 的 `word_id` 对齐，后续做“参考 Mel + 学生 Mel”的词条件化模型。

## 版权

音频由 Google 词典静态端点提供，版权很可能归 Google/Oxford。**仅用于本地研究/教学，不要重新分发，不要把 `data/reference/` 提交到公开仓库**。脚本只负责下载，不授予任何许可。

`data/reference/` 已在 `.gitignore` 中。
