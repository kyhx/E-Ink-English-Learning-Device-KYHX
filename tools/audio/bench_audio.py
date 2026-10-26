#!/usr/bin/env python3
"""M2 serial audio acceptance / WAV download helper.

Examples:
  python tools/audio/bench_audio.py --port COM4 info
  python tools/audio/bench_audio.py --port COM4 soak --n 20
  python tools/audio/bench_audio.py --port COM4 rec --ms 2000 --out build/rec.wav
  python tools/audio/bench_audio.py --port COM4 dump --path /storage/rec/loop_00.wav --out build/loop.wav

The device must already be running the M2 firmware and the UART0 bench
console must be free (115200 8N1).
"""
from __future__ import annotations

import argparse
import re
import struct
import sys
import time
import wave

try:
    import serial
except ImportError as exc:  # pragma: no cover
    raise SystemExit("pyserial is required: pip install pyserial") from exc


ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")


class Bench:
    def __init__(self, port: str, baud: int = 115200, timeout: float = 30.0):
        self.ser = serial.Serial(port, baud, timeout=0.2)
        self.timeout = timeout
        self.ser.dtr = False
        self.ser.rts = False
        self.ser.rts = True
        time.sleep(0.1)
        self.ser.rts = False
        self.ser.reset_input_buffer()
        self.wait_ready()

    def wait_ready(self, timeout: float = 12.0) -> None:
        end = time.time() + timeout
        data = bytearray()
        while time.time() < end:
            data.extend(self.ser.read(4096))
            if b"bench> " in data:
                return
            time.sleep(0.1)
        raise TimeoutError("bench console prompt not found")

    def close(self) -> None:
        self.ser.close()

    def send(self, command: str, timeout: float | None = None) -> str:
        timeout = timeout if timeout is not None else self.timeout
        self.ser.reset_input_buffer()
        self.ser.write((command + "\r\n").encode())
        end = time.time() + timeout
        data = bytearray()
        while time.time() < end:
            data.extend(self.ser.read(8192))
            if data.endswith(b"bench> "):
                break
        text = data.decode("utf-8", "replace")
        return ANSI.sub("", text).replace("\r", "")

    def dump(self, path: str, timeout: float = 45.0) -> bytes:
        text = self.send("bench audio dump " + path, timeout=timeout)
        match = re.search(r"DUMP (\d+)\s*\n(.*?)\nENDDUMP", text, re.S)
        if not match:
            raise RuntimeError("device did not return a DUMP block")
        frames = int(match.group(1))
        hex_text = "".join(match.group(2).split())
        if len(hex_text) != frames * 4:
            raise RuntimeError(
                f"short dump: {len(hex_text)//4}/{frames} samples")
        # The firmware prints each int16 as %04x (numeric hex), which is
        # big-endian text.  WAV needs little-endian bytes.
        out = bytearray(frames * 2)
        for i in range(frames):
            value = int(hex_text[i * 4:(i + 1) * 4], 16)
            if value >= 0x8000:
                value -= 0x10000
            struct.pack_into("<h", out, i * 2, value)
        return bytes(out)


def write_wav(path: str, pcm: bytes, sample_rate: int = 16000) -> None:
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm)


def parse_saved(text: str) -> str | None:
    match = re.search(r"saved: (\S+)", text)
    return match.group(1) if match else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM4")
    parser.add_argument("--baud", type=int, default=115200)
    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("info")

    soak = sub.add_parser("soak")
    soak.add_argument("--n", type=int, default=20)

    rec = sub.add_parser("rec")
    rec.add_argument("--ms", type=int, default=2000)
    rec.add_argument("--out", default="build/rec.wav")

    dump = sub.add_parser("dump")
    dump.add_argument("--path", required=True)
    dump.add_argument("--out", default="build/dump.wav")

    args = parser.parse_args()
    bench = Bench(args.port, args.baud)
    try:
        if args.cmd == "info":
            print(bench.send("bench audio info"))
            return 0

        if args.cmd == "soak":
            text = bench.send(f"bench audio soak {args.n}", timeout=args.n * 4 + 20)
            print(text)
            match = re.search(r"soak: (\d+)/(\d+) valid files", text)
            return 0 if (match and match.group(1) == match.group(2)) else 1

        if args.cmd == "rec":
            text = bench.send(f"bench audio rec {args.ms}", timeout=args.ms / 1000 + 8)
            print(text)
            path = parse_saved(text)
            if not path:
                return 1
            pcm = bench.dump(path)
            write_wav(args.out, pcm)
            print(f"downloaded {path} -> {args.out} ({len(pcm)//2} samples)")
            return 0

        if args.cmd == "dump":
            pcm = bench.dump(args.path)
            write_wav(args.out, pcm)
            print(f"downloaded {args.path} -> {args.out} ({len(pcm)//2} samples)")
            return 0

    finally:
        bench.close()
    return 1


if __name__ == "__main__":
    sys.exit(main())
