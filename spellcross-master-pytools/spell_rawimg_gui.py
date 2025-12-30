#!/usr/bin/env python3
"""
spell_rawimg_gui.py

Mini GUI wrapper for converting Spellcross RAW 8-bit images (.bin) + palette (.PAL) into PNG.
- Select .bin input
- Optional .PAL (192B or 768B)
- Width/Height (optional; can infer)
- For 192B palettes: auto-infer palette base index or override (128/192/custom)
- Transparency indices (e.g. 0 or 0,255 or 0-15)
- Upscale factor (nearest)

Requires: Pillow (pip install pillow)
"""

from __future__ import annotations
import math
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path
from typing import List, Optional, Tuple

try:
    from PIL import Image
except ImportError:
    raise SystemExit("Missing dependency: Pillow. Install with: pip install pillow")


# ---------- core helpers (same logic as CLI tool) ----------

def read_bytes(path: Path) -> bytes:
    return path.read_bytes()

def infer_dims(n: int) -> Optional[Tuple[int, int]]:
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
        if w < h:
            return 1e8 + (h - w)
        ar = w / h
        d = min(abs(ar - 4/3), abs(ar - 3/2), abs(ar - 16/9))
        size_pen = 0.0
        if w*h < 1500:
            size_pen += 0.5
        if w*h > 2_000_000:
            size_pen += 0.5
        return d + size_pen

    for w in common_widths:
        if n % w == 0:
            h = n // w
            candidates.append((score(w, h), w, h))

    root = int(math.sqrt(n))
    for w in range(max(1, root - 500), root + 501):
        if n % w == 0:
            h = n // w
            candidates.append((score(w, h), w, h))

    if not candidates:
        return None
    candidates.sort(key=lambda x: x[0])
    return candidates[0][1], candidates[0][2]

def parse_palette(pal_bytes: bytes, img_bytes: bytes, offset_override: Optional[int] = None) -> List[int]:
    full = []
    for i in range(256):
        full += [i, i, i]

    if len(pal_bytes) == 768:
        p = list(pal_bytes[:768])
        return [max(0, min(255, x)) for x in p]

    if len(pal_bytes) != 192:
        raise ValueError(f"Unsupported palette size: {len(pal_bytes)} bytes (expected 192 or 768)")

    chunk = [max(0, min(255, x)) for x in list(pal_bytes)]

    if offset_override is not None:
        base = offset_override
    else:
        used = set(img_bytes)
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
                base = int(round(mn / 64)) * 64
                base = max(0, min(192, base))

    if base % 64 != 0:
        base = (base // 64) * 64

    start = base * 3
    full[start:start + 192] = chunk
    return full

def parse_transparent_indices(s: str) -> List[int]:
    s = (s or "").strip()
    if not s:
        return []
    out: List[int] = []
    for part in s.replace(";", ",").split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            a, b = part.split("-", 1)
            a_i = int(a.strip()); b_i = int(b.strip())
            step = 1 if b_i >= a_i else -1
            out.extend(list(range(a_i, b_i + step, step)))
        else:
            out.append(int(part))
    out = [max(0, min(255, i)) for i in out]
    seen = set()
    uniq = []
    for i in out:
        if i not in seen:
            uniq.append(i); seen.add(i)
    return uniq

def convert_to_png(
    bin_path: Path,
    pal_path: Optional[Path],
    w: int,
    h: int,
    pal_offset: Optional[int],
    transparent: List[int],
    scale: int,
    out_path: Path
) -> Tuple[int, int]:
    img_bytes = read_bytes(bin_path)

    if w <= 0 or h <= 0:
        dims = infer_dims(len(img_bytes))
        if dims is None:
            raise ValueError("Neumím odhadnout rozměry. Zadej Width/Height ručně.")
        w, h = dims

    if len(img_bytes) != w * h:
        raise ValueError(f"Velikost nesedí: {len(img_bytes)} B != {w}×{h} ({w*h}).")

    im = Image.frombytes("P", (w, h), img_bytes)

    if pal_path:
        pal_bytes = read_bytes(pal_path)
        palette = parse_palette(pal_bytes, img_bytes, pal_offset)
        im.putpalette(palette)

    if transparent:
        rgba = im.convert("RGBA")
        pix = rgba.load()
        for y in range(h):
            row_off = y * w
            for x in range(w):
                idx = img_bytes[row_off + x]
                if idx in transparent:
                    r, g, b, _a = pix[x, y]
                    pix[x, y] = (r, g, b, 0)
        out_im = rgba
    else:
        out_im = im

    if scale and scale != 1:
        out_im = out_im.resize((w * scale, h * scale), resample=Image.NEAREST)

    out_im.save(out_path, format="PNG")
    return w, h


# ---------- GUI ----------

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Spellcross RAW → PNG (mini)")
        # self.geometry("720x480")
        self.resizable(False, False)

        self.bin_var = tk.StringVar()
        self.pal_var = tk.StringVar()
        self.out_var = tk.StringVar()

        self.w_var = tk.StringVar()
        self.h_var = tk.StringVar()

        self.pal_off_mode = tk.StringVar(value="auto")  # auto / 128 / 192 / custom
        self.pal_off_custom = tk.StringVar()

        self.transparent_var = tk.StringVar(value="0")
        self.scale_var = tk.StringVar(value="1")

        self.status_var = tk.StringVar(value="Vyber .bin a volitelně .PAL…")

        self._build()

    def _build(self):
        pad = 10
        frm = ttk.Frame(self, padding=pad)
        frm.pack(fill="both", expand=True)

        # Input BIN
        ttk.Label(frm, text="Input .bin (RAW 8-bit):").grid(row=0, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.bin_var, width=70).grid(row=1, column=0, sticky="w")
        ttk.Button(frm, text="Vybrat…", command=self.pick_bin).grid(row=1, column=1, padx=8)

        # Palette
        ttk.Label(frm, text="Palette .PAL (volitelně, 192B/768B):").grid(row=2, column=0, sticky="w", pady=(10,0))
        ttk.Entry(frm, textvariable=self.pal_var, width=70).grid(row=3, column=0, sticky="w")
        ttk.Button(frm, text="Vybrat…", command=self.pick_pal).grid(row=3, column=1, padx=8)

        # Output
        ttk.Label(frm, text="Output .png:").grid(row=4, column=0, sticky="w", pady=(10,0))
        ttk.Entry(frm, textvariable=self.out_var, width=70).grid(row=5, column=0, sticky="w")
        ttk.Button(frm, text="Uložit jako…", command=self.pick_out).grid(row=5, column=1, padx=8)

        # Dimensions + scale + transparency
        dims = ttk.LabelFrame(frm, text="Parametry", padding=10)
        dims.grid(row=6, column=0, columnspan=2, sticky="we", pady=(12, 0))

        ttk.Label(dims, text="Width:").grid(row=0, column=0, sticky="w")
        ttk.Entry(dims, textvariable=self.w_var, width=8).grid(row=0, column=1, sticky="w", padx=(6, 18))
        ttk.Label(dims, text="Height:").grid(row=0, column=2, sticky="w")
        ttk.Entry(dims, textvariable=self.h_var, width=8).grid(row=0, column=3, sticky="w", padx=(6, 18))
        ttk.Label(dims, text="(nech prázdné = auto infer)").grid(row=0, column=4, sticky="w")

        ttk.Label(dims, text="Scale:").grid(row=1, column=0, sticky="w", pady=(8,0))
        scale = ttk.Combobox(dims, textvariable=self.scale_var, width=6, values=["1","2","3","4","5","6","8"], state="readonly")
        scale.grid(row=1, column=1, sticky="w", padx=(6,18), pady=(8,0))

        ttk.Label(dims, text="Transparent idx:").grid(row=1, column=2, sticky="w", pady=(8,0))
        ttk.Entry(dims, textvariable=self.transparent_var, width=18).grid(row=1, column=3, sticky="w", padx=(6,18), pady=(8,0))
        ttk.Label(dims, text="(např. 0 nebo 0,255 nebo 0-15)").grid(row=1, column=4, sticky="w", pady=(8,0))

        # Palette offset options
        palbox = ttk.LabelFrame(frm, text="PAL offset (jen pro 192B)", padding=10)
        palbox.grid(row=7, column=0, columnspan=2, sticky="we", pady=(12, 0))

        ttk.Radiobutton(palbox, text="Auto", variable=self.pal_off_mode, value="auto").grid(row=0, column=0, sticky="w")
        ttk.Radiobutton(palbox, text="128", variable=self.pal_off_mode, value="128").grid(row=0, column=1, sticky="w", padx=10)
        ttk.Radiobutton(palbox, text="192", variable=self.pal_off_mode, value="192").grid(row=0, column=2, sticky="w", padx=10)
        ttk.Radiobutton(palbox, text="Custom:", variable=self.pal_off_mode, value="custom").grid(row=0, column=3, sticky="w", padx=(10,0))
        ttk.Entry(palbox, textvariable=self.pal_off_custom, width=8).grid(row=0, column=4, sticky="w", padx=6)
        ttk.Label(palbox, text="(0..192, násobek 64)").grid(row=0, column=5, sticky="w", padx=8)

        # Buttons + status
        btns = ttk.Frame(frm)
        btns.grid(row=8, column=0, columnspan=2, sticky="we", pady=(14, 0))
        ttk.Button(btns, text="Konvertovat", command=self.do_convert).pack(side="left")
        ttk.Button(btns, text="Reset", command=self.reset).pack(side="left", padx=10)

        ttk.Label(frm, textvariable=self.status_var, foreground="#333").grid(row=9, column=0, columnspan=2, sticky="w", pady=(10,0))

    def pick_bin(self):
        p = filedialog.askopenfilename(title="Vyber .bin", filetypes=[("RAW .bin", "*.bin"), ("All files", "*.*")])
        if not p:
            return
        self.bin_var.set(p)
        bin_path = Path(p)
        if not self.out_var.get().strip():
            self.out_var.set(str(bin_path.with_name(bin_path.stem + "_restored.png")))
        self._autofill_dims(bin_path)

    def pick_pal(self):
        p = filedialog.askopenfilename(title="Vyber .PAL", filetypes=[("Palette .pal", "*.pal;*.PAL"), ("All files", "*.*")])
        if not p:
            return
        self.pal_var.set(p)

    def pick_out(self):
        default = self.out_var.get().strip() or "output.png"
        p = filedialog.asksaveasfilename(title="Uložit PNG", defaultextension=".png",
                                         initialfile=Path(default).name,
                                         filetypes=[("PNG", "*.png")])
        if not p:
            return
        self.out_var.set(p)

    def _autofill_dims(self, bin_path: Path):
        try:
            n = bin_path.stat().st_size
            dims = infer_dims(n)
            if dims:
                w, h = dims
                # only fill if empty
                if not self.w_var.get().strip() and not self.h_var.get().strip():
                    self.w_var.set(str(w))
                    self.h_var.set(str(h))
                    self.status_var.set(f"Odhad rozměrů: {w}×{h} (z {n} B)")
                else:
                    self.status_var.set(f"Soubor {n} B, odhad: {w}×{h}")
            else:
                self.status_var.set(f"Soubor {n} B (rozměry neodhadnuty)")
        except Exception as e:
            self.status_var.set(f"Nelze odhadnout rozměry: {e}")

    def reset(self):
        self.bin_var.set("")
        self.pal_var.set("")
        self.out_var.set("")
        self.w_var.set("")
        self.h_var.set("")
        self.transparent_var.set("0")
        self.scale_var.set("1")
        self.pal_off_mode.set("auto")
        self.pal_off_custom.set("")
        self.status_var.set("Vyber .bin a volitelně .PAL…")

    def do_convert(self):
        try:
            bin_path = Path(self.bin_var.get().strip())
            if not bin_path.exists():
                raise ValueError("Vyber platný input .bin soubor.")

            pal_txt = self.pal_var.get().strip()
            pal_path = Path(pal_txt) if pal_txt else None
            if pal_path and not pal_path.exists():
                raise ValueError("PAL soubor neexistuje.")

            out_txt = self.out_var.get().strip()
            if not out_txt:
                out_txt = str(bin_path.with_name(bin_path.stem + "_restored.png"))
                self.out_var.set(out_txt)
            out_path = Path(out_txt)

            w = int(self.w_var.get().strip()) if self.w_var.get().strip() else 0
            h = int(self.h_var.get().strip()) if self.h_var.get().strip() else 0

            trans = parse_transparent_indices(self.transparent_var.get())
            scale = int(self.scale_var.get())

            pal_off = None
            mode = self.pal_off_mode.get()
            if mode == "128":
                pal_off = 128
            elif mode == "192":
                pal_off = 192
            elif mode == "custom":
                if not self.pal_off_custom.get().strip():
                    raise ValueError("Zvolil jsi Custom PAL offset, ale nezadal hodnotu.")
                pal_off = int(self.pal_off_custom.get().strip())

            w2, h2 = convert_to_png(
                bin_path=bin_path,
                pal_path=pal_path,
                w=w, h=h,
                pal_offset=pal_off,
                transparent=trans,
                scale=scale,
                out_path=out_path
            )

            self.status_var.set(f"OK: {out_path.name} ({w2}×{h2})" + (f" scale×{scale}" if scale != 1 else ""))
            messagebox.showinfo("Hotovo", f"Uloženo:\n{out_path}\n\nRozměr: {w2}×{h2}\nScale: {scale}")
        except Exception as e:
            messagebox.showerror("Chyba", str(e))
            self.status_var.set(f"Chyba: {e}")


def main():
    # better default theme on Windows
    try:
        from ctypes import windll
        windll.shcore.SetProcessDpiAwareness(1)
    except Exception:
        pass
    app = App()
    app.mainloop()


if __name__ == "__main__":
    main()
