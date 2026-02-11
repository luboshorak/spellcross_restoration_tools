import json
import os
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from dataclasses import dataclass, asdict
from PIL import Image, ImageTk

ACTIONS = [
    "NewGame",
    "Continue",
    "LoadGame",
    "Credits",
    "Intro",
    "Exit",
]

@dataclass
class Zone:
    x: int
    y: int
    w: int
    h: int
    action: str

    def norm(self):
        # ensure w/h positive
        x, y, w, h = self.x, self.y, self.w, self.h
        if w < 0:
            x += w
            w = -w
        if h < 0:
            y += h
            h = -h
        return Zone(x, y, w, h, self.action)

    def contains(self, px, py):
        z = self.norm()
        return z.x <= px <= z.x + z.w and z.y <= py <= z.y + z.h


class ZoneEditor(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Spellcross Main Menu Zone Editor")
        self.geometry("1200x800")

        self.img = None
        self.img_tk = None
        self.img_path = None

        self.zones: list[Zone] = []
        self.selected = -1

        self.dragging = False
        self.drag_start = (0, 0)
        self.drag_rect_id = None

        self._build_ui()

    def _build_ui(self):
        top = ttk.Frame(self)
        top.pack(side=tk.TOP, fill=tk.X, padx=8, pady=6)

        ttk.Button(top, text="Open background PNG", command=self.open_image).pack(side=tk.LEFT)
        ttk.Button(top, text="Load JSON", command=self.load_json).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Button(top, text="Save JSON", command=self.save_json).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Button(top, text="Delete zone", command=self.delete_zone).pack(side=tk.LEFT, padx=(18, 0))

        ttk.Label(top, text="Action:").pack(side=tk.LEFT, padx=(18, 4))
        self.action_var = tk.StringVar(value=ACTIONS[0])
        self.action_combo = ttk.Combobox(top, textvariable=self.action_var, values=ACTIONS, width=16, state="readonly")
        self.action_combo.pack(side=tk.LEFT)
        self.action_combo.bind("<<ComboboxSelected>>", lambda e: self.set_action_for_selected())

        ttk.Label(top, text="Hint: drag = new zone, click = select, arrows = move, Shift+arrows = resize, Del = delete").pack(
            side=tk.LEFT, padx=(18, 0)
        )

        main = ttk.Frame(self)
        main.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        # left canvas
        self.canvas = tk.Canvas(main, bg="#202020")
        self.canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        # right list
        right = ttk.Frame(main, width=320)
        right.pack(side=tk.RIGHT, fill=tk.Y, padx=8, pady=8)

        ttk.Label(right, text="Zones").pack(anchor="w")
        self.listbox = tk.Listbox(right, height=30)
        self.listbox.pack(fill=tk.BOTH, expand=True)
        self.listbox.bind("<<ListboxSelect>>", self.on_list_select)

        ttk.Button(right, text="Clear all", command=self.clear_all).pack(fill=tk.X, pady=(8, 0))

        # bindings
        self.canvas.bind("<Button-1>", self.on_mouse_down)
        self.canvas.bind("<B1-Motion>", self.on_mouse_drag)
        self.canvas.bind("<ButtonRelease-1>", self.on_mouse_up)

        self.bind("<Delete>", lambda e: self.delete_zone())
        self.bind("<BackSpace>", lambda e: self.delete_zone())
        self.bind("<KeyPress>", self.on_key)

    def open_image(self):
        p = filedialog.askopenfilename(
            title="Open background PNG",
            filetypes=[("PNG", "*.png"), ("All files", "*.*")]
        )
        if not p:
            return
        self.load_image(p)

    def load_image(self, path):
        self.img_path = path
        self.img = Image.open(path).convert("RGBA")
        self.img_tk = ImageTk.PhotoImage(self.img)
        self.canvas.delete("all")
        self.canvas.config(scrollregion=(0, 0, self.img.width, self.img.height))
        self.canvas.create_image(0, 0, image=self.img_tk, anchor="nw", tags=("bg",))
        self.redraw_zones()

    def redraw_zones(self):
        self.canvas.delete("zone")
        for i, z in enumerate(self.zones):
            zz = z.norm()
            outline = "#ffcc00" if i == self.selected else "#00ff66"
            width = 3 if i == self.selected else 2
            rid = self.canvas.create_rectangle(
                zz.x, zz.y, zz.x + zz.w, zz.y + zz.h,
                outline=outline, width=width, tags=("zone", f"zone_{i}")
            )
            self.canvas.create_text(
                zz.x + 6, zz.y + 6, anchor="nw",
                text=f"{i}: {zz.action}", fill=outline,
                font=("Consolas", 11, "bold"), tags=("zone",)
            )

        self.refresh_listbox()

    def refresh_listbox(self):
        self.listbox.delete(0, tk.END)
        for i, z in enumerate(self.zones):
            zz = z.norm()
            self.listbox.insert(tk.END, f"{i}: {zz.action}  [{zz.x},{zz.y},{zz.w},{zz.h}]")
        if 0 <= self.selected < len(self.zones):
            self.listbox.selection_clear(0, tk.END)
            self.listbox.selection_set(self.selected)
            self.listbox.see(self.selected)

    def on_list_select(self, _e=None):
        sel = self.listbox.curselection()
        if not sel:
            return
        self.selected = int(sel[0])
        self.action_var.set(self.zones[self.selected].action)
        self.redraw_zones()

    def set_action_for_selected(self):
        if 0 <= self.selected < len(self.zones):
            self.zones[self.selected].action = self.action_var.get()
            self.redraw_zones()

    def find_zone_at(self, x, y):
        for i in range(len(self.zones)-1, -1, -1):
            if self.zones[i].contains(x, y):
                return i
        return -1

    def on_mouse_down(self, e):
        if self.img is None:
            messagebox.showinfo("No image", "Open a background PNG first.")
            return
        x, y = e.x, e.y
        hit = self.find_zone_at(x, y)
        if hit >= 0:
            self.selected = hit
            self.action_var.set(self.zones[hit].action)
            self.redraw_zones()
            return

        # start creating a new zone
        self.dragging = True
        self.drag_start = (x, y)
        self.drag_rect_id = self.canvas.create_rectangle(
            x, y, x+1, y+1, outline="#ffffff", width=2, dash=(4, 2), tags=("zone",)
        )

    def on_mouse_drag(self, e):
        if not self.dragging or self.drag_rect_id is None:
            return
        x0, y0 = self.drag_start
        x1, y1 = e.x, e.y
        self.canvas.coords(self.drag_rect_id, x0, y0, x1, y1)

    def on_mouse_up(self, e):
        if not self.dragging:
            return
        self.dragging = False

        x0, y0 = self.drag_start
        x1, y1 = e.x, e.y
        if self.drag_rect_id is not None:
            self.canvas.delete(self.drag_rect_id)
            self.drag_rect_id = None

        w = x1 - x0
        h = y1 - y0
        if abs(w) < 6 or abs(h) < 6:
            return

        z = Zone(x0, y0, w, h, self.action_var.get()).norm()
        self.zones.append(z)
        self.selected = len(self.zones) - 1
        self.redraw_zones()

    def delete_zone(self):
        if 0 <= self.selected < len(self.zones):
            del self.zones[self.selected]
            self.selected = min(self.selected, len(self.zones) - 1)
            self.redraw_zones()

    def clear_all(self):
        if messagebox.askyesno("Clear", "Delete all zones?"):
            self.zones.clear()
            self.selected = -1
            self.redraw_zones()

    def on_key(self, e):
        if not (0 <= self.selected < len(self.zones)):
            return
        z = self.zones[self.selected].norm()

        step = 1
        if e.state & 0x0001:  # Shift
            step = 1

        dx = dy = 0
        if e.keysym == "Left": dx = -step
        elif e.keysym == "Right": dx = step
        elif e.keysym == "Up": dy = -step
        elif e.keysym == "Down": dy = step
        else:
            return

        # Shift+arrows = resize, arrows = move
        if e.state & 0x0001:
            z.w = max(1, z.w + dx)
            z.h = max(1, z.h + dy)
        else:
            z.x += dx
            z.y += dy

        self.zones[self.selected] = z
        self.redraw_zones()

    def save_json(self):
        p = filedialog.asksaveasfilename(
            title="Save zones JSON",
            defaultextension=".json",
            filetypes=[("JSON", "*.json")]
        )
        if not p:
            return
        data = {
            "background": os.path.basename(self.img_path) if self.img_path else "",
            "zones": [asdict(z.norm()) for z in self.zones]
        }
        Path(p).write_text(json.dumps(data, indent=2), encoding="utf-8")
        messagebox.showinfo("Saved", f"Saved {len(self.zones)} zones.")

    def load_json(self):
        p = filedialog.askopenfilename(
            title="Load zones JSON",
            filetypes=[("JSON", "*.json"), ("All files", "*.*")]
        )
        if not p:
            return
        data = json.loads(Path(p).read_text(encoding="utf-8"))
        self.zones = [Zone(**z).norm() for z in data.get("zones", [])]
        self.selected = 0 if self.zones else -1
        if 0 <= self.selected < len(self.zones):
            self.action_var.set(self.zones[self.selected].action)
        self.redraw_zones()


if __name__ == "__main__":
    app = ZoneEditor()

    # optional auto-load common path if exists
    for guess in ["data/mainmenu.png", "data/MAINMENU.png", "mainmenu.png"]:
        if os.path.exists(guess):
            app.load_image(guess)
            break

    app.mainloop()
