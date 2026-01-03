#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
spell_extractfs_gui.py
Simple UI wrapper over spell_extractfs.exe (Spellcross *.FS dearchivator).

Features:
- Add one or more .FS files to a batch list
- Choose destination folder (the extractor is executed with cwd=destination)
- Optional: extract each archive into its own subfolder (avoids collisions)
- Live log + progress, cancel between files

Requirements: Python 3.x (tkinter included on most Windows installs)
Usage: python spell_extractfs_gui.py
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

APP_TITLE = "Spellcross FS Extractor (GUI wrapper)"

def guess_exe_path() -> Path | None:
    """
    Try to locate spell_extractfs.exe next to this script or in CWD.
    """
    candidates = []
    try:
        here = Path(__file__).resolve().parent
        candidates += [here / "spell_extractfs.exe", here / "spell_extractfs.EXE"]
    except Exception:
        pass
    cwd = Path.cwd()
    candidates += [cwd / "spell_extractfs.exe", cwd / "spell_extractfs.EXE"]
    for p in candidates:
        if p.exists():
            return p
    return None

class App(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title(APP_TITLE)
        self.minsize(860, 520)

        self.exe_path = tk.StringVar(value=str(guess_exe_path() or "spell_extractfs.exe"))
        self.dest_dir = tk.StringVar(value=str(Path.cwd()))
        self.make_subfolders = tk.BooleanVar(value=True)

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
        ttk.Label(exe_row, text="Extractor exe:").pack(side="left")
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

        # Options row
        opt_row = ttk.Frame(top)
        opt_row.pack(fill="x", pady=(8, 0))
        ttk.Checkbutton(
            opt_row,
            text="Extract each archive into its own subfolder (recommended)",
            variable=self.make_subfolders
        ).pack(side="left")

        # Middle: file list + buttons
        mid = ttk.Frame(self)
        mid.pack(fill="both", expand=True, **pad)

        left = ttk.Frame(mid)
        left.pack(side="left", fill="both", expand=True)

        ttk.Label(left, text="Archives to extract (.FS):").pack(anchor="w")

        self.listbox = tk.Listbox(left, selectmode=tk.EXTENDED)
        self.listbox.pack(fill="both", expand=True, pady=(4, 0))
        self.listbox.bind("<Delete>", lambda e: self._remove_selected())

        right = ttk.Frame(mid)
        right.pack(side="left", fill="y", padx=(10, 0))

        ttk.Button(right, text="Add files…", command=self._add_files).pack(fill="x", pady=(0, 6))
        ttk.Button(right, text="Add folder…", command=self._add_folder).pack(fill="x", pady=(0, 6))
        ttk.Button(right, text="Remove selected", command=self._remove_selected).pack(fill="x", pady=(0, 6))
        ttk.Button(right, text="Clear list", command=self._clear_list).pack(fill="x", pady=(0, 14))

        self.btn_extract_all = ttk.Button(right, text="Extract all", command=self._start_extract_all)
        self.btn_extract_all.pack(fill="x", pady=(0, 6))

        self.btn_extract_sel = ttk.Button(right, text="Extract selected", command=self._start_extract_selected)
        self.btn_extract_sel.pack(fill="x", pady=(0, 6))

        self.btn_cancel = ttk.Button(right, text="Cancel", command=self._cancel, state="disabled")
        self.btn_cancel.pack(fill="x", pady=(0, 6))

        # Bottom: progress + log
        bot = ttk.Frame(self)
        bot.pack(fill="both", expand=False, **pad)

        self.progress = ttk.Progressbar(bot, mode="determinate")
        self.progress.pack(fill="x")

        ttk.Label(bot, text="Log:").pack(anchor="w", pady=(8, 0))
        self.txt = tk.Text(bot, height=10, wrap="word")
        self.txt.pack(fill="both", expand=True, pady=(4, 0))
        self.txt.configure(state="disabled")

        self._log(f"Ready. If extractor isn't found, click 'Browse…' and select spell_extractfs.exe.")

    # ---------------- Helpers ----------------
    def _browse_exe(self) -> None:
        p = filedialog.askopenfilename(
            title="Select spell_extractfs.exe",
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
            title="Select .FS archive(s)",
            filetypes=[("Spellcross FS archives", "*.FS *.fs"), ("All files", "*.*")]
        )
        for f in files:
            self._add_path(Path(f))

    def _add_folder(self) -> None:
        folder = filedialog.askdirectory(title="Select folder with .FS files")
        if not folder:
            return
        p = Path(folder)
        fs_files = sorted(list(p.glob("*.FS")) + list(p.glob("*.fs")))
        if not fs_files:
            messagebox.showinfo(APP_TITLE, "No .FS files found in the selected folder.")
            return
        for f in fs_files:
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
        state_run = "disabled" if running else "normal"
        self.btn_extract_all.configure(state=state_run if not running else "disabled")
        self.btn_extract_sel.configure(state=state_run if not running else "disabled")
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

    # ---------------- Extraction ----------------
    def _validate(self) -> tuple[Path, Path] | None:
        exe = Path(self.exe_path.get()).expanduser()
        if not exe.is_file():
            messagebox.showerror(APP_TITLE, f"Extractor not found:\n{exe}")
            return None
        dest = Path(self.dest_dir.get()).expanduser()
        try:
            dest.mkdir(parents=True, exist_ok=True)
        except Exception as e:
            messagebox.showerror(APP_TITLE, f"Cannot create destination folder:\n{dest}\n\n{e}")
            return None
        return exe, dest

    def _get_all_items(self) -> list[Path]:
        return [Path(x) for x in self.listbox.get(0, tk.END)]

    def _get_selected_items(self) -> list[Path]:
        sel = self.listbox.curselection()
        items = self._get_all_items()
        return [items[i] for i in sel] if sel else []

    def _start_extract_all(self) -> None:
        items = self._get_all_items()
        if not items:
            messagebox.showinfo(APP_TITLE, "Add at least one .FS file first.")
            return
        self._start_worker(items)

    def _start_extract_selected(self) -> None:
        items = self._get_selected_items()
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
            messagebox.showwarning(APP_TITLE, "Extraction is already running.")
            return

        self._cancel_flag.clear()
        self._set_running(True)

        self.progress.configure(maximum=max(1, len(items)))
        self.progress.configure(value=0)

        def run():
            ok = 0
            fail = 0
            for i, fs_path in enumerate(items, start=1):
                if self._cancel_flag.is_set():
                    self._log("Canceled.")
                    break

                fs_path = fs_path.expanduser()
                if not fs_path.is_file():
                    self._log(f"[{i}/{len(items)}] SKIP (missing): {fs_path}")
                    fail += 1
                    self._update_progress(i)
                    continue

                out_dir = dest
                if self.make_subfolders.get():
                    # smart: pokud extractor stejně tvoří data\<stem>, nevytvářej další <stem>
                    stem = fs_path.stem
                    if stem.lower() == "data":
                        out_dir = dest
                    else:
                        out_dir = dest / stem
                    try:
                        out_dir.mkdir(parents=True, exist_ok=True)
                    except Exception as e:
                        self._log(f"[{i}/{len(items)}] FAIL create folder {out_dir}: {e}")
                        fail += 1
                        self._update_progress(i)
                        continue

                self._log(f"[{i}/{len(items)}] Extracting: {fs_path.name} -> {out_dir}")
                try:
                    # spell_extractfs.exe is called with a single argument (input FS)
                    # Output location is controlled by current working directory.
                    proc = subprocess.run(
                        [str(exe), str(fs_path)],
                        cwd=str(out_dir),
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,
                        text=True,
                        errors="replace"
                    )
                    if proc.stdout.strip():
                        for line in proc.stdout.splitlines():
                            self._log("    " + line)
                    if proc.returncode == 0:
                        ok += 1
                        self._log(f"    OK")
                    else:
                        fail += 1
                        self._log(f"    FAIL (exit code {proc.returncode})")
                except FileNotFoundError:
                    fail += 1
                    self._log(f"    FAIL: extractor not found: {exe}")
                    break
                except Exception as e:
                    fail += 1
                    self._log(f"    FAIL: {e}")

                self._update_progress(i)

            self._log(f"Done. Success: {ok}, Failed: {fail}.")
            self.after(0, lambda: self._set_running(False))

        self._work_thread = threading.Thread(target=run, daemon=True)
        self._work_thread.start()

    def _update_progress(self, value: int) -> None:
        self.after(0, lambda: self.progress.configure(value=value))

    def _cancel(self) -> None:
        self._cancel_flag.set()
        self._log("Cancel requested (will stop after current file).")

def main() -> int:
    try:
        app = App()
        app.mainloop()
        return 0
    except KeyboardInterrupt:
        return 0

if __name__ == "__main__":
    raise SystemExit(main())
