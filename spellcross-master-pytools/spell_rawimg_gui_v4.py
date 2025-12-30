#!/usr/bin/env python3
import subprocess, sys
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path

TOOL = Path(__file__).with_name('spell_rawimg_tool_v4.py')

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title('Spellcross RAW → PNG (mini) v4')
        self.resizable(False, False)

        self.bin_var = tk.StringVar()
        self.pal_var = tk.StringVar()
        self.out_var = tk.StringVar()

        self.ui_var = tk.StringVar(value='none')
        self.orient_var = tk.StringVar(value='transpose')
        self.fmt_var = tk.StringVar(value='auto')
        self.w_var = tk.StringVar()
        self.h_var = tk.StringVar()
        self.swap_var = tk.BooleanVar(value=False)
        self.trans_var = tk.StringVar(value='0')
        self.scale_var = tk.StringVar(value='1')

        self._build()

    def _build(self):
        f = ttk.Frame(self, padding=10)
        f.grid()

        ttk.Label(f, text='Input .bin:').grid(row=0, column=0, sticky='w')
        ttk.Entry(f, textvariable=self.bin_var, width=60).grid(row=1, column=0, sticky='w')
        ttk.Button(f, text='Vybrat…', command=self.pick_bin).grid(row=1, column=1, padx=8)

        ttk.Label(f, text='Palette .PAL (volitelné):').grid(row=2, column=0, sticky='w', pady=(10,0))
        ttk.Entry(f, textvariable=self.pal_var, width=60).grid(row=3, column=0, sticky='w')
        ttk.Button(f, text='Vybrat…', command=self.pick_pal).grid(row=3, column=1, padx=8)

        ttk.Label(f, text='Output .png:').grid(row=4, column=0, sticky='w', pady=(10,0))
        ttk.Entry(f, textvariable=self.out_var, width=60).grid(row=5, column=0, sticky='w')
        ttk.Button(f, text='Uložit jako…', command=self.pick_out).grid(row=5, column=1, padx=8)

        p = ttk.LabelFrame(f, text='Parametry', padding=10)
        p.grid(row=6, column=0, columnspan=2, sticky='we', pady=(12,0))

        ttk.Label(p, text='UI layout:').grid(row=0, column=0, sticky='w')
        ttk.Combobox(p, textvariable=self.ui_var, width=10, state='readonly', values=['none','buy','hierarch']).grid(row=0, column=1, padx=(6,18))
        ttk.Label(p, text='Orient:').grid(row=0, column=2, sticky='w')
        ttk.Combobox(p, textvariable=self.orient_var, width=10, state='readonly', values=['transpose','rot90cw','rot90ccw','flipx','flipy','none']).grid(row=0, column=3, padx=(6,18))
        ttk.Checkbutton(p, text='Swap nibbles', variable=self.swap_var).grid(row=0, column=4, sticky='w')

        ttk.Label(p, text='Format:').grid(row=1, column=0, sticky='w', pady=(8,0))
        ttk.Combobox(p, textvariable=self.fmt_var, width=10, state='readonly', values=['auto','8bpp','packed4']).grid(row=1, column=1, padx=(6,18), pady=(8,0))
        ttk.Label(p, text='W:').grid(row=1, column=2, sticky='w', pady=(8,0))
        ttk.Entry(p, textvariable=self.w_var, width=8).grid(row=1, column=3, sticky='w', padx=(6,18), pady=(8,0))
        ttk.Label(p, text='H:').grid(row=2, column=2, sticky='w', pady=(8,0))
        ttk.Entry(p, textvariable=self.h_var, width=8).grid(row=2, column=3, sticky='w', padx=(6,18), pady=(8,0))

        ttk.Label(p, text='Transparent idx:').grid(row=2, column=0, sticky='w', pady=(8,0))
        ttk.Entry(p, textvariable=self.trans_var, width=12).grid(row=2, column=1, sticky='w', padx=(6,18), pady=(8,0))
        ttk.Label(p, text='Scale:').grid(row=3, column=0, sticky='w', pady=(8,0))
        ttk.Combobox(p, textvariable=self.scale_var, width=6, state='readonly', values=['1','2','3','4','5','6','8']).grid(row=3, column=1, sticky='w', padx=(6,18), pady=(8,0))

        b = ttk.Frame(f)
        b.grid(row=7, column=0, columnspan=2, sticky='w', pady=(12,0))
        ttk.Button(b, text='Konvertovat', command=self.convert).grid(row=0, column=0)

    def pick_bin(self):
        p = filedialog.askopenfilename(title='Vyber .bin', filetypes=[('BIN','*.bin'),('All','*.*')])
        if not p: return
        self.bin_var.set(p)
        bp = Path(p)
        if not self.out_var.get().strip():
            self.out_var.set(str(bp.with_name(bp.stem + '_restored.png')))

    def pick_pal(self):
        p = filedialog.askopenfilename(title='Vyber .pal', filetypes=[('PAL','*.pal;*.PAL'),('All','*.*')])
        if p: self.pal_var.set(p)

    def pick_out(self):
        p = filedialog.asksaveasfilename(title='Uložit PNG', defaultextension='.png', filetypes=[('PNG','*.png')])
        if p: self.out_var.set(p)

    def convert(self):
        try:
            inp = self.bin_var.get().strip()
            if not inp:
                raise ValueError('Vyber input .bin')
            out = self.out_var.get().strip() or (Path(inp).with_name(Path(inp).stem + '_restored.png'))

            cmd = [sys.executable, str(TOOL), inp, '-o', str(out), '--format', self.fmt_var.get()]
            if self.pal_var.get().strip():
                cmd += ['-p', self.pal_var.get().strip()]
            if self.w_var.get().strip() and self.h_var.get().strip():
                cmd += ['-w', self.w_var.get().strip(), '-h', self.h_var.get().strip()]
            if self.swap_var.get():
                cmd += ['--swap-nibbles']
            if self.trans_var.get().strip():
                cmd += ['--transparent', self.trans_var.get().strip()]
            cmd += ['--scale', self.scale_var.get().strip() or '1']

            ui = self.ui_var.get()
            if ui != 'none':
                cmd += ['--ui-layout', ui, '--ui-orient', self.orient_var.get()]

            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0:
                raise RuntimeError((r.stderr or r.stdout or 'Unknown error').strip())
            messagebox.showinfo('OK', (r.stdout or 'Hotovo').strip())
        except Exception as e:
            messagebox.showerror('Chyba', str(e))

if __name__ == '__main__':
    App().mainloop()
