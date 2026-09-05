#!/usr/bin/env python3
"""Convert one animated WebP into a fixed-size RGB565 frame blob."""
import argparse
from pathlib import Path
from PIL import Image

W, H = 320, 132

def convert(im, swap_rb=False):
    im = im.resize((W, H), Image.LANCZOS).convert("RGBA")
    out = bytearray()
    for y in range(H):
        for x in range(W):
            r, g, b, a = im.getpixel((x, y))
            if a < 128:
                r = g = b = 0
            if swap_rb:
                r, b = b, r
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            out += bytes((v & 255, v >> 8))
    return bytes(out)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("source", type=Path)
    ap.add_argument("output", type=Path)
    ap.add_argument("symbol")
    ap.add_argument("--swap-rb", action="store_true",
                    help="swap R/B channels (panel is BGR)")
    args = ap.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    img = Image.open(args.source)
    frames = [convert(img.seek(i) or img.copy(), args.swap_rb)
              for i in range(getattr(img, "n_frames", 1))]
    blob = args.output.with_suffix(".bin")
    blob.write_bytes(b"".join(frames))
    asm = args.output.with_suffix(".S")
    asm.write_text(f'''    .section .rodata.claude,"a",%progbits\n    .global {args.symbol}_data\n    .balign 4\n{args.symbol}_data:\n    .incbin "{blob.resolve()}"\n''')
    print(f"{args.symbol}: {len(frames)} frames, {len(blob.read_bytes())} bytes")

if __name__ == "__main__":
    main()
