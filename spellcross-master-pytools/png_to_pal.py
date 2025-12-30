#!/usr/bin/env python3
# screenshot_to_pal_ui.py
#
# Simple GUI tool: screenshot (PNG/JPG) -> 256-color .PAL (768 bytes, RGBRGB...)
#
# Requirements:
#   pip install pillow

import tkinter as tk
from tkinter import filedialog, messagebox
from pathlib import Path
from PIL import Image


def build_palette_from_image(img: Image.Image, colors: int = 256, sample_max: int = 1024,
                             composite_bg=(0, 0, 0)) -> list[int]:
    """
    Returns palette as list[int] length 768: [r,g,b,r,g,b,...] (256 colors).
    Uses Pillow adaptive quantization.
    """
    img = img.convert("RGBA")

    # Downsample for speed/stability
    w, h = img.size
    scale = min(1.0, sample_max / max(w, h))
    if scale < 1.0:
        img = img.resize(
            (max(1, int(w * scale)), max(1, int(h * scale))),
            Image.Resampling.LANCZOS
        )

    # Composite alpha over background
    bg = Image.new("RGBA", img.size, (*composite_bg, 255))
    img_rgb = Image.alpha_composite(bg, img).convert("RGB")

    # Quantize
    q = img_rgb.quantize(colors=colors, method=Image.Quantize.MEDIANCUT)
    pal = q.getpalette() or []
    pal = pal[: 256 * 3]
    if len(pal) < 256 * 3:
        pal += [0] * (256 * 3 - len(pal))
    return pal


def save_pal(path: Path, pal: list[int]) -> None:
    data = bytes(max(0, min(255, v)) for v in pal[:768])
    if len(data) != 768:
        raise RuntimeError(f"Internal error: palette length {len(data)} != 768")
    path.write_bytes(data)


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Screenshot → PAL (256 colors)")
        self.geometry("680x240")
        self.resizable(False, False)

        self.in_path = tk.StringVar(value="")
        self.out_path = tk.StringVar(value="")

        self.colors = tk.IntVar(value=256)
        self.sample_max = tk.IntVar(value=1024)
        self.index0_black = tk.BooleanVar(value=True)

        self.bg_choice = tk.StringVar(value="black")  # black / white

        pad = {"padx": 10, "pady": 6}

        # Input row
        frm_in = tk.Frame(self)
        frm_in.pack(fill="x", **pad)
        tk.Label(frm_in, text="Input screenshot:").pack(side="left")
        tk.Entry(frm_in, textvariable=self.in_path, width=60).pack(side="left", padx=8)
        tk.Button(frm_in, text="Browse…", command=self.browse_input).pack(side="left")

        # Output row
        frm_out = tk.Frame(self)
        frm_out.pack(fill="x", **pad)
        tk.Label(frm_out, text="Output .pal:").pack(side="left")
        tk.Entry(frm_out, textvariable=self.out_path, width=60).pack(side="left", padx=8)
        tk.Button(frm_out, text="Save as…", command=self.browse_output).pack(side="left")

        # Options row
        frm_opt = tk.Frame(self)
        frm_opt.pack(fill="x", **pad)

        tk.Label(frm_opt, text="Colors:").pack(side="left")
        colors_spin = tk.Spinbox(frm_opt, from_=2, to=256, textvariable=self.colors, width=5)
        colors_spin.pack(side="left", padx=(6, 16))

        tk.Label(frm_opt, text="Sample max:").pack(side="left")
        sample_spin = tk.Spinbox(frm_opt, from_=256, to=4096, increment=128, textvariable=self.sample_max, width=7)
        sample_spin.pack(side="left", padx=(6, 16))

        tk.Checkbutton(frm_opt, text="Force index 0 = black", variable=self.index0_black).pack(side="left")

        # Alpha background choice
        frm_bg = tk.Frame(self)
        frm_bg.pack(fill="x", **pad)
        tk.Label(frm_bg, text="If screenshot has transparency, composite over:").pack(side="left")
        tk.Radiobutton(frm_bg, text="black", variable=self.bg_choice, value="black").pack(side="left", padx=10)
        tk.Radiobutton(frm_bg, text="white", variable=self.bg_choice, value="white").pack(side="left")

        # Action buttons
        frm_btn = tk.Frame(self)
        frm_btn.pack(fill="x", padx=10, pady=14)
        tk.Button(frm_btn, text="Generate .PAL", command=self.generate, height=2, width=18).pack(side="left")
        tk.Button(frm_btn, text="Quit", command=self.destroy, height=2, width=10).pack(side="right")

        # Hint
        hint = ("Output format: 768 bytes (256×RGB), values 0..255 — directly works with your parse_palette(len==768).")
        tk.Label(self, text=hint, fg="#444").pack(side="bottom", pady=6)

    def browse_input(self):
        fn = filedialog.askopenfilename(
            title="Select screenshot",
            filetypes=[("Images", "*.png *.jpg *.jpeg *.bmp *.gif *.tga *.webp"), ("All files", "*.*")]
        )
        if not fn:
            return
        self.in_path.set(fn)

        # Default output next to input
        p = Path(fn)
        self.out_path.set(str(p.with_suffix(".pal")))

    def browse_output(self):
        fn = filedialog.asksaveasfilename(
            title="Save palette as",
            defaultextension=".pal",
            filetypes=[("PAL (raw 768 bytes)", "*.pal"), ("All files", "*.*")]
        )
        if not fn:
            return
        self.out_path.set(fn)

    def generate(self):
        in_fn = self.in_path.get().strip()
        out_fn = self.out_path.get().strip()

        if not in_fn:
            messagebox.showerror("Missing input", "Pick an input screenshot first.")
            return
        if not out_fn:
            messagebox.showerror("Missing output", "Pick an output .pal path first.")
            return

        try:
            colors = int(self.colors.get())
            sample_max = int(self.sample_max.get())
            if colors < 2 or colors > 256:
                raise ValueError("Colors must be 2..256")
            if sample_max < 64:
                raise ValueError("Sample max is too small")

            bg = (0, 0, 0) if self.bg_choice.get() == "black" else (255, 255, 255)

            img = Image.open(in_fn)
            pal = build_palette_from_image(img, colors=colors, sample_max=sample_max, composite_bg=bg)

            if self.index0_black.get():
                pal[0:3] = [0, 0, 0]

            save_pal(Path(out_fn), pal)

            messagebox.showinfo("Done", f"Palette generated:\n{out_fn}\n(768 bytes)")
        except Exception as e:
            messagebox.showerror("Error", str(e))


if __name__ == "__main__":
    App().mainloop()
