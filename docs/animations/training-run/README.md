# 一次真实训练回放（74 秒）

用 `speechocean762` 2000 条平衡子集的实际训练结果做可视化，不是概念动画。

**视频**：[`renders/training-run-explainer.mp4`](renders/training-run-explainer.mp4)  
**预览**：[`renders/preview.png`](renders/preview.png)

## 内容

| 时间 | 内容 |
| --- | --- |
| 0–6 s | 标题：2000 条真实语音、20 epochs、6,066 参数 |
| 6–14 s | 数据：官方 125/125 说话人零重叠；1000/1000 句；正确/需改进各 500 |
| 14–34 s | 实际 loss/accuracy 曲线；best epoch 13；val acc 69.7%；F1 69.4%；AUC 0.759 |
| 34–44 s | 混淆矩阵 `[[354,146],[157,343]]`；precision 70.1%、recall 68.6% |
| 44–58 s | 3 条真实样本：TP p=0.913、TN p=0.062、8 岁 FP p=0.079；儿童 61.3% vs 成人 77.5% |
| 58–66 s | 26.9 KB → 11.5 KB int8；98.3% 标签一致；70.0% int8 accuracy |
| 66–74 s | 结论：管线通了，但还不是单词卡模型；下一步自采 600 条 + 参考发音 |

## 数据来源

- 数据：`speechocean762`，CC BY 4.0，2000 条平衡子集
- 训练记录：`build/pipeline_speechocean762/history.json`
- 指标：`build/pipeline_speechocean762/metrics.json`
- 逐条预测：`build/pipeline_speechocean762/val_predictions.csv`
- 公开数据报告：[`docs/public_test_speechocean762.md`](../../public_test_speechocean762.md)

三条样本（真实音频）：

| 类型 | utt | 说话人 | 真实分 | p(correct) |
| --- | --- | --- | --- | --- |
| TP | 020310141 | 2031 / 成人 | 9 | 0.913 |
| TN | 096260016 | 9626 / 成人 | 3 | 0.062 |
| FP | 020300059 | 2030 / 8 岁 | 9 | 0.079 |

Mel 图由 `features.extract_file` 从原始 WAV 提取后保存为 `assets/mel_*.png`。

## 重新渲染

```powershell
npx --yes hyperframes render -o renders/training-run-explainer.mp4 -q delivery -w 4
```

如果 `npx` 找不到 `pwsh`，先把真实 PowerShell 放到 PATH 前面：

```powershell
$env:Path = "$PSHOME;F:\CodeTool\Node;C:\Users\Administrator\AppData\Local\Microsoft\WinGet\Links;C:\Windows\System32;C:\Windows"
```

## 说明

- 这是**句级评分代理任务**，不是 2 秒单词卡任务；69.7% 不能当最终单词准确率。
- 2 秒中心裁剪会丢掉长句开头/结尾，是时长相关误差的主要混杂因素。
- 视频里所有数字都来自本次实际训练，不是示意值。
- 模型仍是 mel-only，没有使用 Oxford 参考发音；词条件化是下一步。
