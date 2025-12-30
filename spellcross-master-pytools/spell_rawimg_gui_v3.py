#!/usr/bin/env python3
"""
spell_rawimg_gui_v3.py

Mini GUI for Spellcross RAW -> PNG converter (v3).
Adds planar4 support (VGA 4-bitplanes).

Requires: Pillow (pip install pillow)
"""

from __future__ import annotations
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path
from typing import List, Optional

from PIL import Image

# --- import core logic from v3 tool (copy small subset) ---

def read_bytes(path: Path) -> bytes:
    return path.read_bytes()

def _maybe_scale_vga6(values: List[int]) -> List[int]:
    if values and max(values) <= 63:
        return [min(255, v * 4) for v in values]
    return values

def parse_palette(pal_bytes: bytes, img8: bytes, offset_override: Optional[int] = None) -> List[int]:
    full: List[int] = []
    for i in range(256):
        full += [i, i, i]

    if len(pal_bytes) == 768:
        p = [max(0, min(255, x)) for x in list(pal_bytes[:768])]
        return _maybe_scale_vga6(p)

    if len(pal_bytes) == 96:
        chunk = [max(0, min(255, x)) for x in list(pal_bytes)]
        chunk = _maybe_scale_vga6(chunk)
        full[0:96] = chunk
        return full

    if len(pal_bytes) != 192:
        raise ValueError(f"Unsupported palette size: {len(pal_bytes)} bytes (expected 96/192/768).")

    chunk = [max(0, min(255, x)) for x in list(pal_bytes)]
    chunk = _maybe_scale_vga6(chunk)

    if offset_override is not None:
        base = offset_override
    else:
        used = set(img8); used.discard(0)
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
    full[base * 3: base * 3 + 192] = chunk
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
    uniq = []
    seen = set()
    for i in out:
        if i not in seen:
            uniq.append(i); seen.add(i)
    return uniq

def unpack_4bpp_packed(packed: bytes) -> bytes:
    out = bytearray(len(packed) * 2)
    j = 0
    for b in packed:
        out[j] = (b >> 4) & 0x0F
        out[j + 1] = b & 0x0F
        j += 2
    return bytes(out)

def unpack_planar4(raw: bytes, width: int, height: int, layout: str = "rows") -> bytes:
    if width % 8 != 0:
        raise ValueError("planar4 requires width multiple of 8")
    bpr = width // 8
    expected = 4 * bpr * height
    if len(raw) != expected:
        raise ValueError(f"planar4 size mismatch: {len(raw)} != {expected}")
    out = bytearray(width * height)

    if layout == "planes":
        plane_size = bpr * height
        planes = [raw[i * plane_size:(i + 1) * plane_size] for i in range(4)]
        for y in range(height):
            row_off = y * width
            for xb in range(bpr):
                bp = [planes[p][y * bpr + xb] for p in range(4)]
                for bit in range(8):
                    mask = 1 << (7 - bit)
                    v = 0
                    if bp[0] & mask: v |= 1
                    if bp[1] & mask: v |= 2
                    if bp[2] & mask: v |= 4
                    if bp[3] & mask: v |= 8
                    out[row_off + xb * 8 + bit] = v
        return bytes(out)

    pos = 0
    for y in range(height):
        row_planes = [raw[pos + p * bpr: pos + (p + 1) * bpr] for p in range(4)]
        pos += 4 * bpr
        row_off = y * width
        for xb in range(bpr):
            bp = [row_planes[p][xb] for p in range(4)]
            for bit in range(8):
                mask = 1 << (7 - bit)
                v = 0
                if bp[0] & mask: v |= 1
                if bp[1] & mask: v |= 2
                if bp[2] & mask: v |= 4
                if bp[3] & mask: v |= 8
                out[row_off + xb * 8 + bit] = v
    return bytes(out)


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Spellcross RAW → PNG (mini) v3")
        self.resizable(False, False)

        self.bin_var = tk.StringVar()
        self.pal_var = tk.StringVar()
        self.out_var = tk.StringVar()
        self.w_var = tk.StringVar()
        self.h_var = tk.StringVar()
        self.format_var = tk.StringVar(value="auto")
        self.planar_layout_var = tk.StringVar(value="rows")
        self.transparent_var = tk.StringVar(value="0")
        self.scale_var = tk.StringVar(value="1")
        self.status_var = tk.StringVar(value="Vyber .bin a volitelně .PAL…")

        self._build()
        self.update_idletasks()
        self.minsize(self.winfo_width(), self.winfo_height())

    def _build(self):
        frm = ttk.Frame(self, padding=10)
        frm.pack(fill="both", expand=True)

        ttk.Label(frm, text="Input .bin:").grid(row=0, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.bin_var, width=70).grid(row=1, column=0, sticky="w")
        ttk.Button(frm, text="Vybrat…", command=self.pick_bin).grid(row=1, column=1, padx=8)

        ttk.Label(frm, text="Palette .PAL (volitelně, 96/192/768B):").grid(row=2, column=0, sticky="w", pady=(10,0))
        ttk.Entry(frm, textvariable=self.pal_var, width=70).grid(row=3, column=0, sticky="w")
        ttk.Button(frm, text="Vybrat…", command=self.pick_pal).grid(row=3, column=1, padx=8)

        ttk.Label(frm, text="Output .png:").grid(row=4, column=0, sticky="w", pady=(10,0))
        ttk.Entry(frm, textvariable=self.out_var, width=70).grid(row=5, column=0, sticky="w")
        ttk.Button(frm, text="Uložit jako…", command=self.pick_out).grid(row=5, column=1, padx=8)

        params = ttk.LabelFrame(frm, text="Parametry", padding=10)
        params.grid(row=6, column=0, columnspan=2, sticky="we", pady=(12,0))

        ttk.Label(params, text="Width:").grid(row=0, column=0, sticky="w")
        ttk.Entry(params, textvariable=self.w_var, width=8).grid(row=0, column=1, sticky="w", padx=(6,18))
        ttk.Label(params, text="Height:").grid(row=0, column=2, sticky="w")
        ttk.Entry(params, textvariable=self.h_var, width=8).grid(row=0, column=3, sticky="w", padx=(6,18))
        ttk.Label(params, text="(u auto formátu můžeš nechat prázdné)").grid(row=0, column=4, sticky="w")

        ttk.Label(params, text="Format:").grid(row=1, column=0, sticky="w", pady=(8,0))
        ttk.Combobox(params, textvariable=self.format_var, width=10,
                     values=["auto","8bpp","packed4","planar4"], state="readonly")\
            .grid(row=1, column=1, sticky="w", padx=(6,18), pady=(8,0))

        ttk.Label(params, text="Planar layout:").grid(row=1, column=2, sticky="w", pady=(8,0))
        ttk.Combobox(params, textvariable=self.planar_layout_var, width=10,
                     values=["rows","planes"], state="readonly")\
            .grid(row=1, column=3, sticky="w", padx=(6,18), pady=(8,0))
        ttk.Label(params, text="(jen pro planar4)").grid(row=1, column=4, sticky="w", pady=(8,0))

        ttk.Label(params, text="Scale:").grid(row=2, column=0, sticky="w", pady=(8,0))
        ttk.Combobox(params, textvariable=self.scale_var, width=6,
                     values=["1","2","3","4","5","6","8"], state="readonly")\
            .grid(row=2, column=1, sticky="w", padx=(6,18), pady=(8,0))

        ttk.Label(params, text="Transparent idx:").grid(row=2, column=2, sticky="w", pady=(8,0))
        ttk.Entry(params, textvariable=self.transparent_var, width=18).grid(row=2, column=3, sticky="w", padx=(6,18), pady=(8,0))
        ttk.Label(params, text="(0 nebo 0,255 nebo 0-15)").grid(row=2, column=4, sticky="w", pady=(8,0))

        btns = ttk.Frame(frm)
        btns.grid(row=7, column=0, columnspan=2, sticky="we", pady=(14,0))
        ttk.Button(btns, text="Konvertovat", command=self.do_convert).pack(side="left")
        ttk.Button(btns, text="Reset", command=self.reset).pack(side="left", padx=10)

        ttk.Label(frm, textvariable=self.status_var).grid(row=8, column=0, columnspan=2, sticky="w", pady=(10,0))

    def pick_bin(self):
        p = filedialog.askopenfilename(title="Vyber .bin", filetypes=[("RAW .bin","*.bin"), ("All files","*.*")])
        if not p: return
        self.bin_var.set(p)
        bp = Path(p)
        if not self.out_var.get().strip():
            self.out_var.set(str(bp.with_name(bp.stem + "_restored.png")))
        self.status_var.set(f"BIN: {bp.name} ({bp.stat().st_size} B)")

    def pick_pal(self):
        p = filedialog.askopenfilename(title="Vyber .PAL", filetypes=[("Palette .pal","*.pal;*.PAL"), ("All files","*.*")])
        if not p: return
        pp = Path(p)
        self.pal_var.set(p)
        self.status_var.set(f"PAL: {pp.name} ({pp.stat().st_size} B)")

    def pick_out(self):
        default = self.out_var.get().strip() or "output.png"
        p = filedialog.asksaveasfilename(title="Uložit PNG", defaultextension=".png",
                                         initialfile=Path(default).name, filetypes=[("PNG","*.png")])
        if not p: return
        self.out_var.set(p)

    def reset(self):
        self.bin_var.set(""); self.pal_var.set(""); self.out_var.set("")
        self.w_var.set(""); self.h_var.set("")
        self.format_var.set("auto"); self.planar_layout_var.set("rows")
        self.transparent_var.set("0"); self.scale_var.set("1")
        self.status_var.set("Vyber .bin a volitelně .PAL…")

    def do_convert(self):
        try:
            bin_path = Path(self.bin_var.get().strip())
            if not bin_path.exists():
                raise ValueError("Vyber platný input .bin soubor.")
            raw = read_bytes(bin_path)

            pal_txt = self.pal_var.get().strip()
            pal_path = Path(pal_txt) if pal_txt else None
            if pal_path and not pal_path.exists():
                raise ValueError("PAL soubor neexistuje.")

            out_txt = self.out_var.get().strip() or str(bin_path.with_name(bin_path.stem + "_restored.png"))
            self.out_var.set(out_txt)
            out_path = Path(out_txt)

            w = int(self.w_var.get().strip()) if self.w_var.get().strip() else 0
            h = int(self.h_var.get().strip()) if self.h_var.get().strip() else 0
            fmt = self.format_var.get().strip()
            layout = self.planar_layout_var.get().strip()

            # --- auto infer for planar4 only (simple: if size matches 4*(w/8)*h with common widths) ---
            if fmt == "auto":
                # prefer planar4 if any common width matches
                found = None
                for cw in range(64, 2049, 8):
                    bpr = cw // 8
                    denom = 4 * bpr
                    if denom and len(raw) % denom == 0:
                        ch = len(raw) // denom
                        if 50 <= ch <= 2000:
                            found = (cw, ch)
                            break
                if found and not w and not h:
                    w, h = found
                    fmt = "planar4"
                elif not w or not h:
                    raise ValueError("Auto bez rozměrů: zadej Width/Height (nebo použij CLI v3 pro lepší infer).")

            if not w or not h:
                raise ValueError("Zadej Width/Height (v3 GUI má auto infer jen omezeně).")

            if fmt == "8bpp":
                if len(raw) != w*h:
                    raise ValueError("8bpp: size != w*h")
                img8 = raw
            elif fmt == "packed4":
                if len(raw) != (w*h)//2:
                    raise ValueError("packed4: size != w*h/2")
                img8 = unpack_4bpp_packed(raw)
            elif fmt == "planar4":
                img8 = unpack_planar4(raw, w, h, layout=layout)
            else:
                raise ValueError("Neznámý format")

            palette = None
            if pal_path:
                pal_bytes = read_bytes(pal_path)
                palette = parse_palette(pal_bytes, img8)

            trans = parse_transparent_indices(self.transparent_var.get())
            scale = max(1, int(self.scale_var.get()))

            im = Image.frombytes("P", (w, h), img8)
            if palette is not None:
                im.putpalette(palette)

            if trans:
                rgba = im.convert("RGBA")
                pix = rgba.load()
                for y in range(h):
                    row = y * w
                    for x in range(w):
                        if img8[row + x] in trans:
                            r, g, b, _a = pix[x, y]
                            pix[x, y] = (r, g, b, 0)
                out_im = rgba
            else:
                out_im = im

            if scale != 1:
                out_im = out_im.resize((w*scale, h*scale), resample=Image.NEAREST)

            out_im.save(out_path, format="PNG")
            self.status_var.set(f"OK: {out_path.name} ({w}×{h}, {fmt})")
            messagebox.showinfo("Hotovo", f"Uloženo:\n{out_path}\n\nRozměr: {w}×{h}\nFormat: {fmt}")
        except Exception as e:
            messagebox.showerror("Chyba", str(e))
            self.status_var.set(f"Chyba: {e}")


def main():
    try:
        from ctypes import windll
        windll.shcore.SetProcessDpiAwareness(1)
    except Exception:
        pass
    App().mainloop()


if __name__ == "__main__":
    main()
