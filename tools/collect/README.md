# 串口工具（lite-29 分支）

这个分支删掉了 4.2" 采集固件和它的 PC 端同步脚本（`build_collect_firmware.ps1`、
`device_collect.py`），只保留两个不依赖固件功能的串口工具：

| 脚本 | 用途 |
| --- | --- |
| `serial_capture.py` | 打开串口，复位一次并连续打印日志，用于无人值守抓启动输出 |
| `serial_bench.py` | 逐条发送 `bench` 命令并打印回复，用于自动跑命令序列 |

## 用法

```powershell
$py='C:\Users\Administrator\.espressif\python_env\idf6.1_py3.14_env\Scripts\python.exe'

# 抓 30 秒启动日志
& $py tools\collect\serial_capture.py COM4 30

# 逐条执行命令
& $py tools\collect\serial_bench.py COM4 "bench epd1680 pattern" "bench ui page 2"
```

`serial_bench.py` 每条命令等 12 秒或输出静默 1.2 秒后继续，适合 `bench ui page`、
`bench epd1680 partial` 这类会触发刷新的命令（局刷 ~0.5 s，全刷 ~2 s）。

## 采集数据怎么办

lite 分支不再提供设备端采集页。需要采 200 条语音时用 `main` 分支的采集固件，
或者直接用 `bench audio rec` 单条录（`/storage/rec/`）。两条路都能被
`tools/pipeline/` 的训练脚本读取。
