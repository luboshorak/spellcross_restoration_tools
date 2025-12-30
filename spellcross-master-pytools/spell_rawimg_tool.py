#!/usr/bin/env python3
"""
spell_rawimg_tool.py

Convert Spellcross-style RAW 8-bit indexed images (.bin) + palette chunks (.PAL) to PNG.

Supports:
- RAW 8bpp images with no header (size = width * height)
- Palette files:
    * 768 bytes: full 256-color RGB palette (256*3)
    * 192 bytes: 64-color RGB palette (64*3) mapped into a 256 palette at an inferred offset
- Optional transparency (one or more palette indices)
- Optional nearest-neighbor upscale (pixel-perfect)

Examples (PowerShell):
  python .\spell_rawimg_tool.py BIG_MAP.bin -p BIG_MAP.PAL -w 640 -h 480 --transparent 0 --scale 4
  python .\spell_rawimg_tool.py LEVEL_02.bin -p LEVEL_02.PAL -w 379 -h 259 --transparent 0
  python .\spell_rawimg_tool.py I_HUMER.bin -w 60 -h 50 --transparent 0

If you omit -w/-h, the tool will try to infer dimensions from common widths + sane aspect ratios.
"""

from __future__ import annotations
import argparse
import math
from pathlib import Path
from typing import Iterable, List, Optional, Tuple

try:
    from PIL import Image
except ImportError as e:
    raise SystemExit("Missing dependency: Pillow. Install with: pip install pillow") from e


def read_bytes(path: Path) -> bytes:
    return path.read_bytes()


def infer_dims(n: int) -> Optional[Tuple[int, int]]:
    """
    Heuristic inference:
      - prefer known/common widths
      - otherwise try factor pairs close to 4:3 or 3:2
    """
    common_widths = [
        640, 800, 1024,
        320, 360, 379, 384, 400,
        256, 240, 200, 160, 128,
        96, 80, 75, 65, 60, 50
    ]

    candidates: List[Tuple[float, int, int]] = []

    def score(w: int, h: int) -> float:
        if w <= 0 or h <= 0:
            return 1e9
        # prefer landscape
        if w < h:
            return 1e8 + (h - w)
        ar = w / h
        # distance to common UI aspect ratios
        d_43 = abs(ar - (4 / 3))
        d_32 = abs(ar - (3 / 2))
        d_169 = abs(ar - (16 / 9))
        d = min(d_43, d_32, d_169)
        # mild penalty for tiny/huge
        size_pen = 0.0
        if w * h < 1500:
            size_pen += 0.5
        if w * h > 2_000_000:
            size_pen += 0.5
        return d + size_pen

    for w in common_widths:
        if n % w == 0:
            h = n // w
            candidates.append((score(w, h), w, h))

    # also try factor pairs around sqrt(n)
    root = int(math.sqrt(n))
    for w in range(max(1, root - 500), root + 501):
        if w != 0 and n % w == 0:
            h = n // w
            candidates.append((score(w, h), w, h))

    if not candidates:
        return None

    candidates.sort(key=lambda x: x[0])
    best = candidates[0]
    return best[1], best[2]


def parse_palette(pal_bytes: bytes, img_bytes: bytes, offset_override: Optional[int] = None) -> List[int]:
    """
    Return a 256*3 list suitable for PIL putpalette().

    For 64-color palettes (192 bytes), map into 256 palette at inferred offset:
      - infer from image indices: min non-zero index tends to be 128 or 192
    """
    # default filler palette: grayscale ramp (so unknown indices are still visible)
    full = []
    for i in range(256):
        full += [i, i, i]

    if len(pal_bytes) == 768:
        # full palette
        # clamp to 0..255 just in case
        p = list(pal_bytes[:768])
        p = [max(0, min(255, x)) for x in p]
        return p

    if len(pal_bytes) != 192:
        raise ValueError(f"Unsupported palette size: {len(pal_bytes)} bytes (expected 192 or 768)")

    # 64-color chunk
    chunk = list(pal_bytes)
    chunk = [max(0, min(255, x)) for x in chunk]

    if offset_override is not None:
        base = offset_override
    else:
        # infer base from used indices
        used = set(img_bytes)
        used.discard(0)
        if not used:
            base = 0
        else:
            mn = min(used)
            # snap to typical ranges
            if 120 <= mn <= 135:
                base = 128
            elif 185 <= mn <= 205:
                base = 192
            else:
                # fallback: choose nearest multiple of 64
                base = int(round(mn / 64)) * 64
                base = max(0, min(192, base))

    if base % 64 != 0:
        base = (base // 64) * 64

    # map chunk colors into full palette
    start = base * 3
    full[start:start + 192] = chunk
    return full


def parse_transparent_indices(s: Optional[str]) -> List[int]:
    if not s:
        return []
    out: List[int] = []
    for part in s.replace(";", ",").split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            a, b = part.split("-", 1)
            a_i = int(a.strip())
            b_i = int(b.strip())
            step = 1 if b_i >= a_i else -1
            out.extend(list(range(a_i, b_i + step, step)))
        else:
            out.append(int(part))
    # clamp
    out = [max(0, min(255, i)) for i in out]
    # unique preserving order
    seen = set()
    uniq = []
    for i in out:
        if i not in seen:
            uniq.append(i)
            seen.add(i)
    return uniq


def to_png(
    img_bytes: bytes,
    w: int,
    h: int,
    palette: Optional[List[int]],
    transparent: List[int],
    out_path: Path,
    scale: int = 1,
) -> None:
    if len(img_bytes) != w * h:
        raise ValueError(f"Input size mismatch: {len(img_bytes)} bytes != {w}*{h} ({w*h})")

    im = Image.frombytes("P", (w, h), img_bytes)

    if palette is not None:
        im.putpalette(palette)

    if transparent:
        # Convert to RGBA and apply alpha mask
        rgba = im.convert("RGBA")
        pix = rgba.load()
        for y in range(h):
            for x in range(w):
                idx = img_bytes[y * w + x]
                if idx in transparent:
                    r, g, b, _a = pix[x, y]
                    pix[x, y] = (r, g, b, 0)
        out_im = rgba
    else:
        out_im = im

    if scale and scale != 1:
        out_im = out_im.resize((w * scale, h * scale), resample=Image.NEAREST)

    out_im.save(out_path, format="PNG")


def main() -> int:
    ap = argparse.ArgumentParser(description="Convert Spellcross RAW 8-bit images + PAL chunks to PNG.")
    ap.add_argument("input", type=Path, help="Input .bin (RAW 8bpp) file")
    ap.add_argument("-p", "--pal", type=Path, default=None, help="Palette file (.PAL), 192 or 768 bytes")
    ap.add_argument("-w", "--width", type=int, default=0, help="Image width (pixels)")
    ap.add_argument("-h", "--height", type=int, default=0, help="Image height (pixels)")
    ap.add_argument("--pal-offset", type=int, default=None, help="For 192B palettes: force base index (e.g., 128 or 192)")
    ap.add_argument("--transparent", type=str, default=None,
                    help="Comma list of palette indices to make transparent (e.g. '0' or '0,255' or '0-15')")
    ap.add_argument("--scale", type=int, default=1, help="Nearest-neighbor upscale factor (e.g., 2, 3, 4)")
    ap.add_argument("-o", "--out", type=Path, default=None, help="Output PNG path (default: input stem + _restored.png)")
    args = ap.parse_args()

    img_bytes = read_bytes(args.input)

    w = int(args.width) if args.width else 0
    h = int(args.height) if args.height else 0

    if (w <= 0) ^ (h <= 0):
        raise SystemExit("Provide both --width and --height, or neither (auto-infer).")

    if w <= 0 and h <= 0:
        dims = infer_dims(len(img_bytes))
        if dims is None:
            raise SystemExit("Could not infer dimensions. Provide --width and --height.")
        w, h = dims

    palette = None
    if args.pal is not None:
        pal_bytes = read_bytes(args.pal)
        palette = parse_palette(pal_bytes, img_bytes, offset_override=args.pal_offset)

    transparent = parse_transparent_indices(args.transparent)

    out = args.out
    if out is None:
        out = args.input.with_name(args.input.stem + "_restored.png")

    to_png(
        img_bytes=img_bytes,
        w=w,
        h=h,
        palette=palette,
        transparent=transparent,
        out_path=out,
        scale=max(1, int(args.scale)),
    )

    print(f"OK: {args.input.name} -> {out.name} ({w}x{h})" + (f" scale x{args.scale}" if args.scale != 1 else ""))
    if args.pal:
        print(f"  palette: {args.pal.name}")
    if transparent:
        print(f"  transparent indices: {transparent}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
