# M2 音频串口验收

`bench_audio.py` 通过 UART0（COM4，115200）调用固件里的 `bench audio ...`，用于：

- 查看 I2S/引脚/存储/最近录音；
- 连续录音并校验 20 条 WAV；
- 把设备上的 WAV 通过 `bench audio dump` 下载到 PC；
- 生成 1 kHz 测试 WAV（`bench audio gen`）。

## 依赖

```powershell
C:\venv-eink\Scripts\python.exe -m pip install pyserial
```

## 用法

```powershell
# 基本信息
C:\venv-eink\Scripts\python.exe tools\audio\bench_audio.py --port COM4 info

# 20 条 1 s 连续录音，校验文件完整性
C:\venv-eink\Scripts\python.exe tools\audio\bench_audio.py --port COM4 soak --n 20

# 录 2 s，下载为 PC 上的 WAV
C:\venv-eink\Scripts\python.exe tools\audio\bench_audio.py --port COM4 rec --ms 2000 --out build\rec.wav

# 下载设备上已有的 WAV
C:\venv-eink\Scripts\python.exe tools\audio\bench_audio.py --port COM4 dump `
  --path /storage/rec/rec_20261026_121630_232.wav --out build\rec.wav

```

## 2026-10-26 实测结论

| 项 | 结果 |
| --- | --- |
| `bench audio gen` 1 kHz | 1000 Hz，−17.8 dBFS，SNR 162.6 dB（算法自检通过） |
| `bench audio rec 2000` | 32000 帧 / 2.0 s，无削顶，启动 122 ms（含 100 ms 稳定丢弃） |
| `bench audio soak 20` | **20/20** WAV 文件头/帧数校验通过 |
| `bench audio play` | I2S 写入返回 ok；**未做听感确认** |
| 声学测试 | 曾用 duplex loopback 测到 0/1；该命令因 TX/RX 同时开时 RX 停住已移除，改用手机 1 kHz + `rec`/`analyze` |

功放未出声时先查：

1. MAX98357A 的 **5 V** 是否真的供电（COM 口供电时 5V 排针默认是输入，需桥接 `IN-OUT`）；
2. `SD`（GPIO47）是否在高电平，喇叭是否接在 **+/- 差分输出**，任一端不能接 GND；
3. 喇叭是否 4 Ω/3 W，DIN=21、BCLK=17、LRC=18 是否接对；
4. 若功放改由 3.3 V 供电，音量会明显变小，但不至于完全无声。

`bench audio selftest` 只验证“能写 WAV、能读回、能启动录音”，**不验证喇叭出声**；声学链路用手机播 1 kHz + `bench audio analyze` 或人耳确认。
