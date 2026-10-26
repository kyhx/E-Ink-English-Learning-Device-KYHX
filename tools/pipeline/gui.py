#!/usr/bin/env python3
r"""Tkinter GUI for the model/data pipeline.

Usage:
    .\.venv-train\Scripts\python.exe tools\pipeline\gui.py

It calls `run.py` as a subprocess, streams the log, then reads metrics.json /
parity.json / report.md.  The GUI never re-implements training or conversion.
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import queue
import subprocess
import sys
import threading
import time
from pathlib import Path

import numpy as np
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from tkinter.scrolledtext import ScrolledText

from PIL import Image, ImageTk

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from dataset import class_counts, generate_toy_dataset, load_manifest  # noqa: E402
from features import (  # noqa: E402
    N_FRAMES, N_MELS, N_SAMPLES, extract_file, load_audio,
    logmel_from_waveform, pad_or_crop,
)

BG = "#0b1020"
PANEL = "#141b31"
PANEL2 = "#1b2440"
FG = "#f4f7ff"
MUTED = "#9aa7c7"
TEAL = "#5eead4"
AMBER = "#fbbf24"
PINK = "#fb7185"
LINE = "#2a3554"
FONT = "Microsoft YaHei UI"
MONO = "Consolas"


def _open_path(path: Path) -> None:
    path = Path(path)
    if not path.exists():
        messagebox.showwarning("不存在", str(path))
        return
    try:
        os.startfile(str(path))  # type: ignore[attr-defined]
    except Exception:
        messagebox.showinfo("路径", str(path))


def _size(n: int) -> str:
    if n < 1024:
        return f"{n} B"
    if n < 1024 * 1024:
        return f"{n / 1024:.1f} KB"
    return f"{n / 1024 / 1024:.2f} MB"


class App:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.proc: subprocess.Popen | None = None
        self.q: queue.Queue = queue.Queue()
        self.samples = []
        self.mel_photo: ImageTk.PhotoImage | None = None
        self.single_photo: ImageTk.PhotoImage | None = None
        self.collect_photo: ImageTk.PhotoImage | None = None
        self.toy_thread: threading.Thread | None = None
        self.collect_rows: list[dict] = []
        self.words: list[dict] = []
        self.mic_devices: list[tuple[int, str]] = []
        self.pending_audio: np.ndarray | None = None
        self.current_audio: np.ndarray | None = None
        self.current_sr = 16000
        self.selected_utt: str | None = None
        self.recording = False

        self.data_dir = tk.StringVar(value=str(ROOT / "data" / "dataset" / "v1.0"))
        self.out_dir = tk.StringVar(value=str(ROOT / "build" / "pipeline_gui"))
        self.epochs = tk.StringVar(value="25")
        self.batch = tk.StringVar(value="16")
        self.lr = tk.StringVar(value="0.001")
        self.val_speakers = tk.StringVar(value="")
        self.augment = tk.BooleanVar(value=True)
        self.no_convert = tk.BooleanVar(value=False)
        self.status = tk.StringVar(value="就绪")
        self.single_wav = tk.StringVar(value="")
        self.collect_dir = tk.StringVar(value=str(ROOT / "data" / "collected" / "v1.0"))
        self.collect_speaker = tk.StringVar(value="S01")
        self.collect_word = tk.StringVar(value="")
        self.collect_mic = tk.StringVar(value="")
        self.collect_label = tk.StringVar(value="未标注")
        self.collect_error = tk.StringVar(value="")
        self.collect_notes = tk.StringVar(value="")
        self.collect_status = tk.StringVar(value="录一条 2 秒样本，试听后标注")
        self.collect_count = tk.StringVar(value="0 条")
        self.metric_vars: dict[str, tk.StringVar] = {}
        self.parity_vars: dict[str, tk.StringVar] = {}

        self._style()
        self._header()
        self._tabs()
        self._build_collect_tab()
        self._build_data_tab()
        self._build_train_tab()
        self._build_deploy_tab()
        self.root.after(100, self._poll)
        self.root.protocol("WM_DELETE_WINDOW", self._close)

    # ---------------------------------------------------------------- style
    def _style(self) -> None:
        style = ttk.Style(self.root)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure(".", font=(FONT, 10))
        style.configure("TFrame", background=BG)
        style.configure("Panel.TFrame", background=PANEL)
        style.configure("TLabel", background=BG, foreground=FG)
        style.configure("Muted.TLabel", background=BG, foreground=MUTED)
        style.configure("Panel.TLabel", background=PANEL, foreground=FG)
        style.configure("CardValue.TLabel", background=PANEL, foreground=TEAL,
                        font=(FONT, 18, "bold"))
        style.configure("CardCap.TLabel", background=PANEL, foreground=MUTED,
                        font=(FONT, 9))
        style.configure("Title.TLabel", background=BG, foreground=FG,
                        font=(FONT, 20, "bold"))
        style.configure("Sub.TLabel", background=BG, foreground=MUTED,
                        font=(FONT, 10))
        style.configure("TButton", background=PANEL2, foreground=FG,
                        borderwidth=0, padding=(12, 7))
        style.map("TButton",
                  background=[("active", "#26335a"), ("disabled", "#1a2033")],
                  foreground=[("disabled", "#6b7797")])
        style.configure("Accent.TButton", background=TEAL, foreground="#06221f",
                        font=(FONT, 10, "bold"))
        style.map("Accent.TButton", background=[("active", "#8ff5e6")])
        style.configure("Danger.TButton", background="#3a1d2a", foreground="#ffd9e2")
        style.configure("TEntry", fieldbackground=PANEL2, foreground=FG,
                        insertcolor=FG, borderwidth=0, padding=6)
        style.configure("TCombobox", fieldbackground=PANEL2, background=PANEL2,
                        foreground=FG, arrowcolor=TEAL, padding=4)
        style.map("TCombobox",
                  fieldbackground=[("readonly", PANEL2)],
                  foreground=[("readonly", FG)],
                  selectbackground=[("readonly", PANEL2)],
                  selectforeground=[("readonly", FG)])
        self.root.option_add("*TCombobox*Listbox.background", PANEL2)
        self.root.option_add("*TCombobox*Listbox.foreground", FG)
        self.root.option_add("*TCombobox*Listbox.selectBackground", "#26405a")
        style.configure("TCheckbutton", background=BG, foreground=FG)
        style.map("TCheckbutton", background=[("active", BG)])
        style.configure("TNotebook", background=BG, borderwidth=0)
        style.configure("TNotebook.Tab", background=PANEL, foreground=MUTED,
                        padding=(18, 9), font=(FONT, 10, "bold"))
        style.map("TNotebook.Tab",
                  background=[("selected", PANEL2)],
                  foreground=[("selected", TEAL)])
        style.configure("Treeview", background=PANEL, fieldbackground=PANEL,
                        foreground=FG, rowheight=26, borderwidth=0,
                        font=(MONO, 9))
        style.configure("Treeview.Heading", background=PANEL2, foreground=MUTED,
                        relief="flat", font=(FONT, 9, "bold"))
        style.map("Treeview", background=[("selected", "#26405a")])
        style.configure("TProgressbar", background=TEAL, troughcolor=PANEL2,
                        borderwidth=0, thickness=6)

    def _header(self) -> None:
        bar = ttk.Frame(self.root, padding=(22, 16, 22, 8))
        bar.pack(fill="x")
        ttk.Label(bar, text="E-Ink 模型管线控制台", style="Title.TLabel").pack(anchor="w")
        ttk.Label(
            bar,
            text="数据 → log-Mel 40×64 → 小 CNN → ONNX → TFLite int8 → parity → C 数组",
            style="Sub.TLabel",
        ).pack(anchor="w", pady=(4, 0))
        status = ttk.Frame(self.root, padding=(22, 0, 22, 6))
        status.pack(fill="x")
        tk.Label(status, textvariable=self.status, bg=BG, fg=MUTED,
                 font=(FONT, 9)).pack(side="left")

    def _tabs(self) -> None:
        self.nb = ttk.Notebook(self.root)
        self.nb.pack(fill="both", expand=True, padx=18, pady=(4, 14))
        self.tab_collect = ttk.Frame(self.nb, padding=14)
        self.tab_data = ttk.Frame(self.nb, padding=14)
        self.tab_train = ttk.Frame(self.nb, padding=14)
        self.tab_deploy = ttk.Frame(self.nb, padding=14)
        self.nb.add(self.tab_collect, text="  采集 / 标注  ")
        self.nb.add(self.tab_data, text="  数据 / 预览  ")
        self.nb.add(self.tab_train, text="  训练 / 指标  ")
        self.nb.add(self.tab_deploy, text="  部署 / 试推理  ")

    def _card(self, parent, title: str, key: str, var_store: dict) -> tk.Frame:
        f = tk.Frame(parent, bg=PANEL, highlightbackground=LINE,
                     highlightthickness=1, padx=14, pady=10)
        v = tk.StringVar(value="—")
        var_store[key] = v
        tk.Label(f, text=title, bg=PANEL, fg=MUTED,
                 font=(FONT, 9)).pack(anchor="w")
        tk.Label(f, textvariable=v, bg=PANEL, fg=TEAL,
                 font=(FONT, 18, "bold")).pack(anchor="w", pady=(3, 0))
        return f

    # ------------------------------------------------------------- collect
    def _build_collect_tab(self) -> None:
        top = ttk.Frame(self.tab_collect)
        top.pack(fill="x")
        ttk.Label(top, text="说话人", style="Muted.TLabel").pack(side="left")
        ttk.Entry(top, textvariable=self.collect_speaker, width=9).pack(side="left", padx=(6, 14))
        ttk.Label(top, text="单词", style="Muted.TLabel").pack(side="left")
        self.word_combo = ttk.Combobox(top, textvariable=self.collect_word,
                                       state="readonly", width=34)
        self.word_combo.pack(side="left", padx=(6, 14))
        ttk.Label(top, text="麦克风", style="Muted.TLabel").pack(side="left")
        self.mic_combo = ttk.Combobox(top, textvariable=self.collect_mic,
                                      state="readonly", width=40)
        self.mic_combo.pack(side="left", padx=(6, 4), fill="x", expand=True)
        ttk.Button(top, text="刷新", command=self._refresh_mics).pack(side="left")

        toolbar = ttk.Frame(self.tab_collect)
        toolbar.pack(fill="x", pady=(10, 8))
        self.record_btn = ttk.Button(toolbar, text="● 录音 2 秒", style="Accent.TButton",
                                     command=self._record)
        self.record_btn.pack(side="left")
        ttk.Button(toolbar, text="▶ 试听", command=self._play_collect).pack(side="left", padx=4)
        ttk.Button(toolbar, text="■ 停止", command=self._stop_play).pack(side="left", padx=4)
        ttk.Button(toolbar, text="导入 WAV", command=self._import_wav).pack(side="left", padx=4)
        ttk.Button(toolbar, text="保存样本/标注", command=self._save_collect).pack(side="left", padx=4)
        ttk.Button(toolbar, text="删除选中", style="Danger.TButton",
                   command=self._delete_collect).pack(side="left", padx=4)
        tk.Label(toolbar, textvariable=self.collect_status, bg=BG, fg=AMBER,
                 font=(FONT, 9)).pack(side="left", padx=12)

        body = ttk.Frame(self.tab_collect)
        body.pack(fill="both", expand=True)

        left = tk.Frame(body, bg=PANEL, highlightbackground=LINE,
                        highlightthickness=1, padx=12, pady=12, width=390)
        left.pack(side="left", fill="y")
        left.pack_propagate(False)
        tk.Label(left, text="试听 / Mel", bg=PANEL, fg=MUTED,
                 font=(FONT, 9)).pack(anchor="w")
        self.collect_mel = tk.Label(left, bg="#0e1729", text="录音或导入后显示",
                                    fg=MUTED, font=(FONT, 9))
        self.collect_mel.pack(pady=(8, 6))
        self.collect_info = tk.Label(left, text="40×64 log-Mel · 16 kHz",
                                     bg=PANEL, fg=MUTED, font=(MONO, 9),
                                     justify="left", wraplength=340)
        self.collect_info.pack(anchor="w")

        ann = ttk.Frame(left)
        ann.pack(fill="x", pady=(14, 0))
        ttk.Label(ann, text="标签", style="Panel.TLabel").pack(side="left")
        ttk.Combobox(ann, textvariable=self.collect_label, state="readonly",
                     values=["未标注", "正确", "错误"], width=10).pack(side="left", padx=(6, 14))
        ttk.Label(ann, text="错误类型", style="Panel.TLabel").pack(side="left")
        ttk.Combobox(ann, textvariable=self.collect_error, state="readonly",
                     values=["", "替换", "吞音", "加音", "重音", "其他"],
                     width=10).pack(side="left", padx=(6, 0))

        quick = ttk.Frame(left)
        quick.pack(fill="x", pady=(8, 0))
        ttk.Button(quick, text="正确 (1)", style="Accent.TButton",
                   command=lambda: self.collect_label.set("正确")).pack(side="left", fill="x", expand=True)
        ttk.Button(quick, text="错误 (0)", command=lambda: self.collect_label.set("错误")).pack(
            side="left", fill="x", expand=True, padx=(6, 0)
        )

        notes = ttk.Frame(left)
        notes.pack(fill="x", pady=(8, 0))
        ttk.Label(notes, text="备注", style="Panel.TLabel").pack(side="left")
        ttk.Entry(notes, textvariable=self.collect_notes).pack(side="left", fill="x",
                                                               expand=True, padx=(6, 0))

        tk.Label(left, text="流程：录音 → 试听 → 选“正确/错误” → 保存样本。\n"
                            "已有样本：在右侧选中 → 改标签/错误类型 → 保存标注。",
                 bg=PANEL, fg=MUTED, font=(FONT, 8), justify="left").pack(anchor="w", pady=(12, 0))

        right = ttk.Frame(body)
        right.pack(side="left", fill="both", expand=True, padx=(12, 0))
        hdr = ttk.Frame(right)
        hdr.pack(fill="x")
        ttk.Label(hdr, text="已采集样本", style="Muted.TLabel").pack(side="left")
        ttk.Label(hdr, textvariable=self.collect_count, style="Muted.TLabel").pack(side="right")
        cols = ("utt", "speaker", "word", "label", "error", "notes")
        self.collect_tree = ttk.Treeview(right, columns=cols, show="headings", height=20)
        for c, w in zip(cols, (150, 80, 80, 60, 70, 220)):
            self.collect_tree.heading(c, text={"utt": "utt_id", "speaker": "speaker",
                                               "word": "word", "label": "label",
                                               "error": "error", "notes": "notes"}[c])
            self.collect_tree.column(c, width=w, anchor="w")
        self.collect_tree.pack(side="left", fill="both", expand=True, pady=(6, 0))
        sb = ttk.Scrollbar(right, orient="vertical", command=self.collect_tree.yview)
        sb.pack(side="right", fill="y", pady=(6, 0))
        self.collect_tree.configure(yscrollcommand=sb.set)
        self.collect_tree.bind("<<TreeviewSelect>>", self._on_collect_select)

        self._load_words()
        self._load_collected()
        self._refresh_mics()

    def _load_words(self) -> None:
        path = ROOT / "data" / "words.csv"
        rows: list[dict] = []
        if path.exists():
            with path.open(encoding="utf-8-sig", newline="") as f:
                for r in csv.DictReader(f):
                    try:
                        wid = int(r.get("word_id", ""))
                    except ValueError:
                        continue
                    rows.append({
                        "word_id": wid,
                        "word": str(r.get("word", "")).strip(),
                        "meaning": str(r.get("meaning", "")).strip(),
                        "ipa": str(r.get("ipa", "")).strip(),
                    })
        if not rows:
            # Fallback to the firmware's 50-word demo list.
            words = ("apple book cat dog eat fish good hand idea jump kind love moon name open "
                     "park quiz rain sun tree unit very word yard zero bird blue cold door easy "
                     "face game home iron just keep light move nice only play quick read ship "
                     "time upon view warm year zoom").split()
            rows = [{"word_id": i, "word": w, "meaning": "", "ipa": ""} for i, w in enumerate(words)]
        self.words = rows
        self._word_by_id = {r["word_id"]: r for r in rows}
        self._word_display = [
            f"{r['word_id']:02d}  {r['word']}" + (f"  {r['meaning']}" if r["meaning"] else "")
            for r in rows
        ]
        self.word_combo.configure(values=self._word_display)
        if self._word_display:
            self.word_combo.current(0)
            self.collect_word.set(self._word_display[0])

    def _selected_word(self) -> dict | None:
        idx = self.word_combo.current()
        if 0 <= idx < len(self.words):
            return self.words[idx]
        return None

    def _refresh_mics(self) -> None:
        try:
            import sounddevice as sd

            devs = sd.query_devices()
            self.mic_devices = [
                (i, f"{d['name']} ({int(d['max_input_channels'])} in)")
                for i, d in enumerate(devs) if int(d["max_input_channels"]) > 0
            ]
            values = [f"{i}: {name}" for i, name in self.mic_devices]
            self.mic_combo.configure(values=values)
            if values:
                default_in = sd.default.device[0]
                pick = next((v for v in values if v.startswith(f"{default_in}:")), values[0])
                self.collect_mic.set(pick)
                self.collect_status.set("麦克风就绪；点“录音 2 秒”")
                self.record_btn.configure(state="normal")
            else:
                self.collect_status.set("没有找到输入设备")
                self.record_btn.configure(state="disabled")
        except Exception as exc:  # noqa: BLE001
            self.mic_devices = []
            self.mic_combo.configure(values=[])
            self.collect_mic.set("")
            self.collect_status.set(f"录音不可用：{exc}（仍可导入 WAV）")
            self.record_btn.configure(state="disabled")

    def _mic_index(self) -> int | None:
        s = self.collect_mic.get()
        if ":" in s:
            try:
                return int(s.split(":", 1)[0])
            except ValueError:
                return None
        return None

    def _record(self) -> None:
        if self.recording:
            return
        idx = self._mic_index()
        if idx is None:
            messagebox.showwarning("没有麦克风", "先刷新/选择一个输入设备")
            return
        try:
            import sounddevice as sd

            dev = sd.query_devices(idx)
            native_sr = int(dev["default_samplerate"])
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("录音失败", str(exc))
            return

        self.recording = True
        self.record_btn.configure(state="disabled")
        self.collect_status.set("录音中… 请自然读单词（2 秒）")

        def work() -> None:
            try:
                rec = sd.rec(
                    int(2.0 * native_sr),
                    samplerate=native_sr,
                    channels=1,
                    device=idx,
                    dtype="float32",
                )
                sd.wait()
                y = np.asarray(rec[:, 0], dtype=np.float32)
                if native_sr != 16000:
                    import librosa
                    y = librosa.resample(y, orig_sr=native_sr, target_sr=16000,
                                         res_type="soxr_hq")
                y = y - float(np.mean(y))
                peak = float(np.max(np.abs(y))) if len(y) else 0.0
                if peak > 0.99:
                    y = y / peak * 0.99
                self.q.put(("record_done", (y.astype(np.float32), 16000)))
            except Exception as exc:  # noqa: BLE001
                self.q.put(("record_error", str(exc)))

        threading.Thread(target=work, daemon=True).start()

    def _play_collect(self) -> None:
        if self.current_audio is None:
            return
        try:
            import sounddevice as sd
            sd.stop()
            sd.play(self.current_audio, self.current_sr)
            self.collect_status.set("播放中…")
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("播放失败", str(exc))

    def _stop_play(self) -> None:
        try:
            import sounddevice as sd
            sd.stop()
            self.collect_status.set("已停止")
        except Exception:
            pass

    def _import_wav(self) -> None:
        p = filedialog.askopenfilename(
            initialdir=str(ROOT),
            filetypes=[("WAV", "*.wav"), ("Audio", "*.wav *.flac *.ogg"), ("All", "*.*")],
        )
        if not p:
            return
        try:
            y = load_audio(p)
            self._set_pending(y)
            self.collect_status.set(f"已导入 {Path(p).name}；试听后标注")
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("导入失败", str(exc))

    def _set_pending(self, y: np.ndarray) -> None:
        y = pad_or_crop(np.asarray(y, dtype=np.float32), N_SAMPLES)
        self.pending_audio = y
        self.current_audio = self.pending_audio
        self.current_sr = 16000
        self.selected_utt = None
        self.collect_label.set("未标注")
        self.collect_error.set("")
        self.collect_notes.set("")
        self._preview_collect(self.pending_audio)

    def _preview_collect(self, y: np.ndarray) -> None:
        try:
            f = logmel_from_waveform(y, 16000, do_vad=True)
            self._show_mel(f, self.collect_mel, "collect_photo")
            dur = len(y) / 16000.0
            peak = float(np.max(np.abs(y))) if len(y) else 0.0
            self.collect_info.config(
                text=f"{dur:.2f}s · peak {peak:.2f} · {f.shape} · "
                     f"range [{f.min():.2f}, {f.max():.2f}]"
            )
        except Exception as exc:  # noqa: BLE001
            self.collect_info.config(text=f"Mel 提取失败：{exc}")

    def _label_value(self) -> int | None:
        return {"正确": 1, "错误": 0}.get(self.collect_label.get())

    def _labels_path(self) -> Path:
        return Path(self.collect_dir.get()) / "labels.csv"

    def _rewrite_labels(self) -> None:
        out = Path(self.collect_dir.get())
        out.mkdir(parents=True, exist_ok=True)
        (out / "audio").mkdir(parents=True, exist_ok=True)
        path = out / "labels.csv"
        tmp = path.with_suffix(".csv.tmp")
        fields = ["utt_id", "file", "speaker_id", "word_id", "label",
                  "error_type", "notes", "split"]
        with tmp.open("w", encoding="utf-8", newline="") as f:
            w = csv.DictWriter(f, fieldnames=fields)
            w.writeheader()
            for r in self.collect_rows:
                w.writerow({k: r.get(k, "") for k in fields})
        tmp.replace(path)

    def _load_collected(self) -> None:
        out = Path(self.collect_dir.get())
        out.mkdir(parents=True, exist_ok=True)
        (out / "audio").mkdir(parents=True, exist_ok=True)
        path = out / "labels.csv"
        rows: list[dict] = []
        if path.exists():
            with path.open(encoding="utf-8-sig", newline="") as f:
                for r in csv.DictReader(f):
                    if not r.get("utt_id"):
                        continue
                    rows.append({
                        "utt_id": r.get("utt_id", ""),
                        "file": r.get("file", ""),
                        "speaker_id": r.get("speaker_id", ""),
                        "word_id": r.get("word_id", ""),
                        "label": r.get("label", ""),
                        "error_type": r.get("error_type", ""),
                        "notes": r.get("notes", ""),
                        "split": r.get("split", ""),
                    })
        self.collect_rows = rows
        self._refresh_collect_tree()
        if not path.exists():
            self._rewrite_labels()

    def _refresh_collect_tree(self) -> None:
        self.collect_tree.delete(*self.collect_tree.get_children())
        n_ok = n_bad = 0
        for r in self.collect_rows:
            label = str(r.get("label", ""))
            if label == "1":
                n_ok += 1
                shown = "正确"
            elif label == "0":
                n_bad += 1
                shown = "错误"
            else:
                shown = "未标注"
            try:
                word = self._word_by_id.get(int(r.get("word_id", -1)), {}).get("word", "")
            except ValueError:
                word = ""
            self.collect_tree.insert("", "end", iid=r["utt_id"], values=(
                r["utt_id"], r.get("speaker_id", ""), word, shown,
                r.get("error_type", ""), r.get("notes", ""),
            ))
        self.collect_count.set(f"{len(self.collect_rows)} 条 · 正确 {n_ok} · 错误 {n_bad}")

    def _on_collect_select(self, _event=None) -> None:
        sel = self.collect_tree.selection()
        if not sel:
            return
        utt = sel[0]
        row = next((r for r in self.collect_rows if r["utt_id"] == utt), None)
        if not row:
            return
        path = Path(self.collect_dir.get()) / row["file"]
        if not path.exists():
            self.collect_status.set(f"文件不存在：{path}")
            return
        try:
            y = load_audio(path)
        except Exception as exc:  # noqa: BLE001
            self.collect_status.set(f"读取失败：{exc}")
            return
        self.pending_audio = None
        self.current_audio = y
        self.current_sr = 16000
        self.selected_utt = utt
        self.collect_speaker.set(row.get("speaker_id", ""))
        try:
            self.word_combo.current(int(row.get("word_id", 0)))
        except (ValueError, tk.TclError):
            pass
        self.collect_label.set({"1": "正确", "0": "错误"}.get(str(row.get("label")), "未标注"))
        self.collect_error.set(row.get("error_type", ""))
        self.collect_notes.set(row.get("notes", ""))
        self._preview_collect(y)
        self.collect_status.set(f"选中 {utt}；修改后点“保存样本/标注”")

    def _save_collect(self) -> None:
        label = self._label_value()
        if label is None:
            messagebox.showwarning("未标注", "先选“正确”或“错误”")
            return
        error = self.collect_error.get().strip()
        notes = self.collect_notes.get().strip()

        if self.selected_utt:
            row = next((r for r in self.collect_rows if r["utt_id"] == self.selected_utt), None)
            if not row:
                return
            row["label"] = str(label)
            row["error_type"] = error
            row["notes"] = notes
            self._rewrite_labels()
            self._refresh_collect_tree()
            self.collect_status.set(f"已更新标注：{self.selected_utt}")
            return

        if self.pending_audio is None:
            messagebox.showwarning("没有样本", "先录音或导入一条 WAV")
            return
        word = self._selected_word()
        if word is None:
            messagebox.showwarning("没有单词", "先选择一个单词")
            return
        import soundfile as sf

        out = Path(self.collect_dir.get())
        (out / "audio").mkdir(parents=True, exist_ok=True)
        speaker = "".join(c for c in self.collect_speaker.get().strip()
                          if c.isalnum() or c in "-_") or "S01"
        stem = f"{speaker}_{int(word['word_id']):02d}_{int(time.time())}"
        wav = out / "audio" / f"{stem}.wav"
        n = 1
        while wav.exists():
            wav = out / "audio" / f"{stem}_{n}.wav"
            n += 1
        sf.write(wav, self.pending_audio, 16000, subtype="PCM_16")
        row = {
            "utt_id": wav.stem,
            "file": f"audio/{wav.name}",
            "speaker_id": speaker,
            "word_id": str(word["word_id"]),
            "label": str(label),
            "error_type": error,
            "notes": notes,
            "split": "",
        }
        self.collect_rows.append(row)
        self._rewrite_labels()
        self._refresh_collect_tree()
        self.pending_audio = None
        self.current_audio = None
        self.selected_utt = None
        self.collect_label.set("未标注")
        self.collect_error.set("")
        self.collect_notes.set("")
        self.collect_status.set(f"已保存 {wav.name}；继续录下一条")

    def _delete_collect(self) -> None:
        sel = self.collect_tree.selection()
        if not sel:
            messagebox.showwarning("未选中", "先在右侧选中一条样本")
            return
        utt = sel[0]
        if not messagebox.askyesno("删除", f"删除 {utt} 及其 WAV？"):
            return
        row = next((r for r in self.collect_rows if r["utt_id"] == utt), None)
        if row:
            p = Path(self.collect_dir.get()) / row["file"]
            try:
                p.unlink(missing_ok=True)
            except Exception:
                pass
            self.collect_rows = [r for r in self.collect_rows if r["utt_id"] != utt]
            self._rewrite_labels()
            self._refresh_collect_tree()
        self.selected_utt = None
        self.pending_audio = None
        self.current_audio = None
        self.collect_status.set(f"已删除 {utt}")

    # ---------------------------------------------------------------- data
    def _build_data_tab(self) -> None:
        top = ttk.Frame(self.tab_data)
        top.pack(fill="x")
        ttk.Label(top, text="数据目录", style="Muted.TLabel").pack(side="left")
        ttk.Entry(top, textvariable=self.data_dir, width=70).pack(
            side="left", padx=8, fill="x", expand=True
        )
        ttk.Button(top, text="浏览", command=self._browse_data).pack(side="left", padx=4)
        ttk.Button(top, text="检查数据", command=self._inspect_data).pack(side="left", padx=4)
        ttk.Button(top, text="生成合成数据", command=self._generate_toy).pack(side="left", padx=4)

        cards = ttk.Frame(self.tab_data)
        cards.pack(fill="x", pady=(12, 10))
        specs = [("样本", "samples"), ("说话人", "speakers"),
                 ("正确 / 错误", "labels"), ("特征", "features")]
        for i, (title, key) in enumerate(specs):
            c = self._card(cards, title, key, self.metric_vars)
            c.pack(side="left", fill="x", expand=True, padx=(0 if i == 0 else 8, 0))

        body = ttk.Frame(self.tab_data)
        body.pack(fill="both", expand=True)
        left = ttk.Frame(body)
        left.pack(side="left", fill="both", expand=True)
        cols = ("utt", "speaker", "word", "label", "file")
        self.tree = ttk.Treeview(left, columns=cols, show="headings", height=18)
        for c, w in zip(cols, (120, 90, 60, 60, 360)):
            self.tree.heading(c, text={"utt": "utt_id", "speaker": "speaker",
                                       "word": "word", "label": "label",
                                       "file": "file"}[c])
            self.tree.column(c, width=w, anchor="w")
        self.tree.pack(side="left", fill="both", expand=True)
        sb = ttk.Scrollbar(left, orient="vertical", command=self.tree.yview)
        sb.pack(side="right", fill="y")
        self.tree.configure(yscrollcommand=sb.set)
        self.tree.bind("<<TreeviewSelect>>", self._on_select_row)

        right = tk.Frame(body, bg=PANEL, highlightbackground=LINE,
                         highlightthickness=1, padx=12, pady=12)
        right.pack(side="left", fill="both", padx=(12, 0))
        tk.Label(right, text="Mel 预览", bg=PANEL, fg=MUTED,
                 font=(FONT, 9)).pack(anchor="w")
        self.mel_label = tk.Label(right, bg="#0e1729", text="选择样本",
                                  fg=MUTED, font=(FONT, 9))
        self.mel_label.pack(pady=(8, 6))
        self.mel_info = tk.Label(right, text="选一条样本查看 40×64 log-Mel",
                                 bg=PANEL, fg=MUTED, font=(MONO, 9))
        self.mel_info.pack(anchor="w")

    def _browse_data(self) -> None:
        p = filedialog.askdirectory(initialdir=str(ROOT))
        if p:
            self.data_dir.set(p)
            self._inspect_data()

    def _inspect_data(self) -> None:
        try:
            samples = load_manifest(self.data_dir.get())
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("读取失败", str(exc))
            return
        self.samples = samples
        self.tree.delete(*self.tree.get_children())
        for s in samples[:300]:
            self.tree.insert("", "end", values=(
                s.utt_id, s.speaker_id, s.word_id, s.label,
                str(s.path.relative_to(Path(self.data_dir.get()).resolve()))
                if str(s.path).startswith(str(Path(self.data_dir.get()).resolve()))
                else s.path.name,
            ))
        counts = class_counts(np.array([s.label for s in samples]))
        speakers = len({s.speaker_id for s in samples})
        self.metric_vars["samples"].set(str(len(samples)))
        self.metric_vars["speakers"].set(str(speakers))
        self.metric_vars["labels"].set(f"{counts.get(1, 0)} / {counts.get(0, 0)}")
        self.metric_vars["features"].set(f"{N_MELS}×{N_FRAMES}")
        self.status.set(f"数据：{len(samples)} 条，{speakers} 个说话人")

    def _on_select_row(self, _event=None) -> None:
        sel = self.tree.selection()
        if not sel:
            return
        idx = self.tree.index(sel[0])
        if idx >= len(self.samples):
            return
        s = self.samples[idx]
        try:
            f = extract_file(s.path)
        except Exception as exc:  # noqa: BLE001
            self.mel_info.config(text=f"提取失败：{exc}")
            return
        self._show_mel(f, self.mel_label, "mel_photo")
        self.mel_info.config(
            text=f"{s.utt_id}  label={s.label}  shape={f.shape}  "
                 f"range=[{f.min():.2f},{f.max():.2f}]"
        )

    def _show_mel(self, feature: np.ndarray, label: tk.Label, attr: str) -> None:
        import matplotlib
        matplotlib.use("Agg")
        from matplotlib import cm

        f = np.asarray(feature, dtype=np.float32)
        lo, hi = float(f.min()), float(f.max())
        n = (f - lo) / (hi - lo + 1e-6)
        rgb = (cm.magma(n)[:, :, :3] * 255).astype(np.uint8)
        img = Image.fromarray(rgb).resize((360, 300), Image.Resampling.NEAREST)
        photo = ImageTk.PhotoImage(img)
        setattr(self, attr, photo)
        label.configure(image=photo, width=360, height=300, text="")

    def _generate_toy(self) -> None:
        if self.toy_thread and self.toy_thread.is_alive():
            return
        out = ROOT / "build" / "gui_toy_dataset"

        def work() -> None:
            try:
                generate_toy_dataset(out, n_speakers=6, n_per_speaker=50)
                self.q.put(("toy_done", str(out)))
            except Exception as exc:  # noqa: BLE001
                self.q.put(("error", f"生成合成数据失败：{exc}"))

        self.status.set("正在生成合成数据…")
        self.toy_thread = threading.Thread(target=work, daemon=True)
        self.toy_thread.start()

    # --------------------------------------------------------------- train
    def _build_train_tab(self) -> None:
        wrap = ttk.Frame(self.tab_train)
        wrap.pack(fill="both", expand=True)

        left = ttk.Frame(wrap)
        left.pack(side="left", fill="y", padx=(0, 14))
        form = tk.Frame(left, bg=PANEL, highlightbackground=LINE,
                        highlightthickness=1, padx=16, pady=14)
        form.pack(fill="x")

        def row(label: str, var: tk.StringVar, width: int = 32) -> None:
            r = ttk.Frame(form)
            r.pack(fill="x", pady=4)
            ttk.Label(r, text=label, style="Panel.TLabel", width=12).pack(side="left")
            ttk.Entry(r, textvariable=var, width=width).pack(side="left", fill="x", expand=True)

        row("数据目录", self.data_dir, 40)
        row("输出目录", self.out_dir, 40)
        row("epochs", self.epochs, 10)
        row("batch", self.batch, 10)
        row("lr", self.lr, 10)
        row("验证说话人", self.val_speakers, 24)
        ttk.Label(form, text="留空则按说话人自动留出 ~25%", style="Panel.TLabel",
                  font=(FONT, 8)).pack(anchor="w", pady=(0, 6))
        ttk.Checkbutton(form, text="特征增强（gain / time shift / noise）",
                        variable=self.augment).pack(anchor="w", pady=2)
        ttk.Checkbutton(form, text="只训练，不转换（--no-convert）",
                        variable=self.no_convert).pack(anchor="w", pady=2)

        btns = ttk.Frame(left)
        btns.pack(fill="x", pady=(12, 0))
        self.btn_train = ttk.Button(btns, text="开始训练", style="Accent.TButton",
                                    command=self._start_train)
        self.btn_train.pack(side="left", fill="x", expand=True)
        self.btn_stop = ttk.Button(btns, text="停止", style="Danger.TButton",
                                   command=self._stop_train, state="disabled")
        self.btn_stop.pack(side="left", padx=(6, 0))
        ttk.Button(btns, text="打开输出", command=lambda: _open_path(Path(self.out_dir.get()))).pack(
            side="left", padx=(6, 0)
        )

        self.progress = ttk.Progressbar(left, mode="indeterminate")
        self.progress.pack(fill="x", pady=(12, 0))

        right = ttk.Frame(wrap)
        right.pack(side="left", fill="both", expand=True)
        cards = ttk.Frame(right)
        cards.pack(fill="x")
        for i, (title, key) in enumerate([
            ("准确率", "acc"), ("F1", "f1"), ("参数量", "params"),
            ("最佳 epoch", "epoch"), ("int8 标签一致", "agree"),
            ("int8 概率差", "prob"),
        ]):
            c = self._card(cards, title, key, self.parity_vars)
            c.pack(side="left", fill="x", expand=True, padx=(0 if i == 0 else 8, 0))

        self.log = ScrolledText(right, height=26, bg="#080d1a", fg="#dbeafe",
                                insertbackground=FG, relief="flat",
                                font=(MONO, 9), padx=10, pady=8)
        self.log.pack(fill="both", expand=True, pady=(10, 0))
        self._log("就绪。先点“检查数据/生成合成数据”，再“开始训练”。\n")

    def _log(self, text: str) -> None:
        self.log.insert("end", text)
        self.log.see("end")

    def _start_train(self) -> None:
        if self.proc and self.proc.poll() is None:
            return
        data = Path(self.data_dir.get())
        if not (data / "labels.csv").exists():
            messagebox.showerror("缺少数据", f"{data}/labels.csv 不存在")
            return
        out = Path(self.out_dir.get())
        out.mkdir(parents=True, exist_ok=True)
        cmd = [
            sys.executable, str(HERE / "run.py"),
            "--data-dir", str(data),
            "--out", str(out),
            "--epochs", self.epochs.get().strip() or "25",
            "--batch-size", self.batch.get().strip() or "16",
            "--lr", self.lr.get().strip() or "0.001",
        ]
        if self.val_speakers.get().strip():
            cmd += ["--val-speakers", self.val_speakers.get().strip()]
        if self.augment.get():
            cmd += ["--augment"]
        if self.no_convert.get():
            cmd += ["--no-convert"]

        env = os.environ.copy()
        env["PYTHONUTF8"] = "1"
        env["PYTHONIOENCODING"] = "utf-8"
        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        self.log.delete("1.0", "end")
        self._log("$ " + " ".join(cmd) + "\n\n")
        self.proc = subprocess.Popen(
            cmd, cwd=str(ROOT), env=env, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, encoding="utf-8",
            errors="replace", creationflags=flags,
        )
        self.btn_train.configure(state="disabled")
        self.btn_stop.configure(state="normal")
        self.progress.start(12)
        self.status.set("训练/转换中…")

        def reader() -> None:
            assert self.proc and self.proc.stdout
            for line in self.proc.stdout:
                self.q.put(("log", line))
            code = self.proc.wait()
            self.q.put(("done", code))

        threading.Thread(target=reader, daemon=True).start()

    def _terminate_proc(self) -> None:
        if not (self.proc and self.proc.poll() is None):
            return
        try:
            if os.name == "nt":
                subprocess.run(
                    ["taskkill", "/PID", str(self.proc.pid), "/T", "/F"],
                    capture_output=True,
                    creationflags=subprocess.CREATE_NO_WINDOW,
                )
            else:
                self.proc.terminate()
        except Exception:
            try:
                self.proc.terminate()
            except Exception:
                pass

    def _stop_train(self) -> None:
        if self.proc and self.proc.poll() is None:
            self.status.set("正在停止…")
            self._terminate_proc()

    def _finish_train(self, code: int) -> None:
        self.progress.stop()
        self.btn_train.configure(state="normal")
        self.btn_stop.configure(state="disabled")
        out = Path(self.out_dir.get())
        if code != 0:
            self.status.set(f"失败（exit {code}）")
            self._log(f"\n[exit {code}]\n")
            return
        self.status.set("完成")
        try:
            m = json.loads((out / "metrics.json").read_text(encoding="utf-8"))
            self.parity_vars["acc"].set(f"{m.get('accuracy', 0):.3f}")
            self.parity_vars["f1"].set(f"{m.get('f1', 0):.3f}")
            self.parity_vars["params"].set(str(m.get("n_params", "—")))
            self.parity_vars["epoch"].set(str(m.get("best_epoch", "—")))
            p = json.loads((out / "parity.json").read_text(encoding="utf-8")) \
                if (out / "parity.json").exists() else {}
            q = p.get("int8_tflite", {})
            self.parity_vars["agree"].set(f"{q.get('label_agreement', 0):.3f}"
                                          if "label_agreement" in q else "—")
            self.parity_vars["prob"].set(f"{q.get('max_abs_prob', 0):.3f}"
                                         if "max_abs_prob" in q else "—")
            self._load_artifacts()
        except Exception as exc:  # noqa: BLE001
            self._log(f"\n[读取 metrics 失败] {exc}\n")

    # -------------------------------------------------------------- deploy
    def _build_deploy_tab(self) -> None:
        top = ttk.Frame(self.tab_deploy)
        top.pack(fill="x")
        ttk.Label(top, text="输出目录", style="Muted.TLabel").pack(side="left")
        ttk.Entry(top, textvariable=self.out_dir, width=70).pack(
            side="left", padx=8, fill="x", expand=True
        )
        ttk.Button(top, text="刷新产物", command=self._load_artifacts).pack(side="left", padx=4)
        ttk.Button(top, text="打开 report", command=lambda: _open_path(Path(self.out_dir.get()) / "report.md")).pack(
            side="left", padx=4
        )

        body = ttk.Frame(self.tab_deploy)
        body.pack(fill="both", expand=True, pady=(12, 0))
        left = ttk.Frame(body)
        left.pack(side="left", fill="both", expand=True)
        self.artifacts = ttk.Treeview(left, columns=("name", "size", "path"),
                                      show="headings", height=10)
        for c, w in zip(("name", "size", "path"), (170, 80, 520)):
            self.artifacts.heading(c, text=c)
            self.artifacts.column(c, width=w, anchor="w")
        self.artifacts.pack(fill="both", expand=True)
        self.artifacts.bind("<Double-1>", self._open_artifact)

        self.parity_text = ScrolledText(left, height=10, bg="#080d1a", fg="#dbeafe",
                                        relief="flat", font=(MONO, 9), padx=10, pady=8)
        self.parity_text.pack(fill="x", pady=(8, 0))

        right = tk.Frame(body, bg=PANEL, highlightbackground=LINE,
                         highlightthickness=1, padx=14, pady=14)
        right.pack(side="left", fill="both", padx=(12, 0))
        tk.Label(right, text="单条 WAV 试推理", bg=PANEL, fg=TEAL,
                 font=(FONT, 11, "bold")).pack(anchor="w")
        r = ttk.Frame(right)
        r.pack(fill="x", pady=(8, 8))
        ttk.Entry(r, textvariable=self.single_wav, width=34).pack(
            side="left", fill="x", expand=True
        )
        ttk.Button(r, text="浏览", command=self._browse_wav).pack(side="left", padx=4)
        ttk.Button(right, text="运行推理", style="Accent.TButton",
                   command=self._single_infer).pack(fill="x")
        self.single_result = tk.StringVar(value="选择 WAV 后点击运行")
        tk.Label(right, textvariable=self.single_result, bg=PANEL, fg=AMBER,
                 font=(MONO, 9), justify="left", wraplength=300).pack(anchor="w", pady=(10, 8))
        self.single_label = tk.Label(right, bg="#0e1729", text="Mel",
                                     fg=MUTED, font=(FONT, 9))
        self.single_label.pack()

    def _browse_wav(self) -> None:
        p = filedialog.askopenfilename(
            initialdir=str(ROOT),
            filetypes=[("WAV", "*.wav"), ("Audio", "*.wav *.flac *.ogg"), ("All", "*.*")],
        )
        if p:
            self.single_wav.set(p)

    def _load_metrics(self, out: Path) -> None:
        """Populate cards when opening an existing output directory."""
        try:
            m = json.loads((out / "metrics.json").read_text(encoding="utf-8"))
            self.parity_vars["acc"].set(f"{m.get('accuracy', 0):.3f}")
            self.parity_vars["f1"].set(f"{m.get('f1', 0):.3f}")
            self.parity_vars["params"].set(str(m.get("n_params", "—")))
            self.parity_vars["epoch"].set(str(m.get("best_epoch", "—")))
        except Exception:
            pass
        try:
            p = json.loads((out / "parity.json").read_text(encoding="utf-8"))
            q = p.get("int8_tflite", {})
            self.parity_vars["agree"].set(
                f"{q.get('label_agreement', 0):.3f}" if "label_agreement" in q else "—"
            )
            self.parity_vars["prob"].set(
                f"{q.get('max_abs_prob', 0):.3f}" if "max_abs_prob" in q else "—"
            )
        except Exception:
            pass

    def _load_artifacts(self) -> None:
        out = Path(self.out_dir.get())
        self.artifacts.delete(*self.artifacts.get_children())
        if not out.exists():
            return
        for p in sorted(out.iterdir()):
            if p.is_file():
                self.artifacts.insert("", "end", values=(p.name, _size(p.stat().st_size), str(p)))
        self._load_metrics(out)
        parity = out / "parity.json"
        if parity.exists():
            self.parity_text.delete("1.0", "end")
            self.parity_text.insert("end", json.dumps(json.loads(parity.read_text(encoding="utf-8")),
                                                      indent=2, ensure_ascii=False))

    def _open_artifact(self, _event=None) -> None:
        sel = self.artifacts.selection()
        if not sel:
            return
        values = self.artifacts.item(sel[0], "values")
        if len(values) >= 3:
            _open_path(Path(values[2]))

    def _single_infer(self) -> None:
        wav = Path(self.single_wav.get())
        if not wav.exists():
            messagebox.showerror("缺少文件", "请选择一个 WAV")
            return
        out = Path(self.out_dir.get())
        try:
            f = extract_file(wav)
            self._show_mel(f, self.single_label, "single_photo")
            x = f[None, None, :, :]
            t0 = time.perf_counter()
            if (out / "model_int8.tflite").exists():
                from convert import TFLiteRunner

                runner = TFLiteRunner(out / "model_int8.tflite")
                logits = runner.predict_logits(x)[0]
                source = "TFLite int8"
            elif (out / "model.pt").exists():
                import torch
                from model import TinyMelNet

                model = TinyMelNet()
                model.load_state_dict(torch.load(out / "model.pt", map_location="cpu"))
                model.eval()
                with torch.no_grad():
                    logits = model(torch.from_numpy(x)).numpy()[0]
                source = "PyTorch float"
            else:
                raise FileNotFoundError("没有 model_int8.tflite / model.pt，请先训练")
            dt = (time.perf_counter() - t0) * 1000

            e = np.exp(logits - logits.max())
            prob = e / e.sum()
            p1 = float(prob[1])
            self.single_result.set(
                f"{'PASS' if p1 >= 0.5 else 'FAIL'}  p(correct)={p1:.3f}\n"
                f"source={source}  latency={dt:.1f} ms\nwav={wav.name}"
            )
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("推理失败", str(exc))

    # --------------------------------------------------------------- queue
    def _poll(self) -> None:
        try:
            while True:
                kind, payload = self.q.get_nowait()
                if kind == "log":
                    self._log(payload)
                elif kind == "done":
                    self._finish_train(int(payload))
                elif kind == "toy_done":
                    self.data_dir.set(payload)
                    self._inspect_data()
                    self.status.set("合成数据已生成")
                elif kind == "error":
                    self.status.set("出错")
                    messagebox.showerror("错误", str(payload))
                elif kind == "record_done":
                    y, sr = payload
                    self.recording = False
                    self.record_btn.configure(state="normal")
                    self._set_pending(np.asarray(y, dtype=np.float32))
                    self.collect_status.set("录音完成；试听后选“正确/错误”，再保存")
                elif kind == "record_error":
                    self.recording = False
                    self.record_btn.configure(state="normal")
                    self.collect_status.set(f"录音失败：{payload}")
                    messagebox.showerror("录音失败", str(payload))
        except queue.Empty:
            pass
        self.root.after(100, self._poll)

    def _close(self) -> None:
        if self.proc and self.proc.poll() is None:
            if not messagebox.askyesno("退出", "训练仍在运行，确定退出？"):
                return
            self._terminate_proc()
        try:
            import sounddevice as sd
            sd.stop()
        except Exception:
            pass
        self.root.destroy()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    try:
        if os.name == "nt":
            import ctypes
            ctypes.windll.shcore.SetProcessDpiAwareness(1)  # type: ignore[attr-defined]
    except Exception:
        pass
    root = tk.Tk()
    root.title("E-Ink 模型管线控制台")
    sw, sh = root.winfo_screenwidth(), root.winfo_screenheight()
    root.geometry(f"{min(1280, sw - 80)}x{min(860, sh - 100)}")
    root.minsize(1000, 680)
    root.configure(bg=BG)
    app = App(root)
    if args.selftest:
        toy = ROOT / "build" / "pipeline_toy2" / "dataset"
        if toy.exists():
            app.data_dir.set(str(toy))
            app._inspect_data()
        root.update()
        print("GUI selftest PASS")
        root.destroy()
        return 0
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
