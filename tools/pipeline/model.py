#!/usr/bin/env python3
"""Tiny CNN for 40x64 log-Mel binary classification.

This is intentionally small: the deployment target is ESP32-S3 with TFLM.
Start with ~5k parameters; increase only if the training loss clearly
underfits.  The first version is mel-only; word conditioning can be added
later without changing the feature contract.
"""
from __future__ import annotations

import torch
import torch.nn as nn


class TinyMelNet(nn.Module):
    def __init__(self, dropout: float = 0.2) -> None:
        super().__init__()
        self.features = nn.Sequential(
            nn.Conv2d(1, 8, 3, padding=1),
            nn.BatchNorm2d(8),
            nn.ReLU(),
            nn.MaxPool2d(2),          # 40x64 -> 20x32
            nn.Conv2d(8, 16, 3, padding=1),
            nn.BatchNorm2d(16),
            nn.ReLU(),
            nn.MaxPool2d(2),          # 20x32 -> 10x16
            nn.Conv2d(16, 32, 3, padding=1),
            nn.BatchNorm2d(32),
            nn.ReLU(),
            nn.AdaptiveAvgPool2d(1),
            nn.Flatten(),
            nn.Dropout(dropout),
        )
        self.head = nn.Linear(32, 2)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.head(self.features(x))


def count_parameters(model: nn.Module) -> int:
    return sum(p.numel() for p in model.parameters() if p.requires_grad)


if __name__ == "__main__":
    m = TinyMelNet()
    print(f"parameters: {count_parameters(m)}")
    print(m(torch.zeros(1, 1, 40, 64)).shape)
