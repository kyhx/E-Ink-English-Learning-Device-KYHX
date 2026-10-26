# 动画索引

三个视频都是 1920×1080、30 fps、无旁白（文字解释），用 HyperFrames 从 HTML/GSAP 渲染。

| 视频 | 时长 | 内容 | 文件 |
| --- | --- | --- | --- |
| **模型训练讲解** | 78 s | 概念：声音→Mel→CNN→训练→换人验证→int8 | [`model-training/renders/model-training-explainer.mp4`](model-training/renders/model-training-explainer.mp4) |
| **一次真实训练回放** | 74 s | 实际 2000 条数据的 loss/accuracy/混淆矩阵/样本 | [`training-run/renders/training-run-explainer.mp4`](training-run/renders/training-run-explainer.mp4) |
| **CNN 看到了什么** | 44 s | 真实激活图 + Grad-CAM + TN/TP 对比 | [`cnn-visual/renders/cnn-visual-explainer.mp4`](cnn-visual/renders/cnn-visual-explainer.mp4) |

每个目录都有 `README.md`（内容/数据来源/复现命令）和 `renders/preview.png`。

## 怎么选

- 给完全不懂 ML 的人：先看 **model-training**。
- 组会/答辩讲“这次训练到底怎么样”：看 **training-run**。
- 讲“CNN 是不是真的在看发音”：看 **cnn-visual**。

## 重新渲染

```powershell
cd docs/animations/<name>
npx --yes hyperframes render -o renders/<name>-explainer.mp4 -q delivery -w 4
```

如果 `npx` 找不到 `pwsh`，先把真实 PowerShell 放到 PATH 前面（见各目录 README）。

## 注意

- `model-training` 里的 1,330 参数/0.71→0.39 是玩具数据，不是论文结果。
- `training-run` / `cnn-visual` 用的是 speechocean762 句级代理任务，不是最终单词卡准确率。
