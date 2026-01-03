#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
spell_delz_gui.py
Simple UI wrapper over spell_delz.exe (Spellcross LZ/LZ0 decompressor).

Features:
- Add one or more .LZ/.LZ0 files (batch)
- Choose destination folder
- Output filename is derived from input (removes .lz/.lz0, default adds .bin if no ext)
- Tries two CLI styles:
    1) spell_delz.exe <input> <output>
    2) spell_delz.exe <input>   (with cwd=destination)
  If style (1) fails, it automatically falls back to style (2).

Requirements: Python 3.x with tkinter
Usage: python spell_delz_gui.py
"""

from __future__ import annotations

import os
import sys
import threading
import queue
import subprocess
from pathlib import Path
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

APP_TITLE = "Spellcross LZ Decompressor (spell_delz.exe wrapper)"

def guess_exe_path() -> Path | None:
    candidates = []
    try:
        here = Path(__file__).resolve().parent
        candidates += [here / "spell_delz.exe", here / "spell_delz.EXE"]
    except Exception:
        pass
    cwd = Path.cwd()
    candidates += [cwd / "spell_delz.exe", cwd / "spell_delz.EXE"]
    for p in candidates:
        if p.exists():
            return p
    return None

def derive_output_name(inp: Path) -> str:
    name = inp.name
    lower = name.lower()
    if lower.endswith(".lz0"):
        base = name[:-4]
        # if it had only .lz0, produce .bin
        return base if Path(base).suffix else (base + ".bin")
    if lower.endswith(".lz"):
        base = name[:-3]
        return base if Path(base).suffix else (base + ".bin")
    # unknown extension: append .bin
    return name + ".bin"

def unique_target_path(dst: Path) -> Path:
    if not dst.exists():
        return dst
    stem = dst.stem
    suffix = dst.suffix
    parent = dst.parent
    i = 1
    while True:
        cand = parent / f"{stem}_{i}{suffix}"
        if not cand.exists():
            return cand
        i += 1

class App(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title(APP_TITLE)
        self.minsize(900, 540)

        self.exe_path = tk.StringVar(value=str(guess_exe_path() or "spell_delz.exe"))
        self.dest_dir = tk.StringVar(value=str(Path.cwd()))
        self.overwrite = tk.BooleanVar(value=False)

        self._work_thread: threading.Thread | None = None
        self._cancel_flag = threading.Event()
        self._log_q: "queue.Queue[str]" = queue.Queue()

        self._build_ui()
        self._poll_log()

    # ---------------- UI ----------------
    def _build_ui(self) -> None:
        pad = {"padx": 10, "pady": 6}

        top = ttk.Frame(self)
        top.pack(fill="x", **pad)

        # EXE row
        exe_row = ttk.Frame(top)
        exe_row.pack(fill="x")
        ttk.Label(exe_row, text="Decompressor exe:").pack(side="left")
        exe_entry = ttk.Entry(exe_row, textvariable=self.exe_path)
        exe_entry.pack(side="left", fill="x", expand=True, padx=(8, 8))
        ttk.Button(exe_row, text="Browse…", command=self._browse_exe).pack(side="left")

        # Destination row
        dst_row = ttk.Frame(top)
        dst_row.pack(fill="x", pady=(8, 0))
        ttk.Label(dst_row, text="Destination folder:").pack(side="left")
        dst_entry = ttk.Entry(dst_row, textvariable=self.dest_dir)
        dst_entry.pack(side="left", fill="x", expand=True, padx=(8, 8))
        ttk.Button(dst_row, text="Browse…", command=self._browse_dest).pack(side="left")

        opt_row = ttk.Frame(top)
        opt_row.pack(fill="x", pady=(8, 0))
        ttk.Checkbutton(opt_row, text="Overwrite existing files", variable=self.overwrite).pack(side="left")

        mid = ttk.Frame(self)
        mid.pack(fill="both", expand=True, **pad)

        left = ttk.Frame(mid)
        left.pack(side="left", fill="both", expand=True)

        ttk.Label(left, text="Compressed files (.LZ / .LZ0):").pack(anchor="w")

        self.listbox = tk.Listbox(left, selectmode=tk.EXTENDED)
        self.listbox.pack(fill="both", expand=True, pady=(4, 0))
        self.listbox.bind("<Delete>", lambda e: self._remove_selected())

        right = ttk.Frame(mid)
        right.pack(side="left", fill="y", padx=(10, 0))

        ttk.Button(right, text="Add files…", command=self._add_files).pack(fill="x", pady=(0, 6))
        ttk.Button(right, text="Add folder…", command=self._add_folder).pack(fill="x", pady=(0, 6))
        ttk.Button(right, text="Remove selected", command=self._remove_selected).pack(fill="x", pady=(0, 6))
        ttk.Button(right, text="Clear list", command=self._clear_list).pack(fill="x", pady=(0, 14))

        self.btn_run_all = ttk.Button(right, text="Decompress all", command=self._start_all)
        self.btn_run_all.pack(fill="x", pady=(0, 6))

        self.btn_run_sel = ttk.Button(right, text="Decompress selected", command=self._start_selected)
        self.btn_run_sel.pack(fill="x", pady=(0, 6))

        self.btn_cancel = ttk.Button(right, text="Cancel", command=self._cancel, state="disabled")
        self.btn_cancel.pack(fill="x", pady=(0, 6))

        bot = ttk.Frame(self)
        bot.pack(fill="both", expand=False, **pad)

        self.progress = ttk.Progressbar(bot, mode="determinate")
        self.progress.pack(fill="x")

        ttk.Label(bot, text="Log:").pack(anchor="w", pady=(8, 0))
        self.txt = tk.Text(bot, height=10, wrap="word")
        self.txt.pack(fill="both", expand=True, pady=(4, 0))
        self.txt.configure(state="disabled")

        self._log("Ready. If spell_delz.exe isn't found, click 'Browse…' and select it.")

    def _browse_exe(self) -> None:
        p = filedialog.askopenfilename(
            title="Select spell_delz.exe",
            filetypes=[("Executable", "*.exe"), ("All files", "*.*")]
        )
        if p:
            self.exe_path.set(p)

    def _browse_dest(self) -> None:
        p = filedialog.askdirectory(title="Select destination folder")
        if p:
            self.dest_dir.set(p)

    def _add_files(self) -> None:
        files = filedialog.askopenfilenames(
            title="Select .LZ / .LZ0 file(s)",
            filetypes=[("Spellcross compressed", "*.LZ *.lz *.LZ0 *.lz0"), ("All files", "*.*")]
        )
        for f in files:
            self._add_path(Path(f))

    def _add_folder(self) -> None:
        folder = filedialog.askdirectory(title="Select folder with .LZ/.LZ0 files")
        if not folder:
            return
        p = Path(folder)
        lz_files = sorted(list(p.glob("*.LZ")) + list(p.glob("*.lz")) + list(p.glob("*.LZ0")) + list(p.glob("*.lz0")))
        if not lz_files:
            messagebox.showinfo(APP_TITLE, "No .LZ/.LZ0 files found in the selected folder.")
            return
        for f in lz_files:
            self._add_path(f)

    def _add_path(self, p: Path) -> None:
        p = p.resolve()
        existing = set(self.listbox.get(0, tk.END))
        if str(p) not in existing:
            self.listbox.insert(tk.END, str(p))

    def _remove_selected(self) -> None:
        sel = list(self.listbox.curselection())
        for idx in reversed(sel):
            self.listbox.delete(idx)

    def _clear_list(self) -> None:
        self.listbox.delete(0, tk.END)

    def _set_running(self, running: bool) -> None:
        self.btn_run_all.configure(state="disabled" if running else "normal")
        self.btn_run_sel.configure(state="disabled" if running else "normal")
        self.btn_cancel.configure(state="normal" if running else "disabled")

    def _log(self, msg: str) -> None:
        self._log_q.put(msg)

    def _poll_log(self) -> None:
        try:
            while True:
                msg = self._log_q.get_nowait()
                self.txt.configure(state="normal")
                self.txt.insert("end", msg + "\n")
                self.txt.see("end")
                self.txt.configure(state="disabled")
        except queue.Empty:
            pass
        self.after(120, self._poll_log)

    # ---------------- Run ----------------
    def _validate(self) -> tuple[Path, Path] | None:
        exe = Path(self.exe_path.get()).expanduser()
        if not exe.is_file():
            messagebox.showerror(APP_TITLE, f"Decompressor not found:\n{exe}")
            return None
        dest = Path(self.dest_dir.get()).expanduser()
        try:
            dest.mkdir(parents=True, exist_ok=True)
        except Exception as e:
            messagebox.showerror(APP_TITLE, f"Cannot create destination folder:\n{dest}\n\n{e}")
            return None
        return exe, dest

    def _all_items(self) -> list[Path]:
        return [Path(x) for x in self.listbox.get(0, tk.END)]

    def _selected_items(self) -> list[Path]:
        sel = self.listbox.curselection()
        items = self._all_items()
        return [items[i] for i in sel] if sel else []

    def _start_all(self) -> None:
        items = self._all_items()
        if not items:
            messagebox.showinfo(APP_TITLE, "Add at least one .LZ/.LZ0 file first.")
            return
        self._start_worker(items)

    def _start_selected(self) -> None:
        items = self._selected_items()
        if not items:
            messagebox.showinfo(APP_TITLE, "Select one or more items in the list.")
            return
        self._start_worker(items)

    def _start_worker(self, items: list[Path]) -> None:
        v = self._validate()
        if not v:
            return
        exe, dest = v
        if self._work_thread and self._work_thread.is_alive():
            messagebox.showwarning(APP_TITLE, "Decompression is already running.")
            return

        self._cancel_flag.clear()
        self._set_running(True)

        self.progress.configure(maximum=max(1, len(items)))
        self.progress.configure(value=0)

        def run():
            ok = 0
            fail = 0

            for i, inp in enumerate(items, start=1):
                if self._cancel_flag.is_set():
                    self._log("Canceled.")
                    break

                inp = inp.expanduser()
                if not inp.is_file():
                    self._log(f"[{i}/{len(items)}] SKIP (missing): {inp}")
                    fail += 1
                    self._update_progress(i)
                    continue

                out_name = derive_output_name(inp)
                out_path = dest / out_name

                if out_path.exists() and not self.overwrite.get():
                    out_path = unique_target_path(out_path)

                self._log(f"[{i}/{len(items)}] Decompress: {inp.name} -> {out_path.name}")

                # Try CLI style: <input> <output>
                rc, out_text = self._run_proc(exe, [str(inp), str(out_path)], cwd=dest)

                # If fails, try fallback: <input> with cwd=dest
                if rc != 0:
                    self._log("    (fallback) trying: spell_delz.exe <input> with cwd=destination")
                    rc2, out_text2 = self._run_proc(exe, [str(inp)], cwd=dest)
                    if out_text2:
                        out_text = (out_text + "\n" + out_text2).strip()
                    rc = rc2

                if out_text.strip():
                    for line in out_text.splitlines():
                        self._log("    " + line)

                if rc == 0:
                    ok += 1
                    self._log("    OK")
                else:
                    fail += 1
                    self._log(f"    FAIL (exit code {rc})")

                self._update_progress(i)

            self._log(f"Done. Success: {ok}, Failed: {fail}.")
            self.after(0, lambda: self._set_running(False))

        self._work_thread = threading.Thread(target=run, daemon=True)
        self._work_thread.start()

    def _run_proc(self, exe: Path, args: list[str], cwd: Path) -> tuple[int, str]:
        try:
            proc = subprocess.run(
                [str(exe), *args],
                cwd=str(cwd),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                errors="replace"
            )
            return proc.returncode, proc.stdout or ""
        except Exception as e:
            return 999, str(e)

    def _update_progress(self, value: int) -> None:
        self.after(0, lambda: self.progress.configure(value=value))

    def _cancel(self) -> None:
        self._cancel_flag.set()
        self._log("Cancel requested (will stop after current file).")

def main() -> int:
    app = App()
    app.mainloop()
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
