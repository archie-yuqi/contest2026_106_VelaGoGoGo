#!/usr/bin/env python3
"""Concatenate wake-reply WAV files into one raw PCM blob + .S + index header.

Extracts the `data` chunk (raw S16LE PCM) from each WAV, concatenates them,
and emits:
  wake_reply_raw.bin        — concatenated raw PCM
  wake_reply_assets.S       — .incbin wrapper (relative path)
  wake_reply_assets.h       — offset/size table

All inputs must be 16 kHz / 16-bit / mono.
"""
import argparse
import struct
from pathlib import Path


def wav_data(path: Path) -> bytes:
    d = path.read_bytes()
    assert d[:4] == b"RIFF" and d[8:12] == b"WAVE", f"{path} not WAV"
    off = 12
    ch = sr = bits = None
    data = None
    while off + 8 <= len(d):
        cid = d[off:off + 4]
        sz = struct.unpack("<I", d[off + 4:off + 8])[0]
        if cid == b"fmt ":
            ch, sr, _, _, bits = struct.unpack("<HIIHH", d[off + 8 + 2:off + 8 + 16])
        elif cid == b"data":
            data = d[off + 8:off + 8 + sz]
        off += 8 + sz + (sz & 1)
    assert (ch, sr, bits) == (1, 16000, 16), f"{path} must be 16k/16bit/mono, got {(ch, sr, bits)}"
    assert data is not None, f"{path} has no data chunk"
    return data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src_dir", help="dir containing 1.wav..N.wav")
    ap.add_argument("dst_dir", help="output dir (app/home_scense/doubao)")
    ap.add_argument("--count", type=int, default=8)
    args = ap.parse_args()

    dst = Path(args.dst_dir)
    dst.mkdir(parents=True, exist_ok=True)

    blob = bytearray()
    entries = []  # (offset, size)
    for i in range(1, args.count + 1):
        pcm = wav_data(Path(args.src_dir) / f"{i}.wav")
        if len(pcm) & 1:
            pcm = pcm[:-1]  # keep even (int16)
        entries.append((len(blob), len(pcm)))
        blob += pcm

    bin_path = dst / "wake_reply_raw.bin"
    bin_path.write_bytes(blob)

    s_path = dst / "wake_reply_assets.S"
    # incbin path is relative to the assembler CWD (the app top-level dir),
    # not the .S file's dir. The .bin sits in the doubao/ subdir, so prefix it.
    s_path.write_text(
        '    .section .rodata.wake_reply,"a",%progbits\n'
        '    .global wake_reply_raw_data\n'
        '    .balign 4\n'
        'wake_reply_raw_data:\n'
        '    .incbin "doubao/wake_reply_raw.bin"\n')

    h_path = dst / "wake_reply_assets.h"
    lines = [
        "#ifndef WAKE_REPLY_ASSETS_H",
        "#define WAKE_REPLY_ASSETS_H",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "typedef struct { size_t offset; size_t size; } wake_reply_asset_t;",
        "extern const unsigned char wake_reply_raw_data[];",
        f"#define WAKE_REPLY_ASSETS_COUNT {len(entries)}",
        "static const wake_reply_asset_t wake_reply_assets[WAKE_REPLY_ASSETS_COUNT] = {",
    ]
    for off, sz in entries:
        lines.append(f"    {{ {off}, {sz} }},")
    lines.append("};")
    lines.append("#endif /* WAKE_REPLY_ASSETS_H */")
    h_path.write_text("\n".join(lines) + "\n")

    print(f"{len(entries)} assets, {len(blob)} bytes -> {bin_path}")


if __name__ == "__main__":
    main()
