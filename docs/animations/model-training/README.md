# 模型训练讲解动画（HyperFrames）

一个 78 秒、1920×1080、30 fps 的中文讲解动画，用最少的数学解释这个项目的模型训练流程。

**视频**：[`renders/model-training-explainer.mp4`](renders/model-training-explainer.mp4)（约 6.7 MB）
**预览**：[`renders/preview.png`](renders/preview.png)（9 帧拼接）

## 章节

| 时间 | 内容 |
| --- | --- |
| 0–6 s | 标题：模型训练到底在做什么 |
| 6–16 s | 声音 → 40×64 log-Mel 图 |
| 16–26 s | 打标签：正确 / 错误；玩具梅尔图 |
| 26–38 s | 3×3 卷积核在图上滑动，小 CNN 找条纹 |
| 38–52 s | 训练循环：输入 → 预测 → loss → 更新；loss 下降 |
| 52–62 s | 按说话人切分：训练 6 人 / 测试 2 人 |
| 62–72 s | float32 → int8，ESP32-S3 + TFLM 本地推理 |
| 72–78 s | 你不需要先手推公式；先跑通 20 条真实录音 |

> 动画里的 1,330 参数、0.71→0.39、0.57→0.94 都来自 `tools/train/tutorial_train.py` 的**玩具数据**，只用于解释流程，不是论文实验结果。真实数据、指标以 [`docs/thesis/开题报告.md`](../../thesis/开题报告.md) 和后续实验为准。

## 环境

- Node.js ≥ 22（本机 v24.21.0）
- FFmpeg（本机 9.0.2，winget 安装）
- HyperFrames 0.8.198（`npx hyperframes`）
- Chrome（HyperFrames `doctor` 已通过）

## 重新渲染

在**本目录**执行：

```powershell
npx --yes hyperframes render -o renders/model-training-explainer.mp4 -q delivery -w 4
```

如果 `npx` 找不到 `pwsh`（Windows App Execution Alias 问题），先把真实 PowerShell 放到 PATH 前面：

```powershell
$env:Path = "$PSHOME;F:\CodeTool\Node;C:\Users\Administrator\AppData\Local\Microsoft\WinGet\Links;C:\Windows\System32;C:\Windows"
npx --yes hyperframes render -o renders/model-training-explainer.mp4 -q delivery -w 4
```

## 修改内容

- 文案/布局/时间线：`index.html`（GSAP timeline，`window.__timelines["main"]`）
- 玩具梅尔图：`assets/tutorial_mels.png`，由 `tools/train/tutorial_train.py` 生成
- GSAP：`assets/gsap.min.js`（本地化，避免渲染时依赖 CDN）

## 注意

- 动画是**无声**的，所有解释都在画面上；适合课堂/答辩前快速理解，不适合作为论文实验证据。
- GSAP 使用其 Standard License；若对外分发项目，请保留 `assets/gsap.min.js` 的版权头并遵守 GSAP 许可。
- `.hyperframes/` 是 HyperFrames 的检查/渲染缓存，已加入 `.gitignore`；`renders/` 保留最终 MP4 和预览图。
