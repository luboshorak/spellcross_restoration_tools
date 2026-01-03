#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
spellcross_level_tool.py
UI tool pro Spellcross level vrstvy: LEVEL_0X.bin + HMLA__0X.bin + LEVEL_0X.PAL + LEVEL_0X.CLK (+ SSD volitelně)

Závislosti:
  pip install pillow
"""

from __future__ import annotations
import os, re, struct
from dataclasses import dataclass
from typing import Optional, Tuple

import tkinter as tk
from tkinter import ttk, filedialog, messagebox

from PIL import Image
import numpy as np


# ---------- Core decode ----------

def load_bytes(path: str) -> bytes:
    with open(path, "rb") as f:
        return f.read()

def palette_expand_to_256(pal_rgb: bytes) -> bytes:
    """
    Accept 96 (32c), 192 (64c), 768 (256c) palette. Return 768 bytes for putpalette().
    """
    if len(pal_rgb) == 768:
        return pal_rgb[:768]
    if len(pal_rgb) == 192:
        cols = [pal_rgb[i*3:(i+1)*3] for i in range(64)]
        out = bytearray()
        for i in range(256):
            out += cols[i % 64]
        return bytes(out[:768])
    if len(pal_rgb) == 96:
        cols = [pal_rgb[i*3:(i+1)*3] for i in range(32)]
        out = bytearray()
        for i in range(256):
            out += cols[i % 32]
        return bytes(out[:768])
    raise ValueError(f"Unsupported palette size: {len(pal_rgb)}")

def render_indexed_raw(pixels: bytes, w: int, h: int, pal256: bytes) -> Image.Image:
    imgL = Image.frombytes("L", (w, h), pixels[:w*h])
    pimg = imgL.convert("P")
    pimg.putpalette(pal256)
    return pimg

def decode_clk(clk_bytes: bytes) -> Tuple[int, int, np.ndarray]:
    """
    CLK = uint16 H, uint16 W, then H*uint16 row offsets.
    Each row: pairs (len:uint8, val:uint8) RLE, total len == W.
    Returns (W, H, values uint8[H,W])
    """
    H = struct.unpack_from("<H", clk_bytes, 0)[0]
    W = struct.unpack_from("<H", clk_bytes, 2)[0]
    offsets = [struct.unpack_from("<H", clk_bytes, 4 + 2*i)[0] for i in range(H)]

    out = np.zeros((H, W), dtype=np.uint8)
    for y in range(H):
        start = offsets[y]
        end = offsets[y+1] if y+1 < H else len(clk_bytes)
        row = clk_bytes[start:end]

        x = 0
        for i in range(0, len(row), 2):
            if i + 2 > len(row):
                break
            run_len = row[i]
            val = row[i+1]
            if run_len == 0:
                continue
            x2 = min(W, x + run_len)
            out[y, x:x2] = val
            x = x2
            if x >= W:
                break

    return W, H, out

def values_to_colored(values: np.ndarray) -> Image.Image:
    """
    P-mode image with deterministic debug palette (value -> color).
    """
    pimg = Image.fromarray(values, mode="P")
    pal = bytearray()
    for i in range(256):
        pal += bytes([(i*47) % 256, (i*91) % 256, (i*137) % 256])
    pimg.putpalette(bytes(pal[:768]))
    return pimg

def clk_outline_overlay(clk_values: np.ndarray, inside_mask: Optional[np.ndarray] = None, thickness: int = 2) -> np.ndarray:
    """
    Create a boolean edge map where neighboring CLK values differ (region boundaries).
    If inside_mask is provided (bool[H,W]), edges are limited to that area.
    thickness >=1 inflates the edges a bit for visibility.
    Returns bool[H,W].
    """
    v = clk_values
    H, W = v.shape
    edge = np.zeros((H, W), dtype=bool)
    edge[:, 1:] |= (v[:, 1:] != v[:, :-1])
    edge[1:, :] |= (v[1:, :] != v[:-1, :])

    if inside_mask is not None:
        edge &= inside_mask

    if thickness and thickness > 1:
        e = edge.astype(np.uint8)
        for _ in range(thickness - 1):
            e2 = e.copy()
            e2[:, 1:] |= e[:, :-1]
            e2[:, :-1] |= e[:, 1:]
            e2[1:, :] |= e[:-1, :]
            e2[:-1, :] |= e[1:, :]
            e = e2.astype(np.uint8)
        edge = e.astype(bool)

    return edge


def composite_fog_level(
    level_rgba: np.ndarray,
    fog_rgba: np.ndarray,
    clk_values: np.ndarray,
    fog_darken: float,
    outline: bool
) -> Image.Image:
    """
    Compose:
      - background = HMLA (fog), slightly darkened
      - foreground = LEVEL
      - mask = CLK inverted (inside = clk_values == 0)  <-- as observed in-game for LEVEL_02
      - optional outline = region boundaries (transparent interiors)
    """
    fog = fog_rgba.astype(np.float32).copy()
    fog[..., :3] *= float(fog_darken)
    fog[..., 3] = 255
    fog = np.clip(fog, 0, 255).astype(np.uint8)

    out = fog.copy()

    # IMPORTANT: inverted CLK mask (inside map where value == 0)
    mask_inside = (clk_values == 0)
    out[mask_inside] = level_rgba[mask_inside]

    if outline:
        edge = clk_outline_overlay(clk_values, inside_mask=mask_inside, thickness=1)
        # draw white boundary lines
        out[edge] = [255, 255, 255, 255]

    return Image.fromarray(out, mode="RGBA")


# ---------- Model ----------

@dataclass
class LevelSet:
    level_bin: str
    hmla_bin: str
    pal: str
    clk: str
    ssd: Optional[str] = None

def find_levelset(folder: str, level_num: Optional[int] = None) -> Optional[LevelSet]:
    """
    Najde LEVEL_0X.bin, HMLA__0X.bin, LEVEL_0X.PAL, LEVEL_0X.CLK (+ SSD) ve složce.
    Pokud level_num není zadané, vezme první nalezený.
    """
    files = {fn.upper(): os.path.join(folder, fn) for fn in os.listdir(folder)}
    # pick candidate LEVEL_??.BIN
    candidates = []
    for fn_u, p in files.items():
        m = re.match(r"LEVEL_(\d{2})\.BIN$", fn_u)
        if m:
            candidates.append((int(m.group(1)), p))
    candidates.sort()
    if not candidates:
        return None

    if level_num is None:
        ln, level_path = candidates[0]
    else:
        level_path = None
        ln = level_num
        for n, p in candidates:
            if n == level_num:
                level_path = p
                break
        if not level_path:
            return None

    # expected companions
    hmla_key = f"HMLA__{ln:02d}.BIN"
    pal_key  = f"LEVEL_{ln:02d}.PAL"
    clk_key  = f"LEVEL_{ln:02d}.CLK"
    ssd_key  = f"LEVEL_{ln:02d}.SSD"

    if hmla_key not in files or pal_key not in files or clk_key not in files:
        return None

    return LevelSet(
        level_bin=level_path,
        hmla_bin=files[hmla_key],
        pal=files[pal_key],
        clk=files[clk_key],
        ssd=files.get(ssd_key)
    )

# ---------- UI ----------

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Spellcross Level Tool (LEVEL/HMLA/PAL/CLK)")
        self.geometry("1120x720")
        self.minsize(980, 620)

        self.folder: Optional[str] = None
        self.level_num: Optional[int] = None
        self.levelset: Optional[LevelSet] = None

        self.out_dir = os.path.abspath(os.path.join(os.getcwd(), "level_out"))
        self.fog_darken = tk.DoubleVar(value=0.82)
        self.draw_outline = tk.BooleanVar(value=False)

        self._build()

    def _build(self):
        top = ttk.Frame(self)
        top.pack(side=tk.TOP, fill=tk.X, padx=10, pady=10)

        ttk.Button(top, text="Select folder…", command=self.pick_folder).pack(side=tk.LEFT)
        ttk.Label(top, text="Level # (optional):").pack(side=tk.LEFT, padx=(12, 6))
        self.level_entry = ttk.Entry(top, width=6)
        self.level_entry.pack(side=tk.LEFT)
        self.level_entry.insert(0, "")

        ttk.Button(top, text="Auto-detect", command=self.autodetect).pack(side=tk.LEFT, padx=(8, 0))
        ttk.Button(top, text="Render + Export", command=self.render_export).pack(side=tk.LEFT, padx=(8, 0))

        ttk.Separator(top, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=12)

        ttk.Label(top, text="Fog darken:").pack(side=tk.LEFT)
        ttk.Scale(top, from_=0.55, to=1.0, variable=self.fog_darken, orient="horizontal", length=180).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Checkbutton(top, text="Region outline", variable=self.draw_outline).pack(side=tk.LEFT, padx=(12, 0))

        ttk.Separator(top, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=12)

        ttk.Label(top, text="Output:").pack(side=tk.LEFT)
        self.out_var = tk.StringVar(value=self.out_dir)
        ttk.Entry(top, textvariable=self.out_var, width=42).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Button(top, text="…", width=3, command=self.pick_out).pack(side=tk.LEFT, padx=(4, 0))

        mid = ttk.Frame(self)
        mid.pack(fill=tk.BOTH, expand=True, padx=10, pady=(0, 10))

        self.log = tk.Text(mid, wrap="word")
        self.log.pack(fill=tk.BOTH, expand=True)

        self._log("Ready. Select folder and Auto-detect.")

    def _log(self, s: str):
        self.log.insert(tk.END, s + "\n")
        self.log.see(tk.END)
        self.update_idletasks()

    def pick_folder(self):
        d = filedialog.askdirectory(title="Select level folder")
        if not d:
            return
        self.folder = d
        self._log(f"Folder: {d}")

    def pick_out(self):
        d = filedialog.askdirectory(title="Select output folder")
        if not d:
            return
        self.out_dir = d
        self.out_var.set(d)
        self._log(f"Output: {d}")

    def autodetect(self):
        if not self.folder:
            messagebox.showinfo("Spellcross Level Tool", "Select folder first.")
            return

        txt = self.level_entry.get().strip()
        ln = None
        if txt:
            try:
                ln = int(txt)
            except ValueError:
                messagebox.showerror("Invalid", "Level # must be integer (e.g., 2).")
                return

        ls = find_levelset(self.folder, ln)
        if not ls:
            messagebox.showerror("Not found", "Could not find LEVEL/HMLA/PAL/CLK set in folder.")
            return

        self.levelset = ls
        self._log("Detected:")
        self._log(f"  LEVEL: {os.path.basename(ls.level_bin)}")
        self._log(f"  HMLA : {os.path.basename(ls.hmla_bin)}")
        self._log(f"  PAL  : {os.path.basename(ls.pal)}")
        self._log(f"  CLK  : {os.path.basename(ls.clk)}")
        if ls.ssd:
            self._log(f"  SSD  : {os.path.basename(ls.ssd)}")

    def render_export(self):
        if not self.levelset:
            self.autodetect()
            if not self.levelset:
                return

        out_dir = self.out_var.get().strip() or self.out_dir
        os.makedirs(out_dir, exist_ok=True)

        ls = self.levelset

        level_b = load_bytes(ls.level_bin)
        hmla_b  = load_bytes(ls.hmla_bin)
        pal_b   = load_bytes(ls.pal)
        clk_b   = load_bytes(ls.clk)

        pal256 = palette_expand_to_256(pal_b)

        # Validate raster from file size (we already know 379x259 for these)
        W = 379
        H = 259
        # Use CLK as source of truth for dimensions
        clkW, clkH, clk_vals = decode_clk(clk_b)
        W, H = clkW, clkH
        need = W * H

        def normalize_pixels(buf: bytes, name: str) -> bytes:
            if len(buf) == need:
                return buf

            if len(buf) == need + 1:
                # try dropping first OR last byte; choose the one that gives cleaner outside-key
                outside = (clk_vals == 0)

                def score(candidate: bytes) -> int:
                    idx = np.frombuffer(candidate[:need], dtype=np.uint8).reshape(H, W)
                    # most frequent value in outside area should dominate strongly
                    vals, counts = np.unique(idx[outside], return_counts=True)
                    return int(counts.max()) if counts.size else 0

                a = buf[:need]  # drop last
                b = buf[1:need + 1]  # drop first
                return b if score(b) >= score(a) else a

            if len(buf) > need:
                # generic fallback: take first need bytes
                self._log(f"WARNING: {name} has {len(buf)} bytes, trimming to {need}.")
                return buf[:need]

            raise ValueError(f"{name} too small: {len(buf)} bytes, need {need}")

        try:
            level_b = normalize_pixels(level_b, "LEVEL")
            hmla_b = normalize_pixels(hmla_b, "HMLA")
        except ValueError as e:
            messagebox.showerror("Unexpected size", str(e))
            return

        # Render indexed PNGs (for debugging) + RGBA with transparent outside-map corners
        level_png = render_indexed_raw(level_b, W, H, pal256)
        hmla_png  = render_indexed_raw(hmla_b,  W, H, pal256)

        # Decode CLK
        clkW, clkH, clk_vals = decode_clk(clk_b)
        if (clkW, clkH) != (W, H):
            self._log(f"WARNING: CLK dims {clkW}x{clkH} != {W}x{H} (continuing)")

        # Masks / debug from CLK
        # Observed: inside-map mask is INVERTED -> inside where clk_vals == 0
        mask_inside = (clk_vals != 0)

        # 3) Black outline on transparent background (region boundaries only)
        # Region boundary edges (including border between inside/outside)
        edge = clk_outline_overlay(clk_vals, inside_mask=None, thickness=0.1)

        # keep edges that touch inside area (so outer border is drawn too)
        inside_u8 = mask_inside.astype(np.uint8)
        touch_inside = mask_inside.copy()
        touch_inside[:, 1:] |= inside_u8[:, :-1].astype(bool)
        touch_inside[:, :-1] |= inside_u8[:, 1:].astype(bool)
        touch_inside[1:, :] |= inside_u8[:-1, :].astype(bool)
        touch_inside[:-1, :] |= inside_u8[1:, :].astype(bool)

        edge &= touch_inside

        outline_rgba = np.zeros((H, W, 4), dtype=np.uint8)  # transparent background
        outline_rgba[edge] = [0, 0, 0, 255]                  # black lines
        p_clk_outline = os.path.join(out_dir, "REGIONS.png")
        Image.fromarray(outline_rgba, mode="RGBA").save(p_clk_outline)
        self._log(f"Saved: {p_clk_outline}")

        # Build RGBA layers with transparent outside-map corners (keep original colors inside)
        level_rgba = np.array(level_png.convert("RGBA"), dtype=np.uint8)
        hmla_rgba  = np.array(hmla_png.convert("RGBA"), dtype=np.uint8)

        # Determine "transparent key" color index separately for LEVEL and HMLA.
        # Use the most frequent palette index in OUTSIDE area (clk == 0) as the key (typically the salmon background).
        outside = ~mask_inside  # where clk == 0

        level_idx = np.frombuffer(level_b, dtype=np.uint8).reshape(H, W)
        hmla_idx  = np.frombuffer(hmla_b,  dtype=np.uint8).reshape(H, W)

        def most_frequent_value(arr: np.ndarray) -> int:
            # arr uint8
            if arr.size == 0:
                return 0
            vals, counts = np.unique(arr, return_counts=True)
            return int(vals[int(np.argmax(counts))])

        key_level = most_frequent_value(level_idx[outside])
        key_hmla  = most_frequent_value(hmla_idx[outside])

        self._log(f"Transparent key LEVEL index: {key_level}")
        self._log(f"Transparent key HMLA  index: {key_hmla}")

        alpha_level = (level_idx != key_level).astype(np.uint8) * 255
        alpha_hmla  = (hmla_idx  != key_hmla ).astype(np.uint8) * 255

        level_rgba[..., 3] = alpha_level
        hmla_rgba[..., 3]  = alpha_hmla

        # Slightly darken fog (decent, not too strong)
        fog_darken = float(self.fog_darken.get())
        hmla_rgba = hmla_rgba.astype(np.float32)
        hmla_rgba[..., :3] *= fog_darken
        hmla_rgba = np.clip(hmla_rgba, 0, 255).astype(np.uint8)

        # Save the 4 required outputs:
        # 1) LEVEL with transparent corners
        p_level = os.path.join(out_dir, "LEVEL.png")
        Image.fromarray(level_rgba, mode="RGBA").save(p_level)
        self._log(f"Saved: {p_level}")

        # 2) HMLA with transparent corners (darkened)
        p_hmla = os.path.join(out_dir, "HMLA.png")
        Image.fromarray(hmla_rgba, mode="RGBA").save(p_hmla)
        self._log(f"Saved: {p_hmla}")

        # 3) Region outlines already saved as CLK_outline_black.png (transparent background)

        # 4) Final composite = HMLA + LEVEL + outlines (pure alpha stacking)
        comp = Image.alpha_composite(Image.fromarray(hmla_rgba, mode="RGBA"), Image.fromarray(level_rgba, mode="RGBA"))
        comp = Image.alpha_composite(comp, Image.fromarray(outline_rgba, mode="RGBA"))
        p_comp = os.path.join(out_dir, "COMPOSITE.png")
        comp.save(p_comp)
        self._log(f"Saved: {p_comp}")
        messagebox.showinfo("Done", f"Exported to:{out_dir}")


def main():
    app = App()
    app.mainloop()

if __name__ == "__main__":
    main()
