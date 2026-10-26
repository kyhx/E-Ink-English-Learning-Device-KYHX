#!/usr/bin/env python3
r"""Download Google/Oxford dictionary pronunciation MP3s and convert to WAV.

URL pattern (observed 2026-10):
    https://ssl.gstatic.com/dictionary/static/sounds/oxford/<word>--_<accent>_1.mp3

Examples:
    example--_gb_1.mp3
    example--_us_1.mp3

Output:
    data/reference/oxford/
    ├── mp3/gb/example.mp3
    ├── wav/gb/example.wav        # 16 kHz mono PCM16
    ├── metadata.csv
    └── missing.csv

IMPORTANT
---------
The audio is served by Google's dictionary static endpoint, but it is likely
still copyrighted by Google/Oxford.  Use it for local research/teaching only.
Do not redistribute the MP3/WAV files or commit them to a public repository.
This script only automates the download; it does not grant a license.

Usage:
    .\.venv-train\Scripts\python.exe tools\reference\fetch_oxford_audio.py ^
        --words-file data\words.csv --out data\reference\oxford --accents both

    # test a few words first
    ... --words apple,book,cat --dry-run
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path

BASE = "https://ssl.gstatic.com/dictionary/static/sounds/oxford"
UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " \
     "(KHTML, like Gecko) Chrome/131.0 Safari/537.36"
HEADERS = {"User-Agent": UA, "Accept": "audio/mpeg,*/*;q=0.8"}


@dataclass
class Word:
    word_id: str
    word: str
    meaning: str = ""
    ipa: str = ""


def _slug(word: str) -> str:
    """Google dictionary slugs: lower-case, whitespace -> underscore."""
    s = word.strip().lower()
    s = re.sub(r"\s+", "_", s)
    return s


def _url(word: str, accent: str, variant: int) -> str:
    slug = _slug(word)
    # Keep apostrophes/hyphens readable; quote handles anything unusual.
    return f"{BASE}/{urllib.parse.quote(slug, safe='-_')}--_{accent}_{variant}.mp3"


def load_words(path: Path | None, csv_words: str | None) -> list[Word]:
    if csv_words:
        raw = [w.strip() for w in csv_words.split(",") if w.strip()]
        return [Word(str(i), w) for i, w in enumerate(raw)]
    if path is None:
        path = Path(__file__).resolve().parents[2] / "data" / "words.csv"
    if not path.exists():
        raise FileNotFoundError(f"missing words file: {path}")
    out: list[Word] = []
    with path.open(encoding="utf-8-sig", newline="") as f:
        for row in csv.DictReader(f):
            w = str(row.get("word", "")).strip()
            if w:
                out.append(Word(
                    str(row.get("word_id", len(out))).strip(),
                    w,
                    str(row.get("meaning", "")).strip(),
                    str(row.get("ipa", "")).strip(),
                ))
    if not out:
        raise ValueError(f"no words in {path}")
    return out


def _tool(name: str) -> str | None:
    p = shutil.which(name)
    if p:
        return p
    local = Path(os.environ.get("LOCALAPPDATA", "")) / "Microsoft" / "WinGet" / "Links" / f"{name}.exe"
    if local.exists():
        return str(local)
    return None


def _ffprobe_duration(ffprobe: str, path: Path) -> tuple[float | None, int | None, int | None]:
    try:
        proc = subprocess.run(
            [ffprobe, "-v", "error", "-show_entries",
             "format=duration:stream=sample_rate,channels",
             "-of", "json", str(path)],
            capture_output=True, text=True, encoding="utf-8", errors="replace",
        )
        d = json.loads(proc.stdout)
        dur = float(d.get("format", {}).get("duration", 0) or 0)
        stream = (d.get("streams") or [{}])[0]
        return dur, int(stream.get("sample_rate", 0) or 0), int(stream.get("channels", 0) or 0)
    except Exception:
        return None, None, None


def _valid_mp3(data: bytes) -> bool:
    if len(data) < 2000:
        return False
    if data[:5].lower() in (b"<html", b"<!doc"):
        return False
    # Most MP3s start with ID3 or an MPEG frame sync (0xFFE...).
    return data[:3] == b"ID3" or (len(data) > 1 and data[0] == 0xFF and (data[1] & 0xE0) == 0xE0)


def download_one(url: str, path: Path, timeout: int, retries: int) -> tuple[bool, int, str]:
    """Return (ok, bytes, error)."""
    path.parent.mkdir(parents=True, exist_ok=True)
    last = ""
    for attempt in range(1, retries + 1):
        try:
            req = urllib.request.Request(url, headers=HEADERS)
            with urllib.request.urlopen(req, timeout=timeout) as r:
                status = getattr(r, "status", 200)
                ctype = r.headers.get("Content-Type", "")
                data = r.read()
            if status != 200:
                return False, 0, f"HTTP {status}"
            if "html" in ctype.lower() or not _valid_mp3(data):
                return False, len(data), f"not MP3 (content-type={ctype})"
            path.write_bytes(data)
            return True, len(data), ""
        except urllib.error.HTTPError as exc:
            last = f"HTTP {exc.code}"
            if exc.code in (404, 410):
                return False, 0, last
        except Exception as exc:  # noqa: BLE001
            last = str(exc)
        if attempt < retries:
            time.sleep(1.5 * attempt)
    return False, 0, last


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--words-file", type=Path, default=None)
    ap.add_argument("--words", type=str, default=None,
                    help="comma-separated words; overrides --words-file")
    ap.add_argument("--out", type=Path, default=Path("data/reference/oxford"))
    ap.add_argument("--accents", choices=["gb", "us", "both"], default="both")
    ap.add_argument("--variants", default="1",
                    help="comma-separated variant numbers, e.g. 1 or 1,2")
    ap.add_argument("--timeout", type=int, default=30)
    ap.add_argument("--retries", type=int, default=3)
    ap.add_argument("--sleep", type=float, default=0.15,
                    help="seconds between requests (be polite)")
    ap.add_argument("--overwrite", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--no-wav", action="store_true", help="skip ffmpeg conversion")
    args = ap.parse_args()

    out = args.out.resolve()
    words = load_words(args.words_file, args.words)
    accents = ["gb", "us"] if args.accents == "both" else [args.accents]
    variants = [int(x) for x in str(args.variants).split(",") if x.strip()]
    ffmpeg = _tool("ffmpeg")
    ffprobe = _tool("ffprobe")
    if not args.no_wav and not ffmpeg:
        print("ERROR: ffmpeg not found; use --no-wav or install ffmpeg")
        return 1

    if args.dry_run:
        for w in words:
            for a in accents:
                for v in variants:
                    print(f"{w.word:12s} {a} v{v}  {_url(w.word, a, v)}")
        print(f"\n{len(words) * len(accents) * len(variants)} URLs")
        return 0

    out.mkdir(parents=True, exist_ok=True)
    rows: list[dict] = []
    missing: list[dict] = []
    total = len(words) * len(accents) * len(variants)
    n = 0
    for w in words:
        for accent in accents:
            for variant in variants:
                n += 1
                url = _url(w.word, accent, variant)
                stem = _slug(w.word) + (f"_v{variant}" if variant != 1 else "")
                mp3 = out / "mp3" / accent / f"{stem}.mp3"
                wav = out / "wav" / accent / f"{stem}.wav"
                print(f"[{n}/{total}] {w.word} {accent} v{variant}", end=" ... ", flush=True)

                if (not args.overwrite) and mp3.exists() and mp3.stat().st_size > 2000 \
                        and (args.no_wav or wav.exists()):
                    print("skip")
                    dur = sr = ch = None
                    if ffprobe:
                        dur, sr, ch = _ffprobe_duration(ffprobe, mp3)
                    rows.append(_row(
                        w, accent, variant, url, mp3, wav, "cached", "",
                        dur=dur, sr=sr, ch=ch,
                        wav_status="skipped" if args.no_wav else "ok",
                        sha256=hashlib.sha256(mp3.read_bytes()).hexdigest(),
                    ))
                    continue

                ok, nbytes, err = download_one(url, mp3, args.timeout, args.retries)
                if not ok:
                    print(err)
                    missing.append({"word": w.word, "word_id": w.word_id,
                                    "accent": accent, "variant": variant,
                                    "url": url, "error": err})
                    rows.append(_row(w, accent, variant, url, mp3, wav, "missing", err))
                    time.sleep(args.sleep)
                    continue

                dur = sr = ch = None
                if ffprobe:
                    dur, sr, ch = _ffprobe_duration(ffprobe, mp3)
                wav_status = "skipped"
                if not args.no_wav:
                    wav.parent.mkdir(parents=True, exist_ok=True)
                    proc = subprocess.run(
                        [ffmpeg, "-y", "-loglevel", "error", "-i", str(mp3),
                         "-ac", "1", "-ar", "16000", "-c:a", "pcm_s16le", str(wav)],
                        capture_output=True, text=True, encoding="utf-8", errors="replace",
                    )
                    wav_status = "ok" if proc.returncode == 0 and wav.exists() else f"ffmpeg {proc.returncode}"
                print(f"ok {nbytes}B {dur:.2f}s -> {wav_status}" if dur else f"ok {nbytes}B -> {wav_status}")
                rows.append(_row(w, accent, variant, url, mp3, wav, "ok", "",
                                 dur=dur, sr=sr, ch=ch, wav_status=wav_status,
                                 sha256=hashlib.sha256(mp3.read_bytes()).hexdigest()))
                time.sleep(args.sleep)

    _write_csv(out / "metadata.csv", rows)
    _write_csv(out / "missing.csv", missing)
    ok = sum(1 for r in rows if r["status"] == "ok")
    cached = sum(1 for r in rows if r["status"] == "cached")
    print(f"\nsummary: ok={ok} cached={cached} missing={len(missing)} total={len(rows)}")
    print(f"metadata: {out / 'metadata.csv'}")
    print(f"missing:  {out / 'missing.csv'}")
    print("NOTE: audio rights belong to Google/Oxford; local research use only, do not redistribute.")
    return 0 if ok or cached else 1


def _row(w: Word, accent: str, variant: int, url: str, mp3: Path, wav: Path,
         status: str, error: str, *, dur=None, sr=None, ch=None,
         wav_status="", sha256="") -> dict:
    return {
        "word_id": w.word_id,
        "word": w.word,
        "meaning": w.meaning,
        "ipa": w.ipa,
        "accent": accent,
        "variant": variant,
        "url": url,
        "mp3": str(mp3),
        "wav": str(wav),
        "status": status,
        "error": error,
        "bytes": mp3.stat().st_size if mp3.exists() else 0,
        "sha256": sha256,
        "duration_sec": "" if dur is None else f"{dur:.3f}",
        "sample_rate": "" if sr is None else sr,
        "channels": "" if ch is None else ch,
        "wav_status": wav_status,
        "downloaded_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
    }


def _write_csv(path: Path, rows: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = ["word_id", "word", "meaning", "ipa", "accent", "variant", "url",
              "mp3", "wav", "status", "error", "bytes", "sha256",
              "duration_sec", "sample_rate", "channels", "wav_status", "downloaded_at"]
    with path.open("w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)


if __name__ == "__main__":
    raise SystemExit(main())
