#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Spellcross LZ tool GUI wrapper

- Uses spell_delz.exe to decompress .LZ and .LZ0 files
- Uses spell_mklz.exe to compress files back to .LZ (or chosen extension)

Designed to be a simple front-end for batch operations.

Notes:
- Expected CLI (common for these utilities):
    spell_delz.exe <input_file> <output_file>
    spell_mklz.exe <input_file> <output_file>
  If your EXEs use different argument order, adjust CMD building in run_job().
"""

from __future__ import annotations

import os
import sys
import threading
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Tuple

import tkinter as tk
from tkinter import ttk, filedialog, messagebox


APP_TITLE = "Spellcross LZ Tool (GUI wrapper)"
DEFAULT_OUT_SUFFIX_COMP = ".LZ"  # default for compression output


@dataclass
class Job:
    mode: str  # "decompress" or "compress"
    input_path: Path
    output_path: Path


def resource_path(p: str) -> Path:
    """
    Resolve a path relative to the script directory (works when packaged).
    """
    base = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
    return (base / p).resolve()


def guess_output_name_for_decompress(in_path: Path) -> str:
    name = in_path.name
    lower = name.lower()
    # Strip common Spellcross LZ extensions
    for ext in (".lz0", ".lz"):
        if lower.endswith(ext):
            return name[: -len(ext)]
    # If unknown, just append ".bin"
    return name + ".bin"


def guess_output_name_for_compress(in_path: Path, out_ext: str) -> str:
    name = in_path.name
    lower = name.lower()
    if lower.endswith(".lz") or lower.endswith(".lz0"):
        # Avoid double-extension
        return in_path.stem + out_ext
    return name + out_ext


class App(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title(APP_TITLE)
        self.minsize(820, 560)

        # Paths to tools (default: alongside script)
        self.delz_path = tk.StringVar(value=str(resource_path("spell_delz.exe")))
        self.mklz_path = tk.StringVar(value=str(resource_path("spell_mklz.exe")))

        self.mode = tk.StringVar(value="decompress")  # "decompress" | "compress"
        self.out_dir = tk.StringVar(value=str(Path.cwd()))
        self.out_ext = tk.StringVar(value=DEFAULT_OUT_SUFFIX_COMP)  # only for compress
        self.keep_tree = tk.BooleanVar(value=False)
        self.overwrite = tk.BooleanVar(value=False)

        self.file_list: List[Path] = []
        self._worker_thread: Optional[threading.Thread] = None
        self._cancel = threading.Event()

        self._build_ui()

    def _build_ui(self) -> None:
        pad = {"padx": 10, "pady": 6}

        tools = ttk.LabelFrame(self, text="Tools")
        tools.pack(fill="x", **pad)
        tools.columnconfigure(1, weight=1)

        ttk.Label(tools, text="spell_delz.exe:").grid(row=0, column=0, sticky="w", padx=8, pady=6)
        ttk.Entry(tools, textvariable=self.delz_path).grid(row=0, column=1, sticky="ew", padx=8, pady=6)
        ttk.Button(tools, text="Browse…", command=self._browse_delz).grid(row=0, column=2, padx=8, pady=6)

        ttk.Label(tools, text="spell_mklz.exe:").grid(row=1, column=0, sticky="w", padx=8, pady=6)
        ttk.Entry(tools, textvariable=self.mklz_path).grid(row=1, column=1, sticky="ew", padx=8, pady=6)
        ttk.Button(tools, text="Browse…", command=self._browse_mklz).grid(row=1, column=2, padx=8, pady=6)

        opts = ttk.LabelFrame(self, text="Operation")
        opts.pack(fill="x", **pad)
        opts.columnconfigure(3, weight=1)

        ttk.Radiobutton(
            opts,
            text="Decompress (.LZ / .LZ0 → raw)",
            variable=self.mode,
            value="decompress",
            command=self._on_mode_changed,
        ).grid(row=0, column=0, sticky="w", padx=8, pady=6)

        ttk.Radiobutton(
            opts,
            text="Compress (raw → .LZ)",
            variable=self.mode,
            value="compress",
            command=self._on_mode_changed,
        ).grid(row=0, column=1, sticky="w", padx=8, pady=6)

        ttk.Label(opts, text="Output folder:").grid(row=1, column=0, sticky="w", padx=8, pady=6)
        ttk.Entry(opts, textvariable=self.out_dir).grid(row=1, column=1, columnspan=2, sticky="ew", padx=8, pady=6)
        ttk.Button(opts, text="Choose…", command=self._browse_out_dir).grid(row=1, column=3, sticky="e", padx=8, pady=6)

        ttk.Label(opts, text="Compression output ext:").grid(row=2, column=0, sticky="w", padx=8, pady=6)
        self.out_ext_entry = ttk.Entry(opts, textvariable=self.out_ext, width=10)
        self.out_ext_entry.grid(row=2, column=1, sticky="w", padx=8, pady=6)
        ttk.Label(opts, text="(e.g. .LZ or .LZ0)").grid(row=2, column=2, sticky="w", padx=8, pady=6)

        ttk.Checkbutton(opts, text="Preserve input folder structure in output", variable=self.keep_tree).grid(
            row=3, column=0, columnspan=2, sticky="w", padx=8, pady=6
        )
        ttk.Checkbutton(opts, text="Overwrite existing files", variable=self.overwrite).grid(
            row=3, column=2, columnspan=2, sticky="w", padx=8, pady=6
        )

        files = ttk.LabelFrame(self, text="Input files")
        files.pack(fill="both", expand=True, **pad)
        files.columnconfigure(0, weight=1)
        files.rowconfigure(1, weight=1)

        btns = ttk.Frame(files)
        btns.grid(row=0, column=0, sticky="ew", padx=8, pady=6)
        ttk.Button(btns, text="Add files…", command=self._add_files).pack(side="left")
        ttk.Button(btns, text="Add folder…", command=self._add_folder).pack(side="left", padx=(8, 0))
        ttk.Button(btns, text="Remove selected", command=self._remove_selected).pack(side="left", padx=(8, 0))
        ttk.Button(btns, text="Clear", command=self._clear_files).pack(side="left", padx=(8, 0))

        self.listbox = tk.Listbox(files, selectmode=tk.EXTENDED)
        self.listbox.grid(row=1, column=0, sticky="nsew", padx=8, pady=(0, 8))
        sb = ttk.Scrollbar(files, orient="vertical", command=self.listbox.yview)
        sb.grid(row=1, column=1, sticky="ns", pady=(0, 8))
        self.listbox.configure(yscrollcommand=sb.set)

        bottom = ttk.Frame(self)
        bottom.pack(fill="x", **pad)
        bottom.columnconfigure(0, weight=1)

        self.progress = ttk.Progressbar(bottom, mode="determinate")
        self.progress.grid(row=0, column=0, sticky="ew", padx=8, pady=6)
        self.btn_run = ttk.Button(bottom, text="Run", command=self._run)
        self.btn_run.grid(row=0, column=1, padx=8, pady=6)
        self.btn_cancel = ttk.Button(bottom, text="Cancel", command=self._cancel_run, state="disabled")
        self.btn_cancel.grid(row=0, column=2, padx=8, pady=6)

        log_frame = ttk.LabelFrame(self, text="Log")
        log_frame.pack(fill="both", expand=False, **pad)
        log_frame.columnconfigure(0, weight=1)

        self.log = tk.Text(log_frame, height=9, wrap="word")
        self.log.grid(row=0, column=0, sticky="ew", padx=8, pady=8)
        log_sb = ttk.Scrollbar(log_frame, orient="vertical", command=self.log.yview)
        log_sb.grid(row=0, column=1, sticky="ns", pady=8)
        self.log.configure(yscrollcommand=log_sb.set)

        self._on_mode_changed()

    def _append_log(self, s: str) -> None:
        self.log.insert("end", s + "\n")
        self.log.see("end")

    def _browse_delz(self) -> None:
        p = filedialog.askopenfilename(title="Select spell_delz.exe", filetypes=[("Executable", "*.exe"), ("All", "*.*")])
        if p:
            self.delz_path.set(p)

    def _browse_mklz(self) -> None:
        p = filedialog.askopenfilename(title="Select spell_mklz.exe", filetypes=[("Executable", "*.exe"), ("All", "*.*")])
        if p:
            self.mklz_path.set(p)

    def _browse_out_dir(self) -> None:
        p = filedialog.askdirectory(title="Select output folder")
        if p:
            self.out_dir.set(p)

    def _on_mode_changed(self) -> None:
        is_comp = self.mode.get() == "compress"
        self.out_ext_entry.configure(state=("normal" if is_comp else "disabled"))

    def _add_files(self) -> None:
        paths = filedialog.askopenfilenames(title="Select input file(s)")
        if paths:
            self._add_paths([Path(p) for p in paths])

    def _add_folder(self) -> None:
        folder = filedialog.askdirectory(title="Select folder to add files from")
        if not folder:
            return
        folder_path = Path(folder)
        files = [p for p in folder_path.rglob("*") if p.is_file()]
        self._add_paths(files)

    def _add_paths(self, paths: List[Path]) -> None:
        added = 0
        for p in paths:
            p = p.resolve()
            if p not in self.file_list:
                self.file_list.append(p)
                added += 1
        if added:
            self.file_list.sort()
            self._refresh_listbox()
            self._append_log(f"Added {added} file(s).")

    def _refresh_listbox(self) -> None:
        self.listbox.delete(0, "end")
        for p in self.file_list:
            self.listbox.insert("end", str(p))

    def _remove_selected(self) -> None:
        sel = list(self.listbox.curselection())
        if not sel:
            return
        for i in reversed(sel):
            del self.file_list[i]
        self._refresh_listbox()
        self._append_log(f"Removed {len(sel)} file(s).")

    def _clear_files(self) -> None:
        self.file_list.clear()
        self._refresh_listbox()
        self._append_log("Cleared file list.")

    def _validate(self) -> Tuple[bool, str]:
        if not self.file_list:
            return False, "No input files selected."

        out_dir = Path(self.out_dir.get()).expanduser()
        if not out_dir.exists() or not out_dir.is_dir():
            return False, "Output folder is invalid."

        if self.mode.get() == "decompress":
            tool = Path(self.delz_path.get())
            if not tool.exists():
                return False, "spell_delz.exe not found. Please set the correct path."
        else:
            tool = Path(self.mklz_path.get())
            if not tool.exists():
                return False, "spell_mklz.exe not found. Please set the correct path."
            ext = self.out_ext.get().strip()
            if not ext:
                return False, "Compression output extension is empty."
            if not ext.startswith("."):
                return False, "Compression output extension should start with '.' (e.g. .LZ)."

        return True, ""

    def _compute_jobs(self) -> List[Job]:
        out_dir = Path(self.out_dir.get()).expanduser().resolve()
        keep_tree = bool(self.keep_tree.get())
        common_root = Path(os.path.commonpath([str(p.parent) for p in self.file_list])) if keep_tree else None

        jobs: List[Job] = []
        for in_path in self.file_list:
            if self.mode.get() == "decompress":
                out_name = guess_output_name_for_decompress(in_path)
            else:
                out_name = guess_output_name_for_compress(in_path, self.out_ext.get().strip())

            if keep_tree and common_root is not None:
                rel = in_path.parent.resolve().relative_to(common_root)
                out_path = (out_dir / rel / out_name).resolve()
            else:
                out_path = (out_dir / out_name).resolve()

            jobs.append(Job(self.mode.get(), in_path, out_path))
        return jobs

    def _run(self) -> None:
        ok, msg = self._validate()
        if not ok:
            messagebox.showerror(APP_TITLE, msg)
            return

        if self._worker_thread and self._worker_thread.is_alive():
            messagebox.showinfo(APP_TITLE, "Already running.")
            return

        self._cancel.clear()
        jobs = self._compute_jobs()

        if not self.overwrite.get():
            existing = [j.output_path for j in jobs if j.output_path.exists()]
            if existing:
                preview = "\n".join(str(p) for p in existing[:10])
                more = "" if len(existing) <= 10 else f"\n… and {len(existing)-10} more"
                messagebox.showerror(
                    APP_TITLE,
                    "Some output files already exist (Overwrite is OFF):\n\n" + preview + more
                )
                return

        self.progress.configure(maximum=len(jobs), value=0)
        self.btn_run.configure(state="disabled")
        self.btn_cancel.configure(state="normal")
        self._append_log(f"=== START ({self.mode.get()}) | {len(jobs)} file(s) ===")

        self._worker_thread = threading.Thread(target=self._worker, args=(jobs,), daemon=True)
        self._worker_thread.start()

    def _cancel_run(self) -> None:
        if self._worker_thread and self._worker_thread.is_alive():
            self._cancel.set()
            self._append_log("Cancel requested… (will stop after current file)")

    def _worker(self, jobs: List[Job]) -> None:
        errors = 0
        done = 0

        for job in jobs:
            if self._cancel.is_set():
                break

            try:
                job.output_path.parent.mkdir(parents=True, exist_ok=True)
                rc, out = self._run_job(job)
                if rc != 0:
                    errors += 1
                    self._ui_log(f"[ERR] {job.input_path.name} -> rc={rc}\n{(out or '').strip()}\n")
                else:
                    self._ui_log(f"[OK ] {job.input_path.name} -> {job.output_path.name}")
            except Exception as e:
                errors += 1
                self._ui_log(f"[EXC] {job.input_path} -> {e}")

            done += 1
            self._ui_progress(done)

        self._ui_done(done, len(jobs), errors, canceled=self._cancel.is_set())

    def _run_job(self, job: Job) -> Tuple[int, str]:
        tool = Path(self.delz_path.get()).expanduser().resolve() if job.mode == "decompress" else Path(self.mklz_path.get()).expanduser().resolve()
        cmd = [str(tool), str(job.input_path), str(job.output_path)]

        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            shell=False,
            check=False
        )
        return proc.returncode, proc.stdout or ""

    def _ui_progress(self, value: int) -> None:
        self.after(0, lambda: self.progress.configure(value=value))

    def _ui_log(self, s: str) -> None:
        self.after(0, lambda: self._append_log(s))

    def _ui_done(self, done: int, total: int, errors: int, canceled: bool) -> None:
        def _finish() -> None:
            self.btn_run.configure(state="normal")
            self.btn_cancel.configure(state="disabled")
            status = "CANCELED" if canceled else "DONE"
            self._append_log(f"=== {status} | processed {done}/{total} | errors: {errors} ===")
            if errors and not canceled:
                messagebox.showwarning(APP_TITLE, f"Finished with {errors} error(s). Check log for details.")
            elif canceled:
                messagebox.showinfo(APP_TITLE, "Canceled.")
            else:
                messagebox.showinfo(APP_TITLE, "Finished successfully.")
        self.after(0, _finish)


def main() -> None:
    # Better DPI scaling on Windows (optional)
    try:
        from ctypes import windll  # type: ignore
        windll.shcore.SetProcessDpiAwareness(1)  # Windows 8.1+
    except Exception:
        pass

    app = App()
    app.mainloop()


if __name__ == "__main__":
    main()
