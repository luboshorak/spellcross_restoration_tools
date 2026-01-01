#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from __future__ import annotations
import json
import struct
import subprocess
from pathlib import Path
import tkinter as tk

from PIL import Image, ImageTk
import numpy as np

ROOT = Path(".")

PAL_FILE = ROOT / "MAINMENU.PAL"

# můžeš mít .LZ nebo rovnou .bin
FILES = {
    "ui":  ROOT / "MAINMENU.bin",    # 640x480
    "bg":  ROOT / "MAINM_BG.bin",    # patch (u tebe 272x255)
    # zatím neřešíme MAINM_0/1, MAINMD0/1 – jsou pravděpodobně další patch-e / sprity
}

OUT_PNG = ROOT / "menu_base.png"
HITBOX_JSON = ROOT / "menu_hitboxes.json"

# -----------------------------
# optional delz support (když máš jen .LZ)
# -----------------------------
def ensure_bin(p: Path) -> Path:
    if p.exists() and p.suffix.lower() == ".bin":
        return p
    if p.exists() and p.suffix.lower() == ".lz":
        out = p.with_suffix(".bin")
        if out.exists():
            return out
        print(f"[i] delz {p.name} -> {out.name}")
        subprocess.check_call(["delz", str(p), str(out)])
        return out
    # když uživatel zadal .bin co neexistuje, zkus .lz
    if p.suffix.lower() == ".bin":
        alt = p.with_suffix(".lz")
        if alt.exists():
            return ensure_bin(alt)
    raise FileNotFoundError(p)

# -----------------------------
# palette
# -----------------------------
def load_pal(pal_path: Path) -> list[int]:
    pal = list(pal_path.read_bytes())
    if len(pal) != 768:
        raise ValueError(f"PAL must be 768 bytes, got {len(pal)}")
    # VGA 6-bit scaling
    if pal and max(pal) <= 63:
        pal = [min(255, v * 4) for v in pal]
    return pal

def rgb_of_index(pal_list: list[int], idx: int):
    base = idx * 3
    return tuple(pal_list[base:base+3])

# -----------------------------
# loaders
# -----------------------------
def imgP_from_raw(raw: bytes, size: tuple[int,int], pal: list[int]) -> Image.Image:
    img = Image.frombytes("P", size, raw)
    img.putpalette(pal)
    return img

def to_rgba_key(imgP: Image.Image, key_index: int) -> Image.Image:
    rgba = imgP.convert("RGBA")
    data = imgP.tobytes()
    px = rgba.load()
    w, h = imgP.size
    i = 0
    for y in range(h):
        for x in range(w):
            if data[i] == key_index:
                r, g, b, a = px[x, y]
                px[x, y] = (r, g, b, 0)
            i += 1
    return rgba

def most_common_index(imgP: Image.Image) -> int:
    data = imgP.tobytes()
    hist = [0]*256
    for b in data:
        hist[b] += 1
    return max(range(256), key=lambda i: hist[i])

# -----------------------------
# choose BG patch dimensions by scoring factor pairs
# -----------------------------
def guess_patch_dims(n: int, max_w: int, max_h: int) -> tuple[int,int]:
    # všechna (w,h) kde w*h=n
    cands = []
    for w in range(16, max_w+1):
        if n % w != 0:
            continue
        h = n // w
        if 16 <= h <= max_h:
            # skóre: preferuj "rozumné" patch-e (ne pruhy)
            score = 0.0
            # penalizuj extrémní pruhy
            aspect = w / h
            score += abs(aspect - 1.0) * 30  # patch často bývá cca “panel”
            if w < 80 or h < 60:
                score += 50
            if w > 520 or h > 420:
                score += 30
            # preferuj násobky 4/8
            if w % 8 == 0: score -= 5
            if h % 8 == 0: score -= 5
            cands.append((score, w, h))

    if not cands:
        raise ValueError(f"Cannot factor {n} into dims <= {max_w}x{max_h}")
    cands.sort(key=lambda x: x[0])
    return cands[0][1], cands[0][2]

# -----------------------------
# find best placement of patch inside UI transparency
# -----------------------------
def find_best_hole(ui_idx: np.ndarray, key: int, pw: int, ph: int) -> tuple[int,int,float]:
    """
    ui_idx: (H,W) uint8
    key: transparent index
    Returns (x,y,ratio_of_key_pixels) where ratio ~1 means it's mostly "hole"
    """
    mask = (ui_idx == key).astype(np.int32)
    ii = mask.cumsum(axis=0).cumsum(axis=1)

    def rect_sum(x, y, w, h):
        x2 = x + w - 1
        y2 = y + h - 1
        A = ii[y2, x2]
        B = ii[y-1, x2] if y > 0 else 0
        C = ii[y2, x-1] if x > 0 else 0
        D = ii[y-1, x-1] if (x > 0 and y > 0) else 0
        return A - B - C + D

    best_s = -1
    best_xy = (0, 0)
    total = pw * ph

    H, W = ui_idx.shape
    for y in range(0, H - ph + 1):
        for x in range(0, W - pw + 1):
            s = rect_sum(x, y, pw, ph)
            if s > best_s:
                best_s = s
                best_xy = (x, y)

    ratio = best_s / total if total else 0.0
    return best_xy[0], best_xy[1], ratio

# -----------------------------
# build base menu (UI fullscreen + BG patch under it)
# -----------------------------
def build_menu_base() -> Image.Image:
    if not PAL_FILE.exists():
        raise FileNotFoundError(PAL_FILE)

    pal = load_pal(PAL_FILE)

    ui_bin = ensure_bin(FILES["ui"])
    bg_bin = ensure_bin(FILES["bg"])

    ui_raw = ui_bin.read_bytes()
    if len(ui_raw) != 640 * 480:
        raise RuntimeError(f"MAINMENU.bin expected 640*480=307200 bytes, got {len(ui_raw)}")

    uiP = imgP_from_raw(ui_raw, (640, 480), pal)

    # transparent key for UI overlay
    ui_key = most_common_index(uiP)
    print(f"[i] UI key index={ui_key}, rgb={rgb_of_index(pal, ui_key)}")

    # BG patch dims
    bg_raw = bg_bin.read_bytes()
    pw, ph = guess_patch_dims(len(bg_raw), max_w=640, max_h=480)
    print(f"[i] BG patch dims guessed: {pw}x{ph} (bytes={len(bg_raw)})")

    bgP = imgP_from_raw(bg_raw, (pw, ph), pal)

    # find placement hole
    ui_idx = np.frombuffer(ui_raw, dtype=np.uint8).reshape((480, 640))
    x, y, ratio = find_best_hole(ui_idx, ui_key, pw, ph)
    print(f"[i] BG placement: x={x}, y={y}, hole_ratio={ratio:.3f}")

    # composite
    canvas = Image.new("RGBA", (640, 480), (0, 0, 0, 255))
    canvas.alpha_composite(bgP.convert("RGBA"), dest=(x, y))
    canvas.alpha_composite(to_rgba_key(uiP, ui_key), dest=(0, 0))
    return canvas

# -----------------------------
# Tk viewer + hitbox editor
# -----------------------------
class MenuViewer(tk.Tk):
    def __init__(self, base_img: Image.Image):
        super().__init__()
        self.title("Spellcross Menu Prototype (Tk)")

        self.base = base_img
        self.scale = 2
        self.show_hitboxes = True

        w, h = self.base.size
        self.canvas = tk.Canvas(self, width=w*self.scale, height=h*self.scale, bg="black", highlightthickness=0)
        self.canvas.pack(fill="both", expand=True)

        self.tk_img = None
        self.hitboxes = []
        self.load_hitboxes()

        self.drag_start = None
        self.drag_rect_id = None

        self.canvas.bind("<Button-1>", self.on_down)
        self.canvas.bind("<B1-Motion>", self.on_drag)
        self.canvas.bind("<ButtonRelease-1>", self.on_up)
        self.bind("<Key>", self.on_key)

        self.status = tk.Label(
            self,
            text="LMB drag = new hitbox | h toggle | backspace delete last | +/- scale | (auto-save)",
            anchor="w"
        )
        self.status.pack(fill="x")

        self.redraw()

    def redraw(self):
        img2 = self.base.resize((self.base.size[0]*self.scale, self.base.size[1]*self.scale), Image.NEAREST)
        self.tk_img = ImageTk.PhotoImage(img2)

        self.canvas.delete("all")
        self.canvas.create_image(0, 0, anchor="nw", image=self.tk_img)

        # HUD
        hud = f"hitboxes: {len(self.hitboxes)} | scale: {self.scale} | show: {self.show_hitboxes}"
        self.canvas.create_rectangle(6, 6, 6 + 320, 6 + 26, fill="black", outline="")
        self.canvas.create_text(12, 10, anchor="nw", text=hud, fill="white")

        if self.show_hitboxes:
            for hb in self.hitboxes:
                x1, y1, x2, y2 = hb["rect"]
                self.canvas.create_rectangle(
                    x1*self.scale, y1*self.scale, x2*self.scale, y2*self.scale,
                    outline="lime", width=2
                )
                self.canvas.create_text(
                    (x1+4)*self.scale, (y1+4)*self.scale,
                    anchor="nw", text=hb.get("name",""), fill="lime"
                )

    def on_key(self, e):
        k = e.keysym.lower()

        if k == "h":
            self.show_hitboxes = not self.show_hitboxes
            self.redraw()
            return

        if k == "backspace":
            if self.hitboxes:
                self.hitboxes.pop()
                self.save_hitboxes()   # auto-save
                self.redraw()
            return

        if k in ("plus", "equal"):
            self.scale = min(6, self.scale + 1)
            self.redraw()
            return

        if k == "minus":
            self.scale = max(1, self.scale - 1)
            self.redraw()
            return

    def on_down(self, e):
        x = e.x // self.scale
        y = e.y // self.scale
        self.drag_start = (x, y)
        self.drag_rect_id = self.canvas.create_rectangle(e.x, e.y, e.x, e.y, outline="yellow", width=2)

    def on_drag(self, e):
        if not self.drag_start or not self.drag_rect_id:
            return
        x0, y0 = self.drag_start
        x1 = e.x // self.scale
        y1 = e.y // self.scale
        self.canvas.coords(self.drag_rect_id, x0*self.scale, y0*self.scale, x1*self.scale, y1*self.scale)

    def on_up(self, e):
        if not self.drag_start:
            return
        x0, y0 = self.drag_start
        x1 = e.x // self.scale
        y1 = e.y // self.scale
        self.drag_start = None

        xa, xb = sorted([x0, x1])
        ya, yb = sorted([y0, y1])

        if (xb-xa) < 3 or (yb-ya) < 3:
            if self.drag_rect_id:
                self.canvas.delete(self.drag_rect_id)
            self.drag_rect_id = None
            return

        # ask for name immediately
        name = self.ask_name(default=f"item_{len(self.hitboxes)}")
        self.hitboxes.append({"name": name, "rect": [xa, ya, xb, yb]})

        if self.drag_rect_id:
            self.canvas.delete(self.drag_rect_id)
        self.drag_rect_id = None

        self.save_hitboxes()  # auto-save
        self.redraw()

    def ask_name(self, default: str) -> str:
        win = tk.Toplevel(self)
        win.title("Hitbox name")
        win.grab_set()

        tk.Label(win, text="Name:").pack(padx=10, pady=6)
        var = tk.StringVar(value=default)
        ent = tk.Entry(win, textvariable=var, width=30)
        ent.pack(padx=10, pady=6)
        ent.focus_set()

        result = {"name": default}

        def ok():
            result["name"] = var.get().strip() or default
            win.destroy()

        tk.Button(win, text="OK", command=ok).pack(padx=10, pady=10)
        self.wait_window(win)
        return result["name"]

    def load_hitboxes(self):
        if HITBOX_JSON.exists():
            try:
                self.hitboxes = json.loads(HITBOX_JSON.read_text(encoding="utf-8"))
            except Exception:
                self.hitboxes = []

    def save_hitboxes(self):
        HITBOX_JSON.write_text(json.dumps(self.hitboxes, indent=2), encoding="utf-8")

# -----------------------------
# main
# -----------------------------
def main():
    base = build_menu_base()
    base.save(OUT_PNG)
    print(f"[OK] {OUT_PNG.name} ({base.size[0]}x{base.size[1]})")
    MenuViewer(base).mainloop()

if __name__ == "__main__":
    main()
