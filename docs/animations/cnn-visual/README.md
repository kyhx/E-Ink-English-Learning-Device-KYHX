# CNN 看到了什么（44 秒）

用真实 `model.pt` 的激活值和 Grad-CAM 做可视化，不是示意图。

**视频**：[`renders/cnn-visual-explainer.mp4`](renders/cnn-visual-explainer.mp4)  
**预览**：[`renders/preview.png`](renders/preview.png)

## 内容

| 时间 | 内容 |
| --- | --- |
| 0–5 s | CNN 内部：8 → 16 → 32 通道 |
| 5–12 s | TN 样本的 40×64 Mel；真实 3 分，p(correct)=0.062 |
| 12–20 s | conv1：8×40×64，局部能量/边界 |
| 20–28 s | conv2：16×20×32，局部条纹组合 |
| 28–37 s | conv3：32×10×16 + Grad-CAM，上采样回 40×64 |
| 37–44 s | 同一套权重，TN 与 TP 关注区域不同；但没有词条件化 |

## 真实数据

- 模型：`build/pipeline_speechocean762/model.pt`，6,066 参数
- TN 样本：`096260016`，说话人 9626，真实 3 分，p(correct)=0.062
- TP 样本：`020310141`，说话人 2031，真实 9 分，p(correct)=0.913
- 输入：40×64 log-Mel；2 秒中心裁剪

真实激活形状：

```text
conv1: [1, 8, 40, 64]
conv2: [1, 16, 20, 32]
conv3: [1, 32, 10, 16]
Grad-CAM: [10, 16] -> 上采样到 40×64
```

## 方法

1. 在 `features[2] / [6] / [10]`（conv1/2/3 后的 ReLU）注册 forward hook；
2. 对 conv3 注册 forward + full backward hook，目标类取模型预测类；
3. Grad-CAM 权重 = 梯度在空间上的平均，`cam = ReLU(sum(w * A))`；
4. 归一化后双线性上采样回 40×64，叠加到 Mel 上。

复现：

```powershell
.\.venv-train\Scripts\python.exe tools\pipeline\visualize_cnn.py `
  --model build\pipeline_speechocean762\model.pt `
  --wav build\data\speechocean762\WAVE\SPEAKER9626\096260016.WAV `
  --out build\cnn_vis
```

输出：`mel.png`、`conv1.png`、`conv2.png`、`conv3.png`、`gradcam.png`、`meta.json`。

## 重新渲染

```powershell
cd docs/animations/cnn-visual
npx --yes hyperframes render -o renders/cnn-visual-explainer.mp4 -q delivery -w 4
```

## 解释边界

- Grad-CAM 高亮的是**对当前预测贡献大的时频区域**，是相关性解释，不是因果解释。
- 它不能证明模型学会了音素、重音或发音规则；当前模型也没有词 ID/参考 Mel 条件化。
- 10×16 的 conv3 上采样到 40×64 后必然是块状的；不要把它当成像素级标注。
- 视频里的 p 值和真实分都来自这次公开数据训练，不是示意值。
