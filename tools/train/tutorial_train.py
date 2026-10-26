#!/usr/bin/env python3
r"""模型训练“ hello world ”：用玩具数据跑通一遍训练流程。

注意：这里的数据是**人造的 40x64 log-Mel 图**，只用于理解流程，
不能用于论文，也不能说明真实发音准确率。

它做了什么：
  1. 造 500 条 40x64 的“梅尔图”，两类：正确 / 错误（频率条纹位置不同）
  2. 定义一个小 CNN：Conv8 -> Conv16 -> 全局平均池化 -> 2 类
  3. 训练 15 个 epoch，每轮打印 train loss / train acc / val acc
  4. 保存 PyTorch 模型，并导出 ONNX

运行：
    .\.venv-train\Scripts\python.exe tools\train\tutorial_train.py

输出：
    build/tutorial_model.pt
    build/tutorial_model.onnx
    build/tutorial_mels.png
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, TensorDataset

# Windows 控制台默认 GBK，先切 UTF-8，避免中文/符号报错
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

MEL_H, MEL_W = 40, 64
N_TRAIN, N_VAL = 400, 100
BATCH, EPOCHS = 32, 15
SEED = 7
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build"
OUT.mkdir(exist_ok=True)


def make_dataset(n: int, rng: np.random.Generator) -> tuple[np.ndarray, np.ndarray]:
    """造两类玩具梅尔图。

    类别 0（正确）：在固定频率位置有 4 条水平谐波条纹。
    类别 1（错误）：条纹整体偏移 + 部分条纹缺失，模拟“音素不对”。
    每张图再加随机增益、噪声、时间平移，避免模型只记住一个固定模板。
    """
    x = np.zeros((n, 1, MEL_H, MEL_W), dtype=np.float32)
    y = np.zeros((n,), dtype=np.int64)

    for i in range(n):
        label = int(rng.integers(0, 2))
        base = np.array([6, 12, 18, 24])  # 谐波所在的 Mel bin
        if label == 1:
            base = base + int(rng.integers(2, 5))  # 整体偏移
            drop = int(rng.integers(0, 2))
            if drop:
                base = np.delete(base, int(rng.integers(0, len(base))))

        img = rng.normal(0.0, 0.05, size=(MEL_H, MEL_W)).astype(np.float32)
        for b in base:
            b = int(np.clip(b, 0, MEL_H - 1))
            width = int(rng.integers(1, 3))
            for bb in range(max(0, b - width), min(MEL_H, b + width + 1)):
                img[bb, :] += 1.0

        # 随机时间平移、增益、局部遮挡
        shift = int(rng.integers(-6, 7))
        img = np.roll(img, shift, axis=1)
        img *= float(rng.uniform(0.7, 1.3))
        if rng.random() < 0.5:
            f0 = int(rng.integers(0, MEL_H - 6))
            t0 = int(rng.integers(0, MEL_W - 10))
            img[f0:f0 + 6, t0:t0 + 10] *= 0.3

        # 标准化到均值 0 / 方差 1，模拟“每句 CMVN”
        img = (img - img.mean()) / (img.std() + 1e-6)
        x[i, 0] = img
        y[i] = label

    return x, y


class TinyMelNet(nn.Module):
    """故意做得很小：约 1.5k 参数，ESP32-S3 完全跑得动。"""

    def __init__(self) -> None:
        super().__init__()
        self.features = nn.Sequential(
            nn.Conv2d(1, 8, 3, padding=1),
            nn.BatchNorm2d(8),
            nn.ReLU(),
            nn.MaxPool2d(2),
            nn.Conv2d(8, 16, 3, padding=1),
            nn.BatchNorm2d(16),
            nn.ReLU(),
            nn.MaxPool2d(2),
        )
        self.head = nn.Sequential(
            nn.AdaptiveAvgPool2d(1),
            nn.Flatten(),
            nn.Linear(16, 2),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.head(self.features(x))


def accuracy(logits: torch.Tensor, y: torch.Tensor) -> float:
    return float((logits.argmax(dim=1) == y).float().mean())


def main() -> int:
    torch.manual_seed(SEED)
    np.random.seed(SEED)
    rng = np.random.default_rng(SEED)

    # 1) 数据：真实项目里这里换成“读 WAV -> log-Mel -> 40x64”
    x_train, y_train = make_dataset(N_TRAIN, rng)
    x_val, y_val = make_dataset(N_VAL, rng)
    print(f"train {x_train.shape}, val {x_val.shape}, label 0/1 = "
          f"{int((y_train == 0).sum())}/{int((y_train == 1).sum())}")

    train_loader = DataLoader(
        TensorDataset(torch.from_numpy(x_train), torch.from_numpy(y_train)),
        batch_size=BATCH, shuffle=True,
    )
    xv = torch.from_numpy(x_val)
    yv = torch.from_numpy(y_val)

    # 2) 模型 + 损失 + 优化器
    model = TinyMelNet()
    n_params = sum(p.numel() for p in model.parameters())
    print(f"model parameters: {n_params}")
    criterion = nn.CrossEntropyLoss()
    optimizer = torch.optim.AdamW(model.parameters(), lr=1e-3, weight_decay=1e-4)

    # 3) 训练循环：每个 epoch = 把所有 batch 过一遍 + 在验证集上评估
    for epoch in range(1, EPOCHS + 1):
        model.train()
        total_loss, total_n, total_correct = 0.0, 0, 0
        for xb, yb in train_loader:
            optimizer.zero_grad()
            logits = model(xb)
            loss = criterion(logits, yb)
            loss.backward()
            optimizer.step()

            total_loss += loss.item() * len(yb)
            total_n += len(yb)
            total_correct += int((logits.argmax(1) == yb).sum())

        model.eval()
        with torch.no_grad():
            val_logits = model(xv)
            val_acc = accuracy(val_logits, yv)
            val_loss = float(criterion(val_logits, yv))

        print(f"epoch {epoch:02d} | train loss {total_loss / total_n:.4f} "
              f"acc {total_correct / total_n:.3f} | val loss {val_loss:.4f} "
              f"acc {val_acc:.3f}")

    # 4) 保存：.pt 给 PyTorch，.onnx 给后面的 TFLite 转换
    pt_path = OUT / "tutorial_model.pt"
    onnx_path = OUT / "tutorial_model.onnx"
    torch.save(model.state_dict(), pt_path)

    model.eval()
    dummy = torch.zeros(1, 1, MEL_H, MEL_W)
    torch.onnx.export(
        model, dummy, str(onnx_path),
        input_names=["mel"], output_names=["logits"],
        opset_version=18, do_constant_folding=True,
    )
    print(f"saved {pt_path}")
    print(f"saved {onnx_path} ({onnx_path.stat().st_size} bytes)")

    # 5) 画 8 张样本，直观看看“两类图”的差别
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt

        fig, axes = plt.subplots(2, 4, figsize=(10, 4))
        for i, ax in enumerate(axes.ravel()):
            ax.imshow(x_train[i, 0], aspect="auto", origin="lower", cmap="magma")
            ax.set_title(f"label={int(y_train[i])}")
            ax.set_xticks([])
            ax.set_yticks([])
        fig.tight_layout()
        png = OUT / "tutorial_mels.png"
        fig.savefig(png, dpi=120)
        print(f"saved {png}")
    except Exception as exc:  # 画图失败不影响训练
        print(f"plot skipped: {exc}")

    print("\n这一步只证明流程通了；下一把把 make_dataset() 换成真实 WAV 即可。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
