#!/usr/bin/env python3
from __future__ import annotations

import base64
import json
import os
import sys
import tkinter as tk
from dataclasses import dataclass
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from PIL import Image, ImageOps, ImageTk

CANVAS_W = 640
CANVAS_H = 480

# ----------------------------
# Data model
# ----------------------------
@dataclass
class Layer:
    name: str
    file: str
    x: int = 0
    y: int = 0
    visible: bool = True
    opacity: float = 1.0
    # optional: if your extracted images have no alpha you can use color_key later
    color_key: list[int] | None = None


def default_project() -> dict:
    return {
        "canvas": {"w": CANVAS_W, "h": CANVAS_H, "color": [0, 0, 0, 0]},
        "background": {"file": "", "x": 0, "y": 0, "visible": True, "opacity": 1.0},
        "layers": []
    }


def as_layer(d: dict) -> Layer:
    return Layer(
        name=str(d.get("name", Path(d.get("file", "layer")).stem)),
        file=str(d.get("file", "")),
        x=int(d.get("x", 0)),
        y=int(d.get("y", 0)),
        visible=bool(d.get("visible", True)),
        opacity=float(d.get("opacity", 1.0)),
        color_key=d.get("color_key", None),
    )


def layer_to_dict(l: Layer) -> dict:
    d = {
        "name": l.name,
        "file": l.file,
        "x": int(l.x),
        "y": int(l.y),
        "visible": bool(l.visible),
        "opacity": float(l.opacity),
    }
    if l.color_key is not None:
        d["color_key"] = l.color_key
    return d


def load_image_rgba(path: Path, opacity: float = 1.0) -> Image.Image:
    img = Image.open(path)
    img = img.convert("RGBA")

    if opacity < 1.0:
        opacity = max(0.0, min(1.0, opacity))
        a = img.getchannel("A")
        a = a.point(lambda v: int(round(v * opacity)))
        img.putalpha(a)
    return img


def compose_png(project: dict, base_dir: Path) -> Image.Image:
    canvas = Image.new("RGBA", (CANVAS_W, CANVAS_H), (0, 0, 0, 0))

    def draw_one(ld: Layer):
        if not ld.visible or not ld.file:
            return
        p = (base_dir / ld.file).resolve()
        if not p.exists():
            return
        img = load_image_rgba(p, opacity=ld.opacity)
        canvas.alpha_composite(img, dest=(ld.x, ld.y))

    bg = as_layer(project.get("background", {}))
    draw_one(bg)
    for d in project.get("layers", []):
        draw_one(as_layer(d))

    return canvas

def export_svg(project: dict, base_dir: Path, out_svg: Path, embed: bool):
    def href_for(file_rel: str) -> str:
        p = (base_dir / file_rel).resolve()
        if not embed:
            return file_rel.replace("\\", "/")
        raw = p.read_bytes()
        b64 = base64.b64encode(raw).decode("ascii")
        ext = p.suffix.lower().lstrip(".")
        mime = "image/png" if ext == "png" else "application/octet-stream"
        return f"data:{mime};base64,{b64}"

    def svg_image_tag(file_rel: str, x: int, y: int, opacity: float, gid: str) -> str:
        if not file_rel:
            return ""
        p = (base_dir / file_rel).resolve()
        if not p.exists():
            return f'  <!-- missing: {file_rel} -->'
        # width/height optional but nice
        try:
            with Image.open(p) as im:
                w, h = im.size
        except Exception:
            w, h = None, None

        ow = f' width="{w}"' if w else ""
        oh = f' height="{h}"' if h else ""
        op = f' opacity="{max(0.0, min(1.0, opacity)):.4f}"' if opacity < 1.0 else ""
        href = href_for(file_rel)

        return (
            f'  <g id="{gid}">\n'
            f'    <image href="{href}" x="{x}" y="{y}"{ow}{oh}{op} />\n'
            f'  </g>'
        )

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{CANVAS_W}" height="{CANVAS_H}" viewBox="0 0 {CANVAS_W} {CANVAS_H}">'
    ]

    bg = as_layer(project.get("background", {}))
    if bg.file and bg.visible:
        parts.append(svg_image_tag(bg.file, bg.x, bg.y, bg.opacity, "background"))

    for i, d in enumerate(project.get("layers", [])):
        ld = as_layer(d)
        if not ld.visible or not ld.file:
            continue
        gid = ld.name if ld.name else f"layer_{i:02d}"
        parts.append(svg_image_tag(ld.file, ld.x, ld.y, ld.opacity, gid))

    parts.append("</svg>")

    out_svg.parent.mkdir(parents=True, exist_ok=True)
    out_svg.write_text("\n".join(parts), encoding="utf-8")


# ----------------------------
# GUI app
# ----------------------------
class App:
    def __init__(self, root: tk.Tk, project_path: Path | None = None):

        self.zoom = tk.DoubleVar(value=2.0)  # default 2×
        self._dragging = False
        self._internal_update = False

        # caches
        self._pil_cache: dict[str, Image.Image] = {}                 # original RGBA (unscaled)
        self._tk_cache: dict[tuple[str, float, float], ImageTk.PhotoImage] = {}  # (rel, opacity, zoom)-> PhotoImage

        self.root = root
        self.root.title("Spell UI Composer (640×480)")

        self.project_path: Path | None = None
        self.base_dir: Path = Path.cwd()

        self.project = default_project()

        self.tk_images: list[ImageTk.PhotoImage] = []
        self.item_ids: list[int] = []
        self.drag_state = None  # (is_bg: bool, index: int|None, last_x, last_y)

        self._build_ui()
        self._bind_keys()

        if project_path:
            self.open_project(project_path)
        else:
            self.new_project()

    def _resolve_asset_path(self, file_str: str) -> Path | None:
        if not file_str:
            return None

        s = str(file_str).strip().replace("\\", "/")
        if not s:
            return None

        s = os.path.expandvars(os.path.expanduser(s))

        # 1) absolute path
        if os.path.isabs(s):
            p = Path(s)
            if p.exists():
                return p

        # 2) relative to project folder
        p = (self.base_dir / s).resolve()
        if p.exists():
            return p

        # 3) fallback: relative to cwd
        p = (Path.cwd() / s).resolve()
        if p.exists():
            return p

        return None

    def _rebase_file(self, file_str: str, old_base: Path, new_base: Path) -> str:
        if not file_str:
            return file_str

        p = Path(file_str)

        # pokud je to absolutní cesta, nech ji být (nebo ji taky můžeš relativizovat)
        if p.is_absolute():
            return file_str.replace("\\", "/")

        abs_path = (old_base / p).resolve()
        try:
            rel = os.path.relpath(str(abs_path), start=str(new_base))
        except ValueError:
            # fallback (např. jiné disky ve Windows)
            return str(abs_path).replace("\\", "/")

        return rel.replace("\\", "/")

    def _rebase_project_paths(self, old_base: Path, new_base: Path):
        bg = self.project.get("background", {})
        if "file" in bg:
            bg["file"] = self._rebase_file(bg.get("file", ""), old_base, new_base)

        for d in self.project.get("layers", []):
            if "file" in d:
                d["file"] = self._rebase_file(d.get("file", ""), old_base, new_base)

    def apply_props(self):
        if self._internal_update or self._dragging:
            return
        i = self.selected_index()
        if i is None:
            return
        d = self.project["layers"][i]
        d["visible"] = bool(self.var_visible.get())
        d["name"] = str(self.var_name.get())
        d["x"] = int(self.var_x.get())
        d["y"] = int(self.var_y.get())

        # listbox refresh jednou a redraw jen pozic (ne full)
        self.refresh_list(select_index=i)
        self.redraw(full=False)

    # UI layout
    def _build_ui(self):
        # menu
        menubar = tk.Menu(self.root)
        m_file = tk.Menu(menubar, tearoff=0)
        m_file.add_command(label="New", command=self.new_project)
        m_file.add_command(label="Open…", command=self.open_project_dialog)
        m_file.add_command(label="Save", command=self.save_project)
        m_file.add_command(label="Save As…", command=self.save_project_as)
        m_file.add_separator()
        m_file.add_command(label="Export PNG…", command=self.export_png)
        m_file.add_command(label="Export SVG…", command=self.export_svg_dialog)
        m_file.add_separator()
        m_file.add_command(label="Exit", command=self.root.quit)
        menubar.add_cascade(label="File", menu=m_file)
        self.root.config(menu=menubar)

        # main frame
        main = ttk.Frame(self.root, padding=8)
        main.grid(row=0, column=0, sticky="nsew")
        self.root.rowconfigure(0, weight=1)
        self.root.columnconfigure(0, weight=1)

        # canvas
        self.canvas = tk.Canvas(main, width=CANVAS_W, height=CANVAS_H, bg="#2b2b2b", highlightthickness=0)
        self.canvas.grid(row=0, column=0, rowspan=20, padx=(0, 10), sticky="n")

        # right panel

        right = ttk.Frame(main)
        right.grid(row=0, column=1, sticky="nsew")
        main.columnconfigure(1, weight=1)

        # background controls
        bg_box = ttk.LabelFrame(right, text="Background", padding=8)
        bg_box.grid(row=0, column=0, sticky="ew")
        bg_box.columnconfigure(1, weight=1)

        ttk.Button(bg_box, text="Choose…", command=self.choose_background).grid(row=0, column=0, sticky="w")
        self.bg_label = ttk.Label(bg_box, text="(none)")
        self.bg_label.grid(row=0, column=1, sticky="ew", padx=(8, 0))

        # layers list
        layers_box = ttk.LabelFrame(right, text="Layers", padding=8)
        layers_box.grid(row=1, column=0, sticky="nsew", pady=(8, 0))
        right.rowconfigure(1, weight=1)
        layers_box.columnconfigure(0, weight=1)

        self.listbox = tk.Listbox(layers_box, height=14)
        self.listbox.grid(row=0, column=0, columnspan=3, sticky="nsew")
        self.listbox.bind("<<ListboxSelect>>", lambda e: self.on_select())

        btn_row = ttk.Frame(layers_box)
        btn_row.grid(row=1, column=0, columnspan=3, sticky="ew", pady=(8, 0))
        ttk.Button(btn_row, text="Add…", command=self.add_layers).grid(row=0, column=0, padx=(0, 6))
        ttk.Button(btn_row, text="Delete", command=self.delete_layer).grid(row=0, column=1, padx=(0, 6))
        ttk.Button(btn_row, text="Up", command=lambda: self.move_layer(-1)).grid(row=0, column=2, padx=(0, 6))
        ttk.Button(btn_row, text="Down", command=lambda: self.move_layer(+1)).grid(row=0, column=3)

        # properties
        prop = ttk.LabelFrame(right, text="Selected layer", padding=8)
        prop.grid(row=2, column=0, sticky="ew", pady=(8, 0))
        for c in range(4):
            prop.columnconfigure(c, weight=1)

        self.var_visible = tk.BooleanVar(value=True)
        self.chk_visible = ttk.Checkbutton(prop, text="Visible", variable=self.var_visible, command=self.apply_props)
        self.chk_visible.grid(row=0, column=0, sticky="w")

        ttk.Label(prop, text="Name").grid(row=1, column=0, sticky="w")
        self.var_name = tk.StringVar(value="")
        self.ent_name = ttk.Entry(prop, textvariable=self.var_name)
        self.ent_name.grid(row=1, column=1, columnspan=3, sticky="ew")

        ttk.Label(prop, text="X").grid(row=2, column=0, sticky="w")
        self.var_x = tk.IntVar(value=0)
        self.spn_x = ttk.Spinbox(prop, from_=-2000, to=2000, textvariable=self.var_x, width=8, command=self.apply_props)
        self.spn_x.grid(row=2, column=1, sticky="w")

        ttk.Label(prop, text="Y").grid(row=2, column=2, sticky="w")
        self.var_y = tk.IntVar(value=0)
        self.spn_y = ttk.Spinbox(prop, from_=-2000, to=2000, textvariable=self.var_y, width=8, command=self.apply_props)
        self.spn_y.grid(row=2, column=3, sticky="w")

        # apply when typing in entry/spinbox
        self.ent_name.bind("<Return>", lambda e: self.apply_props())
        self.ent_name.bind("<FocusOut>", lambda e: self.apply_props())

        self.spn_x.bind("<Return>", lambda e: self.apply_props())
        self.spn_x.bind("<FocusOut>", lambda e: self.apply_props())

        self.spn_y.bind("<Return>", lambda e: self.apply_props())
        self.spn_y.bind("<FocusOut>", lambda e: self.apply_props())

        zoom_box = ttk.LabelFrame(right, text="Zoom", padding=8)
        zoom_box.grid(row=3, column=0, sticky="ew", pady=(8, 0))

        self.zoom_combo = ttk.Combobox(
            zoom_box,
            values=["1.0", "1.5", "2.0", "3.0", "4.0", "5.0", "6.0"],
            width=6,
            state="readonly"
        )
        self.zoom_combo.set("2.0")
        self.zoom_combo.grid(row=0, column=0, sticky="w")
        self.zoom_combo.bind("<<ComboboxSelected>>", lambda e: self.on_zoom_change())

        # status
        self.status = tk.StringVar(value="Tip: drag layers. Arrow keys = 1px, Shift+Arrows = 10px.")
        ttk.Label(right, textvariable=self.status, wraplength=360).grid(row=4, column=0, sticky="ew", pady=(8, 0))

        # canvas bindings
        self.canvas.bind("<Button-1>", self.on_mouse_down)
        self.canvas.bind("<B1-Motion>", self.on_mouse_drag)
        self.canvas.bind("<ButtonRelease-1>", self.on_mouse_up)

    def _bind_keys(self):
        self.root.bind("<Up>", lambda e: self.nudge(0, -10 if (e.state & 0x0001) else -1))
        self.root.bind("<Down>", lambda e: self.nudge(0, +10 if (e.state & 0x0001) else +1))
        self.root.bind("<Left>", lambda e: self.nudge(-10 if (e.state & 0x0001) else -1, 0))
        self.root.bind("<Right>", lambda e: self.nudge(+10 if (e.state & 0x0001) else +1, 0))

    # Project ops
    def new_project(self):
        self.project = default_project()
        self.project_path = None
        self.base_dir = Path.cwd()
        self.refresh_list()
        self.redraw()
        self.status.set("New project.")
        self._pil_cache.clear()
        self._tk_cache.clear()

    def open_project_dialog(self):
        f = filedialog.askopenfilename(title="Open project JSON", filetypes=[("JSON", "*.json"), ("All", "*.*")])
        if f:
            self.open_project(Path(f))

    def open_project(self, path: Path):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            self.project = data
            self.project_path = path
            self.base_dir = path.parent
            self.refresh_list()
            self.redraw()
            self.status.set(f"Opened: {path}")
            self._pil_cache.clear()
            self._tk_cache.clear()

        except Exception as e:
            messagebox.showerror("Open failed", str(e))

    def save_project(self):
        if not self.project_path:
            return self.save_project_as()
        try:
            self.project_path.write_text(json.dumps(self.project, indent=2, ensure_ascii=False), encoding="utf-8")
            self.status.set(f"Saved: {self.project_path}")
        except Exception as e:
            messagebox.showerror("Save failed", str(e))

    def save_project_as(self):
        f = filedialog.asksaveasfilename(
            title="Save project as",
            defaultextension=".json",
            filetypes=[("JSON", "*.json")]
        )
        if not f:
            return

        old_base = self.base_dir
        self.project_path = Path(f)
        new_base = self.project_path.parent

        # přebázuj cesty na novou složku projektu
        self._rebase_project_paths(old_base, new_base)

        # teprve teď přepni base_dir a ulož
        self.base_dir = new_base
        self._pil_cache.clear()
        self._tk_cache.clear()
        self.save_project()
        self.redraw(full=True)

    # Background / layers
    def choose_background(self):
        f = filedialog.askopenfilename(
            title="Choose background image",
            filetypes=[("Images", "*.png;*.bmp;*.jpg;*.jpeg;*.gif"), ("All", "*.*")]
        )
        if not f:
            return
        rel = os.path.relpath(f, start=str(self.base_dir)).replace("\\", "/")
        self.project["background"]["file"] = rel
        self.project["background"]["x"] = 0
        self.project["background"]["y"] = 0
        self.bg_label.config(text=rel)
        self.redraw()

    def add_layers(self):
        files = filedialog.askopenfilenames(
            title="Add layer images",
            filetypes=[("Images", "*.png;*.bmp;*.jpg;*.jpeg;*.gif"), ("All", "*.*")]
        )
        if not files:
            return
        for f in files:
            rel = os.path.relpath(f, start=str(self.base_dir)).replace("\\", "/")
            self.project["layers"].append({
                "name": Path(f).stem,
                "file": rel,
                "x": 0,
                "y": 0,
                "visible": True,
                "opacity": 1.0
            })
        self.refresh_list(select_last=True)
        self.redraw()

    def delete_layer(self):
        i = self.selected_index()
        if i is None:
            return
        self.project["layers"].pop(i)
        self.refresh_list()
        self.redraw()

    def move_layer(self, delta: int):
        i = self.selected_index()
        if i is None:
            return
        j = i + delta
        if j < 0 or j >= len(self.project["layers"]):
            return
        layers = self.project["layers"]
        layers[i], layers[j] = layers[j], layers[i]
        self.refresh_list(select_index=j)
        self.redraw()

    # Selection + properties
    def selected_index(self) -> int | None:
        sel = self.listbox.curselection()
        return int(sel[0]) if sel else None

    def on_zoom_change(self):
        try:
            z = float(self.zoom_combo.get())
        except:
            z = 1.0
        self.zoom.set(max(1.0, min(6.0, z)))

        # resize canvas (view)
        vw = int(CANVAS_W * self.zoom.get())
        vh = int(CANVAS_H * self.zoom.get())
        self.canvas.config(width=vw, height=vh)

        # IMPORTANT: clear tk cache for this zoom (or keep multi-zoom cache, but this is simple)
        # (We keep per zoom anyway, so no need to clear; only if memory grows too much.)
        self.redraw(full=True)

    def on_select(self):
        i = self.selected_index()
        if i is None:
            return
        ld = as_layer(self.project["layers"][i])
        self.var_visible.set(ld.visible)
        self.var_name.set(ld.name)
        self.var_x.set(ld.x)
        self.var_y.set(ld.y)

    def apply_props(self):
        i = self.selected_index()
        if i is None:
            return
        d = self.project["layers"][i]
        d["visible"] = bool(self.var_visible.get())
        d["name"] = str(self.var_name.get())
        d["x"] = int(self.var_x.get())
        d["y"] = int(self.var_y.get())
        self.refresh_list(select_index=i)
        self.redraw()

    # Rendering / redraw
    def refresh_list(self, select_last: bool = False, select_index: int | None = None):
        self.listbox.delete(0, tk.END)
        layers = self.project.get("layers", [])
        for idx, d in enumerate(layers):
            ld = as_layer(d)
            vis = "👁" if ld.visible else "✖"
            self.listbox.insert(tk.END, f"{idx:02d} {vis}  {ld.name}  @({ld.x},{ld.y})  {ld.file}")
        if layers:
            if select_last:
                select_index = len(layers) - 1
            if select_index is None:
                select_index = min(len(layers) - 1, 0)
            self.listbox.selection_clear(0, tk.END)
            self.listbox.selection_set(select_index)
            self.listbox.activate(select_index)
            self.on_select()

        bg_file = self.project.get("background", {}).get("file", "")
        self.bg_label.config(text=bg_file if bg_file else "(none)")

    def redraw(self, full: bool = True):
        z = float(self.zoom.get())

        if full:
            self.canvas.delete("all")
            self.tk_images.clear()
            self.item_ids.clear()

            # background
            bg = as_layer(self.project.get("background", {}))
            if bg.file and bg.visible:
                tk_im = self._get_tk(bg.file, bg.opacity, z)
                if tk_im:
                    self.tk_images.append(tk_im)
                    self.canvas.create_image(int(bg.x*z), int(bg.y*z), anchor="nw", image=tk_im)

            # layers
            for idx, d in enumerate(self.project.get("layers", [])):
                ld = as_layer(d)
                if not ld.visible or not ld.file:
                    self.item_ids.append(-1)
                    continue
                tk_im = self._get_tk(ld.file, ld.opacity, z)
                if not tk_im:
                    self.item_ids.append(-1)
                    continue
                self.tk_images.append(tk_im)
                item = self.canvas.create_image(int(ld.x*z), int(ld.y*z), anchor="nw", image=tk_im, tags=(f"layer{idx}",))
                self.item_ids.append(item)
        else:
            # only update positions (fast path)
            for idx, d in enumerate(self.project.get("layers", [])):
                item = self.item_ids[idx] if idx < len(self.item_ids) else -1
                if item == -1:
                    continue
                ld = as_layer(d)
                self.canvas.coords(item, int(ld.x*z), int(ld.y*z))

    def _get_pil(self, rel: str) -> Image.Image | None:
        if not rel:
            return None

        key = rel.strip().replace("\\", "/")
        # cache key musí zohlednit i base_dir, jinak se po Open/SafeAs můžou použít staré obrázky
        cache_key = f"{self.base_dir.resolve()}||{key}"

        if cache_key in self._pil_cache:
            return self._pil_cache[cache_key]

        p = self._resolve_asset_path(key)
        if p is None:
            return None

        im = Image.open(p).convert("RGBA")
        self._pil_cache[cache_key] = im
        return im

    def _get_tk(self, rel: str, opacity: float, zoom: float) -> ImageTk.PhotoImage | None:
        if not rel:
            return None
        rel = rel.replace("\\", "/").strip()

        k = (str(self.base_dir.resolve()), rel, float(opacity), float(zoom))
        if k in self._tk_cache:
            return self._tk_cache[k]

        pil = self._get_pil(rel)
        if pil is None:
            return None

        im = pil
        if opacity < 1.0:
            im = pil.copy()
            a = im.getchannel("A")
            op = max(0.0, min(1.0, float(opacity)))
            a = a.point(lambda v: int(round(v * op)))
            im.putalpha(a)

        if zoom != 1.0:
            im = im.resize((int(im.size[0] * zoom), int(im.size[1] * zoom)), Image.NEAREST)

        tk_im = ImageTk.PhotoImage(im)
        self._tk_cache[k] = tk_im
        return tk_im

    # Mouse drag
    def hit_test_layer(self, x: int, y: int) -> int | None:
        items = self.canvas.find_overlapping(x, y, x, y)
        if not items:
            return None
        top = items[-1]
        # map item id to layer index
        for idx, item_id in enumerate(self.item_ids):
            if item_id == top:
                return idx
        return None

    def on_mouse_down(self, ev):
        idx = self.hit_test_layer(ev.x, ev.y)
        if idx is None:
            return

        self.listbox.selection_clear(0, tk.END)
        self.listbox.selection_set(idx)
        self.listbox.activate(idx)
        self.on_select()

        d = self.project["layers"][idx]
        z = float(self.zoom.get())

        # store absolute drag baseline
        self.drag_state = {
            "idx": idx,
            "mx0": ev.x,
            "my0": ev.y,
            "x0": float(d.get("x", 0)),
            "y0": float(d.get("y", 0)),
            "z": z,
        }
        self._dragging = True

    def on_mouse_drag(self, ev):
        if not self.drag_state:
            return

        st = self.drag_state
        idx = st["idx"]
        z = st["z"]

        dx_view = ev.x - st["mx0"]
        dy_view = ev.y - st["my0"]

        # absolute mapping view -> model (NO per-event rounding)
        new_x = st["x0"] + (dx_view / z)
        new_y = st["y0"] + (dy_view / z)

        d = self.project["layers"][idx]
        d["x"] = int(round(new_x))
        d["y"] = int(round(new_y))

        # fast position update only
        self.redraw(full=False)

        # update fields without triggering apply loops
        self._internal_update = True
        self.var_x.set(d["x"])
        self.var_y.set(d["y"])
        self._internal_update = False

    def on_mouse_up(self, ev):
        self.drag_state = None
        self._dragging = False
        i = self.selected_index()
        if i is not None:
            self.refresh_list(select_index=i)

    # Nudge keys
    def nudge(self, dx: int, dy: int):
        i = self.selected_index()
        if i is None:
            return
        d = self.project["layers"][i]
        d["x"] = int(d.get("x", 0)) + dx
        d["y"] = int(d.get("y", 0)) + dy
        self.var_x.set(int(d["x"]))
        self.var_y.set(int(d["y"]))
        self.refresh_list(select_index=i)
        self.redraw()

    # Exports
    def export_png(self):
        f = filedialog.asksaveasfilename(title="Export PNG", defaultextension=".png", filetypes=[("PNG", "*.png")])
        if not f:
            return
        out = Path(f)
        img = compose_png(self.project, self.base_dir)
        out.parent.mkdir(parents=True, exist_ok=True)
        img.save(out)
        self.status.set(f"Exported PNG: {out}")

    def export_svg_dialog(self):
        f = filedialog.asksaveasfilename(title="Export SVG", defaultextension=".svg", filetypes=[("SVG", "*.svg")])
        if not f:
            return
        embed = messagebox.askyesno("Embed images?", "Embed PNGs into SVG (base64)?\n\nYes = one self-contained SVG\nNo = SVG references external images")
        out = Path(f)
        try:
            export_svg(self.project, self.base_dir, out, embed=embed)
            self.status.set(f"Exported SVG: {out}")
        except Exception as e:
            messagebox.showerror("Export SVG failed", str(e))


def main():
    project_path = None
    if len(sys.argv) >= 2:
        project_path = Path(sys.argv[1]).resolve()

    root = tk.Tk()
    App(root, project_path=project_path)
    root.resizable(False, False)  # canvas is fixed 640x480 + right panel
    root.mainloop()


if __name__ == "__main__":
    main()
