#!/usr/bin/env python3
"""PC-side sync for the device collection firmware.

Workflow:
  1. Flash the collect build (CONFIG_APP_MODE_COLLECT=y).
  2. Record with the 5 keys, or use `record` below for PC-triggered capture.
  3. `sync` downloads WAVs + labels.csv and optionally deletes them on device.

Examples:
  python tools/collect/device_collect.py --port COM4 info
  python tools/collect/device_collect.py --port COM4 sync --delete
  python tools/collect/device_collect.py --port COM4 record --speaker S02 --word 12 --label 1
"""
from __future__ import annotations

import argparse
import csv
import io
import re
import sys
import time
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "audio"))
from bench_audio import Bench  # noqa: E402  (same directory helper)

FIELDS = ["utt_id", "file", "speaker_id", "word_id", "label",
          "error_type", "notes", "split", "take", "ts"]


def parse_csv(text: str) -> list[dict[str, str]]:
    start = text.find("utt_id,")
    if start < 0:
        return []
    body = text[start:]
    end = body.find("\nbench>")
    if end >= 0:
        body = body[:end]
    return list(csv.DictReader(io.StringIO(body)))


def local_manifest(out_dir: Path) -> dict[str, dict[str, str]]:
    path = out_dir / "labels.csv"
    if not path.exists():
        return {}
    rows = {}
    with path.open(encoding="utf-8-sig", newline="") as f:
        for row in csv.DictReader(f):
            if row.get("utt_id"):
                rows[row["utt_id"]] = row
    return rows


def write_manifest(out_dir: Path, rows: dict[str, dict[str, str]]) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    with (out_dir / "labels.csv").open("w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS, extrasaction="ignore")
        w.writeheader()
        for row in sorted(rows.values(), key=lambda r: r.get("utt_id", "")):
            w.writerow(row)


def save_wav(path: Path, pcm: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(16000)
        w.writeframes(pcm)


def download_row(bench: Bench, out_dir: Path, row: dict[str, str], delete: bool) -> bool:
    rel = row.get("file", "").strip()
    if not rel:
        return False
    dev_path = "/storage/collect/" + rel
    target = out_dir / "audio" / Path(rel).name
    pcm = None
    last_exc = None
    for attempt in range(3):
        try:
            pcm = bench.dump(dev_path)
            break
        except Exception as exc:  # noqa: BLE001
            last_exc = exc
            print(f"[warn] download {dev_path} attempt {attempt + 1}: {exc}")
            time.sleep(1.0)
    if pcm is None:
        print(f"[error] {dev_path}: {last_exc}")
        return False
    save_wav(target, pcm)
    row = dict(row)
    row["file"] = f"audio/{target.name}"
    # keep the extra take/ts columns only if present
    if delete:
        bench.send("bench collect delete " + dev_path)
    print(f"  {target.name}: {len(pcm)//2} frames")
    return True


def cmd_sync(bench: Bench, out_dir: Path, delete: bool) -> int:
    rows = parse_csv(bench.send("bench collect csv", timeout=15))
    if not rows:
        print("device CSV is empty")
        return 1
    local = local_manifest(out_dir)
    ok = 0
    for i, row in enumerate(rows, 1):
        utt = row.get("utt_id", "")
        if not utt:
            continue
        if utt in local and (out_dir / local[utt]["file"]).exists():
            print(f"[{i}/{len(rows)}] skip existing {utt}")
            if delete:
                bench.send("bench collect delete /storage/collect/" + row["file"])
            continue
        print(f"[{i}/{len(rows)}] {utt}")
        if download_row(bench, out_dir, row, delete):
            local[utt] = row
            write_manifest(out_dir, local)
            ok += 1
    print(f"synced {ok}/{len(rows)} -> {out_dir}")
    return 0 if ok or rows else 1


def cmd_record(bench: Bench, out_dir: Path, speaker: str, word: int,
               label: int, delete: bool) -> int:
    text = bench.send(f"bench collect set {speaker} {word} {label}")
    print(text)
    before = bench.send("bench collect last")
    before_path = ""
    m = re.search(r"last_path=(\S+)", before)
    if m and m.group(1) != "none":
        before_path = m.group(1)

    print("recording 2 s: speak now")
    bench.send("bench collect rec")
    deadline = time.time() + 12
    last_path = ""
    while time.time() < deadline:
        time.sleep(0.5)
        text = bench.send("bench collect last")
        m = re.search(r"last_path=(\S+)", text)
        if m and m.group(1) != "none" and m.group(1) != before_path:
            last_path = m.group(1)
            break
    if not last_path:
        print("no new recording (VAD rejected or timeout)")
        return 1

    rows = parse_csv(bench.send("bench collect csv", timeout=15))
    row = next((r for r in rows if r.get("utt_id", "") in last_path), None)
    if row is None and rows:
        row = rows[-1]
    if row is None:
        print("no metadata row for " + last_path)
        return 1
    if not download_row(bench, out_dir, row, delete):
        return 1
    local = local_manifest(out_dir)
    row = dict(row)
    row["file"] = f"audio/{Path(row['file']).name}"
    local[row["utt_id"]] = row
    write_manifest(out_dir, local)
    print("saved ->", out_dir)
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", default="COM4")
    p.add_argument("--out", default="data/collected/device/v1.0")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("info")
    s = sub.add_parser("sync")
    s.add_argument("--delete", action="store_true", help="delete WAVs on device after download")
    r = sub.add_parser("record")
    r.add_argument("--speaker", default="S01")
    r.add_argument("--word", type=int, default=0)
    r.add_argument("--label", type=int, choices=[0, 1], default=1)
    r.add_argument("--delete", action="store_true", help="delete WAV on device after download")
    args = p.parse_args()

    out_dir = Path(args.out)
    bench = Bench(args.port)
    try:
        if args.cmd == "info":
            print(bench.send("bench collect info"))
            return 0
        if args.cmd == "sync":
            return cmd_sync(bench, out_dir, args.delete)
        if args.cmd == "record":
            return cmd_record(bench, out_dir, args.speaker, args.word, args.label,
                              args.delete)
    finally:
        bench.close()
    return 1


if __name__ == "__main__":
    sys.exit(main())
