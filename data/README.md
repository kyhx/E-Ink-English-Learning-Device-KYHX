# 数据目录

```text
data/
├── words.csv          # 50 词 + 中文释义（提交）
├── collected/         # 自采 WAV + labels.csv（gitignore）
└── reference/         # Oxford 参考发音（gitignore，版权音频）
```

## words.csv

```csv
word_id,word,meaning,ipa
0,apple,苹果,
```

- 与固件 `main/word_view.c` 的 50 词演示表一致。
- `ipa` 目前留空；后续可用 CMUdict/词典补。

## collected/

采集 GUI 写入：

```text
data/collected/v1.0/
├── labels.csv
└── audio/
    └── S01_00_<timestamp>.wav
```

`labels.csv`：

```csv
utt_id,file,speaker_id,word_id,label,error_type,notes,split
```

- `label`：1=正确，0=错误
- `error_type`：替换/吞音/加音/重音/其他，仅分析用
- `split`：可留空；训练时按说话人自动划分

采集协议：[`docs/data_collection.md`](../docs/data_collection.md)。

## reference/

Oxford 参考发音：

```text
data/reference/oxford/
├── mp3/gb|us/*.mp3
├── wav/gb|us/*.wav       # 16 kHz mono
├── metadata.csv
└── missing.csv
```

**版权音频，仅本地研究/教学，不要提交或再分发。** 下载脚本：[`tools/reference/fetch_oxford_audio.py`](../tools/reference/fetch_oxford_audio.py)。

## 不进 Git

`data/collected/`、`data/reference/` 已在 `.gitignore`；`build/` 全部忽略。
