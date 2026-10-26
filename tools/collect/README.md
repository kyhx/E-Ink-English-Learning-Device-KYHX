# 设备端采集固件（M4 准备版）

这版固件把 M2 的录音页换成 **5 键采集页**，直接使用板载 MS4030，不再依赖 PC 麦克风。

采集结果：

```text
/storage/collect/
├── labels.csv
└── audio/
    └── S01_W00_L1_T01_<epoch><ms>.wav
```

`labels.csv` 列：

```csv
utt_id,file,speaker_id,word_id,label,error_type,notes,split,take,ts
S01_W00_L1_T01_...,audio/S01_W00_L1_T01_....wav,S01,0,1,,device,,1,...
```

`file`、`speaker_id`、`word_id`、`label` 与 `tools/pipeline/dataset.py` 兼容；`take/ts` 是额外列，训练脚本会忽略。

## 1. 烧录

先编译：

```powershell
. 'F:\esp\v6.1\esp-idf\export.ps1'
pwsh -File tools\collect\build_collect_firmware.ps1
```

生成合并镜像：

```text
build/collect/E-Ink-collect.bin
```

当前构建：app 395,664 B（0x60990），合并镜像 461,200 B；
SHA256 `6ad1b596c672f8cea35eb7688dfada0b266ae415be8a0c9f8c429e9d4eb3d628`。

烧录（只覆盖 bootloader/分区表/app，不动 10 MB storage）：

```powershell
python -m esptool --chip esp32s3 -p COM4 -b 460800 write-flash 0x0 build\collect\E-Ink-collect.bin
```

> 这版把 `CONFIG_APP_MODE_COLLECT=y` 编进独立 `build-collect/`，不改变默认 M2 固件。

## 2. 按键

| 按键 | 作用 |
| --- | --- |
| `PREV` / `NEXT` | 上一个/下一个单词（50 词，切词时 take 回 1） |
| `SYNC` 短按 | 正确 `L1` / 错误 `L0` 翻转 |
| `SYNC` 长按（800 ms） | 说话人 `S01..S08` 循环 |
| `RECORD` | 录 2 s；VAD 通过才保存，否则显示 `NO SPEECH` |
| `PLAY` | 回放最后一条 |

屏幕显示：

```text
COLLECT
S02 W12 L1 T01
      apple
```

每保存一条，`take` 自动 +1（1→2→…→9→1）。`take` 只用于区分同一人/同一词的多次录音，不参与训练。

## 3. PC 同步

依赖：`pyserial`（`C:\venv-eink` 已装）。

```powershell
# 看设备状态
C:\venv-eink\Scripts\python.exe tools\collect\device_collect.py --port COM4 info

# 下载全部 WAV + labels.csv，成功后删除设备 WAV 释放空间
C:\venv-eink\Scripts\python.exe tools\collect\device_collect.py --port COM4 sync --delete

# PC 触发录一条：设置 S02 / 词 12 / 正确，然后说 2 s
C:\venv-eink\Scripts\python.exe tools\collect\device_collect.py --port COM4 record `
  --speaker S02 --word 12 --label 1 --delete
```

默认输出：

```text
data/collected/device/v1.0/
├── labels.csv
└── audio/
```

`sync` 会把设备 `labels.csv` 的 `file` 改成 PC 本地 `audio/...` 路径，并合并不重复的 `utt_id`。

> 10 MB storage 大约能放 150 条 2 s WAV。建议每 80–100 条执行一次 `sync --delete`。

## 4. 采集后检查

```powershell
C:\venv-eink\Scripts\python.exe tools\pipeline\check_collected.py `
  --data-dir data\collected\device\v1.0
```

## 5. 实测（2026-10-26）

用 PC TTS 念 `apple` 做了一次端到端冒烟：

- 设备保存 `S03_W05_L1_T01_1793052121991.wav`，23,840 帧（VAD 裁到 1.49 s），峰值 1701、RMS 297；
- `labels.csv` 自动追加 `S03,5,1,...,take=1,ts=1793052121`；
- `device_collect.py sync --delete` 下载成功，设备文件被删除；
- `check_collected.py` 返回 `PASS`，只提示时长 1.49 s（低于 2 s，训练管线会补齐）。

TTS 音量小、距离远，峰值只有 −25.7 dBFS；真人 10–20 cm 应明显更高。正式采集后看 `check_collected.py` 的削顶/静音警告。

## 6. 串口命令（调试用）

```text
bench collect info
bench collect set S02 12 1
bench collect last
bench collect rec
bench collect list
bench collect csv
bench collect delete /storage/collect/audio/<name>.wav
```

`bench collect rec` 只是“请求录音”，由采集页主循环真正启动；录完用 `bench collect last` 拿路径，再用现有 `bench audio dump <path>` 下载。

## 7. 注意

- 当前固件仍用编译时间做临时时钟；`ts` 不是真实 UTC，但 `utt_id` 足够唯一。M7 接 SNTP 后校正。
- `label` 是人工判断，建议正确/错误都保留；不要把读错的重录成正确。
- 设备端 VAD 只去掉首尾静音，不改变发音内容；保存的是裁剪后的 WAV。
- 词表来自 `main/word_view.c` 的 50 个演示词；换词表时同时更新 `data/words.csv` 和该数组。
