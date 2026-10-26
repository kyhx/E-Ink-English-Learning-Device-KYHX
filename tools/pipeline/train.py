#!/usr/bin/env python3
"""Training loop + metrics for the tiny mel CNN."""
from __future__ import annotations

import copy
import json
from pathlib import Path

import numpy as np


def _imports():
    import torch
    from torch.utils.data import DataLoader

    return torch, DataLoader


def predict_proba(model, x: np.ndarray, batch_size: int = 64) -> np.ndarray:
    torch, _ = _imports()
    model.eval()
    outs = []
    with torch.no_grad():
        for i in range(0, len(x), batch_size):
            xb = torch.from_numpy(x[i:i + batch_size])
            outs.append(torch.softmax(model(xb), dim=1).cpu().numpy())
    return np.concatenate(outs, axis=0)


def compute_metrics(y_true: np.ndarray, proba: np.ndarray, threshold: float = 0.5) -> dict:
    from sklearn.metrics import (
        accuracy_score,
        confusion_matrix,
        f1_score,
        precision_score,
        recall_score,
        roc_auc_score,
    )

    y_true = np.asarray(y_true).astype(int)
    p1 = np.asarray(proba)[:, 1]
    y_pred = (p1 >= threshold).astype(int)
    out = {
        "n": int(len(y_true)),
        "threshold": float(threshold),
        "accuracy": float(accuracy_score(y_true, y_pred)),
        "precision": float(precision_score(y_true, y_pred, zero_division=0)),
        "recall": float(recall_score(y_true, y_pred, zero_division=0)),
        "f1": float(f1_score(y_true, y_pred, zero_division=0)),
        "confusion_matrix": confusion_matrix(y_true, y_pred).tolist(),
        "class_counts": {str(int(k)): int(v) for k, v in zip(*np.unique(y_true, return_counts=True))},
    }
    try:
        out["roc_auc"] = float(roc_auc_score(y_true, p1))
    except ValueError:
        out["roc_auc"] = None
    return out


def train_model(
    x_train: np.ndarray,
    y_train: np.ndarray,
    x_val: np.ndarray,
    y_val: np.ndarray,
    *,
    out_dir: str | Path,
    epochs: int = 30,
    batch_size: int = 16,
    lr: float = 1e-3,
    weight_decay: float = 1e-4,
    patience: int = 8,
    augment: bool = False,
    seed: int = 7,
):
    torch, DataLoader = _imports()
    from dataset import FeatureDataset
    from model import TinyMelNet, count_parameters

    torch.manual_seed(seed)
    np.random.seed(seed)
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)

    train_ds = FeatureDataset(x_train, y_train, augment=augment, seed=seed).dataset
    val_ds = FeatureDataset(x_val, y_val, augment=False, seed=seed).dataset
    train_loader = DataLoader(train_ds, batch_size=batch_size, shuffle=True, drop_last=False)
    val_loader = DataLoader(val_ds, batch_size=64, shuffle=False)

    model = TinyMelNet()
    n_params = count_parameters(model)
    print(f"model: TinyMelNet, {n_params} params")

    counts = np.bincount(y_train, minlength=2).astype(np.float32)
    weights = counts.sum() / (2.0 * np.maximum(counts, 1.0))
    criterion = torch.nn.CrossEntropyLoss(
        weight=torch.tensor(weights), label_smoothing=0.05
    )
    optimizer = torch.optim.AdamW(model.parameters(), lr=lr, weight_decay=weight_decay)
    scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(
        optimizer, mode="min", factor=0.5, patience=3
    )

    best_state = copy.deepcopy(model.state_dict())
    best_val_loss = float("inf")
    best_epoch = 0
    stale = 0
    history = []

    for epoch in range(1, epochs + 1):
        model.train()
        tr_loss, tr_n, tr_correct = 0.0, 0, 0
        for xb, yb in train_loader:
            optimizer.zero_grad()
            logits = model(xb)
            loss = criterion(logits, yb)
            loss.backward()
            optimizer.step()
            tr_loss += loss.item() * len(yb)
            tr_n += len(yb)
            tr_correct += int((logits.argmax(1) == yb).sum())

        model.eval()
        va_loss, va_n, va_correct = 0.0, 0, 0
        with torch.no_grad():
            for xb, yb in val_loader:
                logits = model(xb)
                loss = criterion(logits, yb)
                va_loss += loss.item() * len(yb)
                va_n += len(yb)
                va_correct += int((logits.argmax(1) == yb).sum())

        tr_loss /= max(1, tr_n)
        va_loss /= max(1, va_n)
        tr_acc = tr_correct / max(1, tr_n)
        va_acc = va_correct / max(1, va_n)
        scheduler.step(va_loss)

        row = {
            "epoch": epoch,
            "train_loss": round(tr_loss, 5),
            "train_acc": round(tr_acc, 4),
            "val_loss": round(va_loss, 5),
            "val_acc": round(va_acc, 4),
            "lr": round(optimizer.param_groups[0]["lr"], 8),
        }
        history.append(row)
        print(
            f"epoch {epoch:02d} | train loss {tr_loss:.4f} acc {tr_acc:.3f} | "
            f"val loss {va_loss:.4f} acc {va_acc:.3f}"
        )

        if va_loss < best_val_loss - 1e-5:
            best_val_loss = va_loss
            best_epoch = epoch
            best_state = copy.deepcopy(model.state_dict())
            stale = 0
        else:
            stale += 1
            if stale >= patience:
                print(f"early stop at epoch {epoch} (best {best_epoch})")
                break

    model.load_state_dict(best_state)
    proba = predict_proba(model, x_val)
    metrics = compute_metrics(y_val, proba)
    metrics["best_epoch"] = best_epoch
    metrics["n_params"] = n_params
    metrics["augment"] = bool(augment)

    torch.save(model.state_dict(), out / "model.pt")
    (out / "history.json").write_text(json.dumps(history, indent=2), encoding="utf-8")
    (out / "metrics.json").write_text(json.dumps(metrics, indent=2), encoding="utf-8")
    print(f"best epoch {best_epoch}, val acc {metrics['accuracy']:.3f}, F1 {metrics['f1']:.3f}")
    return model, metrics, history
