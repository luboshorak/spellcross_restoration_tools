#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import shutil
import time
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

APP_TITLE = "Gitignore Generator PRO"
CHECK_OFF = "☐"
CHECK_ON  = "☑"

MODE_DIR = "DIR"          # ignore folder itself: path/
MODE_CONTENTS = "CONTENTS" # ignore only contents: path/* + exceptions

# ---------- Presets ----------
PRESETS = {
    "Common": [
        "# --- Common ---",
        ".DS_Store",
        "Thumbs.db",
        "Desktop.ini",
        "*.log",
        "*.tmp",
        "*.bak",
        "*.swp",
    ],
    "Editors/IDEs": [
        "# --- Editors/IDEs ---",
        ".vscode/",
        ".idea/",
        "*.suo",
        "*.user",
        "*.userosscache",
        "*.sln.docstates",
    ],
    "Python": [
        "# --- Python ---",
        "__pycache__/",
        "*.py[cod]",
        ".pytest_cache/",
        ".mypy_cache/",
        ".ruff_cache/",
        ".tox/",
        ".venv/",
        "venv/",
        "pip-wheel-metadata/",
        ".coverage",
        "coverage.xml",
    ],
    "Node": [
        "# --- Node ---",
        "node_modules/",
        "npm-debug.log*",
        "yarn-debug.log*",
        "yarn-error.log*",
        "pnpm-debug.log*",
        ".npm/",
        ".yarn/",
        ".pnp.*",
        "dist/",
        "build/",
    ],
    "C++/CMake": [
        "# --- C++/CMake ---",
        "build/",
        "cmake-build-*/",
        "CMakeFiles/",
        "CMakeCache.txt",
        "Makefile",
        "*.o",
        "*.obj",
        "*.a",
        "*.lib",
        "*.dll",
        "*.so",
        "*.dylib",
        "*.exe",
    ],
    "VisualStudio": [
        "# --- Visual Studio ---",
        ".vs/",
        "Debug/",
        "Release/",
        "x64/",
        "x86/",
        "bin/",
        "obj/",
        "*.user",
        "*.suo",
        "*.VC.db",
        "*.VC.VC.opendb",
    ],
    "Unity": [
        "# --- Unity ---",
        "[Ll]ibrary/",
        "[Tt]emp/",
        "[Oo]bj/",
        "[Bb]uild/",
        "[Bb]uilds/",
        "[Ll]ogs/",
        "UserSettings/",
        "MemoryCaptures/",
    ],
}

def norm_slashes(p: str) -> str:
    return p.replace("\\", "/")

def ensure_trailing_slash(p: str) -> str:
    p = norm_slashes(p)
    if p and not p.endswith("/"):
        p += "/"
    return p

def rel_depth(rel: str) -> int:
    rel = norm_slashes(rel)
    if rel in ("", "."):
        return 0
    return rel.count("/") + 1

def scan_dirs(project_root: str, max_depth: int, include_hidden: bool) -> list[str]:
    project_root = os.path.abspath(project_root)
    found = set()

    for root, dirs, _ in os.walk(project_root):
        rel_root = os.path.relpath(root, project_root)
        d = rel_depth(rel_root)

        # stop deeper walking
        if d >= max_depth:
            dirs[:] = []
            continue

        # filter hidden
        kept = []
        for name in dirs:
            if not include_hidden and name.startswith("."):
                continue
            kept.append(name)
        dirs[:] = kept

        for name in dirs:
            rel = os.path.relpath(os.path.join(root, name), project_root)
            found.add(norm_slashes(rel))

    return sorted(found, key=lambda s: (s.count("/"), s.lower()))

def detect_project_types(root: str) -> set[str]:
    """
    Heuristiky: dle přítomnosti typických souborů/složek.
    Vrací množinu labelů presetů, které dává smysl zapnout.
    """
    root = os.path.abspath(root)
    present = set()

    def exists(*parts):
        return os.path.exists(os.path.join(root, *parts))

    # Python
    if exists("pyproject.toml") or exists("requirements.txt") or exists("Pipfile") or exists("setup.py"):
        present.add("Python")

    # Node
    if exists("package.json") or exists("pnpm-lock.yaml") or exists("yarn.lock"):
        present.add("Node")

    # VS
    # (hledání *.sln v rootu)
    try:
        for fn in os.listdir(root):
            if fn.lower().endswith(".sln"):
                present.add("VisualStudio")
                break
    except Exception:
        pass

    # CMake
    if exists("CMakeLists.txt"):
        present.add("C++/CMake")

    # Unity
    if exists("Assets") and exists("ProjectSettings"):
        present.add("Unity")

    # Vždy dává smysl
    present.add("Common")
    present.add("Editors/IDEs")

    return present

class GitignorePro(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("1180x720")
        self.minsize(1050, 650)

        # state
        self.project_dir = tk.StringVar(value="")
        self.depth_var = tk.IntVar(value=2)
        self.include_hidden_var = tk.BooleanVar(value=False)
        self.filter_var = tk.StringVar(value="")
        self.default_mode_var = tk.StringVar(value=MODE_DIR)

        self.write_mode_var = tk.StringVar(value="OVERWRITE")  # OVERWRITE / APPEND

        self.all_dirs: list[str] = []
        self.visible_dirs: list[str] = []

        # per-dir states
        self.dir_checked: dict[str, bool] = {}
        self.dir_mode: dict[str, str] = {}

        # presets states
        self.preset_checked: dict[str, bool] = {k: False for k in PRESETS.keys()}

        # tree item mapping
        self.tree_iid_by_rel: dict[str, str] = {}

        self._build_ui()

    # ---------- UI ----------
    def _build_ui(self):
        top = ttk.Frame(self, padding=10)
        top.pack(fill="x")

        ttk.Label(top, text="Projekt:", width=8).pack(side="left")
        ttk.Entry(top, textvariable=self.project_dir).pack(side="left", fill="x", expand=True, padx=(0, 8))

        ttk.Button(top, text="Vybrat…", command=self.pick_project).pack(side="left")
        ttk.Button(top, text="Skenovat", command=self.scan_and_build).pack(side="left", padx=(8, 0))
        ttk.Button(top, text="Auto-presety", command=self.auto_presets).pack(side="left", padx=(8, 0))

        opts = ttk.Frame(self, padding=(10, 0, 10, 10))
        opts.pack(fill="x")

        ttk.Label(opts, text="Hloubka:").pack(side="left")
        ttk.Spinbox(opts, from_=1, to=12, textvariable=self.depth_var, width=5).pack(side="left", padx=(6, 14))
        ttk.Checkbutton(opts, text="Zahrnout hidden (.…)", variable=self.include_hidden_var).pack(side="left")

        ttk.Label(opts, text="  Filtr:").pack(side="left", padx=(14, 0))
        fe = ttk.Entry(opts, textvariable=self.filter_var, width=30)
        fe.pack(side="left", padx=(6, 0))
        fe.bind("<KeyRelease>", lambda _e: self.apply_filter())

        ttk.Label(opts, text="  Default režim ignorování:").pack(side="left", padx=(14, 0))
        ttk.Radiobutton(opts, text="celá složka", value=MODE_DIR, variable=self.default_mode_var).pack(side="left", padx=(6, 0))
        ttk.Radiobutton(opts, text="jen obsah", value=MODE_CONTENTS, variable=self.default_mode_var).pack(side="left", padx=(6, 0))

        main = ttk.PanedWindow(self, orient="horizontal")
        main.pack(fill="both", expand=True, padx=10, pady=10)

        # LEFT: Tree
        left = ttk.Frame(main, padding=10)
        main.add(left, weight=2)

        ttk.Label(left, text="Složky (klikem přepínáš checkbox; dvojklik přepíná režim DIR/CONTENTS):").pack(anchor="w")

        tree_frame = ttk.Frame(left)
        tree_frame.pack(fill="both", expand=True, pady=(6, 8))

        self.tree = ttk.Treeview(tree_frame, columns=("mode",), show="tree headings")
        self.tree.heading("#0", text="Folder")
        self.tree.heading("mode", text="Mode")
        self.tree.column("mode", width=110, anchor="center", stretch=False)

        ys = ttk.Scrollbar(tree_frame, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=ys.set)
        self.tree.pack(side="left", fill="both", expand=True)
        ys.pack(side="left", fill="y")

        self.tree.bind("<Button-1>", self.on_tree_click)
        self.tree.bind("<Double-1>", self.on_tree_double_click)

        left_btns = ttk.Frame(left)
        left_btns.pack(fill="x")

        ttk.Button(left_btns, text="Vybrat vše", command=self.select_all_dirs).pack(side="left")
        ttk.Button(left_btns, text="Odznačit vše", command=self.unselect_all_dirs).pack(side="left", padx=(8, 0))
        ttk.Button(left_btns, text="Rozbalit vše", command=lambda: self.expand_all(True)).pack(side="left", padx=(8, 0))
        ttk.Button(left_btns, text="Sbalit vše", command=lambda: self.expand_all(False)).pack(side="left", padx=(8, 0))

        # RIGHT: Presets + Custom + Preview/Save
        right = ttk.Frame(main, padding=10)
        main.add(right, weight=3)

        # presets
        presets_box = ttk.LabelFrame(right, text="Presety (klik přepíná)", padding=8)
        presets_box.pack(fill="x")

        self.preset_list = tk.Listbox(presets_box, height=7, activestyle="none", exportselection=False)
        self.preset_list.pack(fill="x")
        self.preset_list.bind("<Button-1>", self.on_preset_click)

        self.refresh_presets_list()

        # write mode
        wm = ttk.Frame(right, padding=(0, 8, 0, 0))
        wm.pack(fill="x")
        ttk.Label(wm, text="Zápis .gitignore:").pack(side="left")
        ttk.Radiobutton(wm, text="Overwrite (udělá .bak)", value="OVERWRITE", variable=self.write_mode_var).pack(side="left", padx=(8, 0))
        ttk.Radiobutton(wm, text="Append (přidat na konec)", value="APPEND", variable=self.write_mode_var).pack(side="left", padx=(8, 0))

        # custom text
        ttk.Label(right, text="Vlastní řádky (patterny/komentáře):").pack(anchor="w", pady=(10, 0))
        self.custom_text = tk.Text(right, height=8, wrap="none")
        self.custom_text.pack(fill="x", pady=(6, 10))
        self.custom_text.insert("1.0", "# --- Custom ---\n")

        # actions
        act = ttk.Frame(right)
        act.pack(fill="x", pady=(0, 8))
        ttk.Button(act, text="Generovat náhled", command=self.generate_preview).pack(side="left")
        ttk.Button(act, text="Kopírovat do schránky", command=self.copy_to_clipboard).pack(side="left", padx=(8, 0))
        ttk.Button(act, text="Uložit do projektu", command=self.save_gitignore).pack(side="left", padx=(8, 0))

        # preview
        ttk.Label(right, text="Náhled .gitignore:").pack(anchor="w")
        prev_frame = ttk.Frame(right)
        prev_frame.pack(fill="both", expand=True, pady=(6, 0))

        self.preview = tk.Text(prev_frame, wrap="none")
        pys = ttk.Scrollbar(prev_frame, orient="vertical", command=self.preview.yview)
        pxs = ttk.Scrollbar(prev_frame, orient="horizontal", command=self.preview.xview)
        self.preview.configure(yscrollcommand=pys.set, xscrollcommand=pxs.set)

        self.preview.pack(side="left", fill="both", expand=True)
        pys.pack(side="left", fill="y")
        pxs.pack(side="bottom", fill="x")

        self.preview.insert("1.0", "# Vyber projekt → Skenovat → (volitelně Auto-presety)\n")

    # ---------- Project / Scan ----------
    def pick_project(self):
        d = filedialog.askdirectory(title="Vyber složku projektu")
        if d:
            self.project_dir.set(d)

    def scan_and_build(self):
        root = self.project_dir.get().strip()
        if not root or not os.path.isdir(root):
            messagebox.showerror("Chyba", "Vyber platnou složku projektu.")
            return

        try:
            self.all_dirs = scan_dirs(root, int(self.depth_var.get()), bool(self.include_hidden_var.get()))
        except Exception as e:
            messagebox.showerror("Chyba", f"Skenování selhalo:\n{e}")
            return

        # init states for new dirs
        for rel in self.all_dirs:
            self.dir_checked.setdefault(rel, False)
            self.dir_mode.setdefault(rel, self.default_mode_var.get())

        # remove stale states (optional cleanup)
        stale = [k for k in self.dir_checked.keys() if k not in set(self.all_dirs)]
        for k in stale:
            self.dir_checked.pop(k, None)
            self.dir_mode.pop(k, None)

        self.apply_filter()
        self.generate_preview()

    def apply_filter(self):
        flt = self.filter_var.get().strip().lower()
        if not flt:
            self.visible_dirs = self.all_dirs[:]
        else:
            self.visible_dirs = [d for d in self.all_dirs if flt in d.lower()]

        self.rebuild_tree()

    # ---------- Tree handling ----------
    def rebuild_tree(self):
        self.tree.delete(*self.tree.get_children())
        self.tree_iid_by_rel.clear()

        # Build hierarchy nodes by inserting parents first
        # Root-level iids are plain rel path segments
        def parent_rel(rel: str) -> str | None:
            rel = norm_slashes(rel)
            if "/" not in rel:
                return None
            return rel.rsplit("/", 1)[0]

        # Ensure deterministic order: parents before children
        rels = sorted(self.visible_dirs, key=lambda s: (s.count("/"), s.lower()))

        for rel in rels:
            p = parent_rel(rel)
            if p and p not in self.visible_dirs:
                # if parent is filtered out, attach to root
                p_iid = ""
            else:
                p_iid = self.tree_iid_by_rel.get(p, "")

            iid = rel  # use rel as iid
            self.tree_iid_by_rel[rel] = iid

            checked = self.dir_checked.get(rel, False)
            mode = self.dir_mode.get(rel, self.default_mode_var.get())
            label = f"{CHECK_ON if checked else CHECK_OFF} {rel}"

            self.tree.insert(p_iid, "end", iid=iid, text=label, values=(mode,))

        # expand root one level for usability
        for iid in self.tree.get_children(""):
            self.tree.item(iid, open=True)

    def tree_rel_from_event(self, event):
        iid = self.tree.identify_row(event.y)
        if not iid:
            return None
        return iid  # iid is rel

    def on_tree_click(self, event):
        rel = self.tree_rel_from_event(event)
        if not rel:
            return

        # Toggle checkbox only if clicked on the tree column
        col = self.tree.identify_column(event.x)
        if col != "#0":
            return

        self.dir_checked[rel] = not self.dir_checked.get(rel, False)
        self.refresh_tree_row(rel)
        self.generate_preview()

    def on_tree_double_click(self, event):
        rel = self.tree_rel_from_event(event)
        if not rel:
            return

        # Toggle mode DIR <-> CONTENTS
        cur = self.dir_mode.get(rel, self.default_mode_var.get())
        self.dir_mode[rel] = MODE_CONTENTS if cur == MODE_DIR else MODE_DIR
        self.refresh_tree_row(rel)
        self.generate_preview()

    def refresh_tree_row(self, rel: str):
        if rel not in self.tree_iid_by_rel:
            return
        checked = self.dir_checked.get(rel, False)
        mode = self.dir_mode.get(rel, self.default_mode_var.get())
        self.tree.item(rel, text=f"{CHECK_ON if checked else CHECK_OFF} {rel}", values=(mode,))

    def expand_all(self, expand: bool):
        def rec(iid):
            self.tree.item(iid, open=expand)
            for c in self.tree.get_children(iid):
                rec(c)
        for iid in self.tree.get_children(""):
            rec(iid)

    def select_all_dirs(self):
        for rel in self.visible_dirs:
            self.dir_checked[rel] = True
        self.rebuild_tree()
        self.generate_preview()

    def unselect_all_dirs(self):
        for rel in self.visible_dirs:
            self.dir_checked[rel] = False
        self.rebuild_tree()
        self.generate_preview()

    # ---------- Presets ----------
    def refresh_presets_list(self):
        self.preset_list.delete(0, tk.END)
        for name in PRESETS.keys():
            self.preset_list.insert(tk.END, f"{CHECK_ON if self.preset_checked.get(name) else CHECK_OFF} {name}")

    def on_preset_click(self, event):
        idx = self.preset_list.nearest(event.y)
        if idx < 0:
            return
        name = list(PRESETS.keys())[idx]
        self.preset_checked[name] = not self.preset_checked.get(name, False)
        self.refresh_presets_list()
        self.generate_preview()

    def auto_presets(self):
        root = self.project_dir.get().strip()
        if not root or not os.path.isdir(root):
            messagebox.showerror("Chyba", "Vyber platnou složku projektu.")
            return

        types = detect_project_types(root)
        for k in PRESETS.keys():
            self.preset_checked[k] = (k in types)

        self.refresh_presets_list()

        # bonus: pokud existují typické složky, předvyber je
        common_dir_names = {"bin", "obj", "build", "dist", "out", "__pycache__", "node_modules", ".vs", ".idea", ".vscode", "Library", "Temp"}
        for rel in self.all_dirs:
            last = rel.split("/")[-1]
            if last in common_dir_names:
                self.dir_checked[rel] = True

        self.rebuild_tree()
        self.generate_preview()

    # ---------- Generation ----------
    def build_ignore_lines_for_selected_dirs(self) -> list[str]:
        lines = []
        selected = [rel for rel, on in self.dir_checked.items() if on]

        if not selected:
            return lines

        lines.append("# --- Project directories ---")
        for rel in sorted(set(selected), key=lambda s: (s.count("/"), s.lower())):
            mode = self.dir_mode.get(rel, self.default_mode_var.get())
            rel_norm = norm_slashes(rel)

            if mode == MODE_DIR:
                lines.append(ensure_trailing_slash(rel_norm))
            else:
                # ignore only contents, keep .gitkeep/.keep if user wants empty dirs tracked
                base = ensure_trailing_slash(rel_norm)
                lines.append(base + "*")
                lines.append("!" + base + ".gitkeep")
                lines.append("!" + base + ".keep")

        lines.append("")
        return lines

    def build_preset_lines(self) -> list[str]:
        out = []
        for name, enabled in self.preset_checked.items():
            if not enabled:
                continue
            out.extend(PRESETS[name])
            out.append("")
        return out

    def build_custom_lines(self) -> list[str]:
        txt = self.custom_text.get("1.0", tk.END).rstrip("\n")
        if not txt.strip():
            return []
        return [txt, ""]

    def build_gitignore_text(self) -> str:
        root = self.project_dir.get().strip()
        header = [
            "# Generated by Gitignore Generator PRO",
            f"# Date: {time.strftime('%Y-%m-%d %H:%M:%S')}",
        ]
        if root:
            header.append(f"# Project: {os.path.abspath(root)}")
        header.append("")

        body = []
        body += self.build_ignore_lines_for_selected_dirs()
        body += self.build_preset_lines()
        body += self.build_custom_lines()

        # tidy: remove duplicate empty lines (max 1 in a row)
        compact = []
        for line in header + body:
            if line == "" and compact and compact[-1] == "":
                continue
            compact.append(line)

        return "\n".join(compact).rstrip() + "\n"

    def generate_preview(self):
        text = self.build_gitignore_text()
        self.preview.delete("1.0", tk.END)
        self.preview.insert("1.0", text)

    # ---------- Save / Clipboard ----------
    def copy_to_clipboard(self):
        text = self.build_gitignore_text()
        self.clipboard_clear()
        self.clipboard_append(text)
        self.update()
        messagebox.showinfo("OK", "Zkopírováno do schránky.")

    def save_gitignore(self):
        root = self.project_dir.get().strip()
        if not root or not os.path.isdir(root):
            messagebox.showerror("Chyba", "Vyber platnou složku projektu.")
            return

        path = os.path.join(root, ".gitignore")
        text = self.build_gitignore_text()
        mode = self.write_mode_var.get()

        try:
            if mode == "OVERWRITE":
                if os.path.exists(path):
                    shutil.copy2(path, path + ".bak")
                with open(path, "w", encoding="utf-8", newline="\n") as f:
                    f.write(text)
            else:  # APPEND
                if os.path.exists(path):
                    with open(path, "r", encoding="utf-8", errors="replace") as f:
                        existing = f.read()
                    # když už existuje stejný blok, netlač to dvakrát (jednoduchý guard)
                    if text.strip() in existing:
                        messagebox.showinfo("Info", "Vypadá to, že stejný obsah už v .gitignore je. Nechávám beze změny.")
                        return
                with open(path, "a", encoding="utf-8", newline="\n") as f:
                    # oddělovač
                    if os.path.exists(path):
                        f.write("\n# ---- appended by Gitignore Generator PRO ----\n")
                    f.write(text)
        except Exception as e:
            messagebox.showerror("Chyba", f"Uložení selhalo:\n{e}")
            return

        msg = f"Uloženo:\n{path}"
        if mode == "OVERWRITE" and os.path.exists(path + ".bak"):
            msg += f"\n\nBackup:\n{path}.bak"
        messagebox.showinfo("Hotovo", msg)

if __name__ == "__main__":
    app = GitignorePro()
    app.mainloop()
