#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
from pathlib import Path
from typing import List, Optional, Tuple

from PIL import Image


def _scale_vga6(pal: List[int]) -> List[int]:
    return [min(255, v * 4) for v in pal] if pal and max(pal) <= 63 else pal


def parse_palette(pal_path: Path, img_idx: bytes, offset_override: Optional[int] = None) -> List[int]:
    b = pal_path.read_bytes()

    # default grayscale
    full: List[int] = []
    for i in range(256):
        full += [i, i, i]

    if len(b) == 768:
        pal = list(b[:768])
        return _scale_vga6(pal)

    if len(b) == 96:
        chunk = _scale_vga6(list(b))
        full[0:96] = chunk
        return full

    if len(b) != 192:
        raise ValueError(f"Unsupported PAL size {len(b)} (expected 96/192/768)")

    chunk = _scale_vga6(list(b))

    if offset_override is not None:
        base = offset_override
    else:
        used = set(img_idx)
        used.discard(0)
        if not used:
            base = 0
        else:
            mn = min(used)
            if 120 <= mn <= 135:
                base = 128
            elif 185 <= mn <= 205:
                base = 192
            else:
                base = max(0, min(192, int(round(mn / 64)) * 64))

    base = (base // 64) * 64
    full[base * 3: base * 3 + 192] = chunk
    return full


def parse_transparent(s: Optional[str]) -> List[int]:
    if not s:
        return []
    out: List[int] = []
    for part in s.replace(";", ",").split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            a, b = part.split("-", 1)
            a_i = int(a); b_i = int(b)
            step = 1 if b_i >= a_i else -1
            out.extend(list(range(a_i, b_i + step, step)))
        else:
            out.append(int(part))
    # unique + clamp
    uniq: List[int] = []
    seen = set()
    for i in out:
        i = max(0, min(255, i))
        if i not in seen:
            uniq.append(i); seen.add(i)
    return uniq


def unpack_packed4(buf: bytes, swap_nibbles: bool = False) -> bytes:
    out = bytearray(len(buf) * 2)
    j = 0
    for b in buf:
        hi = (b >> 4) & 0x0F
        lo = b & 0x0F
        if swap_nibbles:
            hi, lo = lo, hi
        out[j] = hi
        out[j + 1] = lo
        j += 2
    return bytes(out)


def reorder_colmajor_to_rowmajor(idx: bytes, w: int, h: int) -> bytes:
    out = bytearray(w * h)
    for x in range(w):
        col_off = x * h
        for y in range(h):
            out[y * w + x] = idx[col_off + y]
    return bytes(out)


def infer_dims_by_area(area: int) -> Optional[Tuple[int, int]]:
    # try common widths first
    common_w = [464, 640, 800, 1024, 320, 360, 379, 384, 400, 256, 240, 200, 160, 128, 96, 80]
    for w in common_w:
        if w > 0 and area % w == 0:
            h = area // w
            if 50 <= h <= 3000:
                return (w, h)
    # fallback search around sqrt
    root = int(math.sqrt(area))
    for w in range(max(1, root - 1200), root + 1201):
        if area % w == 0:
            h = area // w
            if 50 <= h <= 3000:
                return (w, h)
    return None


def apply_orient(im: Image.Image, orient: str) -> Image.Image:
    o = (orient or "transpose").lower()
    if o == "none":
        return im
    if o == "transpose":
        return im.transpose(Image.TRANSPOSE)
    if o == "rot90cw":
        return im.rotate(-90, expand=True)
    if o == "rot90ccw":
        return im.rotate(90, expand=True)
    if o == "flipx":
        return im.transpose(Image.FLIP_LEFT_RIGHT)
    if o == "flipy":
        return im.transpose(Image.FLIP_TOP_BOTTOM)
    raise ValueError(f"Unknown orient: {orient}")


def main() -> int:
    ap = argparse.ArgumentParser(description="Spellcross RAW → PNG (v4)")
    ap.add_argument("input", type=Path)
    ap.add_argument("-o", "--out", type=Path, default=None)
    ap.add_argument("-p", "--pal", type=Path, default=None)
    ap.add_argument("--pal-offset", type=int, default=None)

    ap.add_argument("--format", choices=["auto", "8bpp", "packed4"], default="auto")
    ap.add_argument("-w", "--width", type=int, default=0)
    ap.add_argument("-H", "--height", type=int, default=0)
    ap.add_argument("--swap-nibbles", action="store_true")

    ap.add_argument("--transparent", type=str, default=None)
    ap.add_argument("--scale", type=int, default=1)

    ap.add_argument("--ui-layout", choices=["none", "buy", "hierarch"], default="none")
    ap.add_argument("--ui-orient", choices=["transpose", "rot90cw", "rot90ccw", "flipx", "flipy", "none"],
                    default="transpose")

    args = ap.parse_args()

    raw = args.input.read_bytes()

    # --- decode indices ---
    if args.ui_layout != "none":
        # UI mode: packed4 + col-major + orient
        if args.width and args.height:
            w, h = args.width, args.height
        else:
            # infer from packed4 size
            area = len(raw) * 2
            dims = infer_dims_by_area(area)
            if not dims:
                raise SystemExit("UI mode: can't infer dimensions; pass --width/--height")
            w, h = dims
        if len(raw) != (w * h) // 2:
            raise SystemExit(f"UI mode expects packed4: size {len(raw)} != {w*h//2} (=w*h/2)")
        idx = unpack_packed4(raw, swap_nibbles=args.swap_nibbles)
        idx = reorder_colmajor_to_rowmajor(idx, w, h)
        im = Image.frombytes("P", (w, h), idx)
        if args.pal:
            im.putpalette(parse_palette(args.pal, idx, args.pal_offset))
        im = apply_orient(im, args.ui_orient)
    else:
        if not (args.width and args.height):
            # infer from size for convenience
            if args.format == "packed4" or (args.format == "auto" and len(raw) % 2 == 0):
                dims = infer_dims_by_area(len(raw) * 2)
                if dims:
                    w, h = dims
                else:
                    dims = infer_dims_by_area(len(raw))
                    if not dims:
                        raise SystemExit("Can't infer dimensions; pass --width/--height")
                    w, h = dims
            else:
                dims = infer_dims_by_area(len(raw))
                if not dims:
                    raise SystemExit("Can't infer dimensions; pass --width/--height")
                w, h = dims
        else:
            w, h = args.width, args.height

        fmt = args.format
        if fmt == "auto":
            if len(raw) == w * h:
                fmt = "8bpp"
            elif len(raw) == (w * h) // 2:
                fmt = "packed4"
            else:
                # default to 8bpp (will error clearly)
                fmt = "8bpp"

        if fmt == "8bpp":
            if len(raw) != w * h:
                raise SystemExit(f"8bpp mismatch: {len(raw)} != {w*h}")
            idx = raw
        else:
            if len(raw) != (w * h) // 2:
                raise SystemExit(f"packed4 mismatch: {len(raw)} != {w*h//2}")
            idx = unpack_packed4(raw, swap_nibbles=args.swap_nibbles)

        im = Image.frombytes("P", (w, h), idx)
        if args.pal:
            im.putpalette(parse_palette(args.pal, idx, args.pal_offset))

    # --- transparency ---
    trans = parse_transparent(args.transparent)
    if trans:
        rgba = im.convert("RGBA")
        px = rgba.load()
        ww, hh = im.size
        idx_bytes = im.tobytes()
        for y in range(hh):
            row = y * ww
            for x in range(ww):
                if idx_bytes[row + x] in trans:
                    r, g, b, _a = px[x, y]
                    px[x, y] = (r, g, b, 0)
        im = rgba

    # --- scale ---
    scale = max(1, int(args.scale))
    if scale != 1:
        im = im.resize((im.size[0] * scale, im.size[1] * scale), resample=Image.NEAREST)

    out = args.out or args.input.with_name(args.input.stem + "_restored.png")
    im.save(out, format="PNG")
    print(f"OK: {args.input.name} -> {out.name} ({im.size[0]}x{im.size[1]})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
