#!/usr/bin/env python3
import sys
import struct
import logging
import os
from pathlib import Path

logging.basicConfig(level=logging.INFO)
log = logging.getLogger('unlz')

# quick'n'dirty bitstream reader
class Bits:
    def __init__(self, f):
        self.f = f
        self.avail = 0
        self.byte = None

    def read(self, nbits):
        """Read nbits bits and return them MSB-first in an int.
        Return None if EOF occurs while reading nbits.
        """
        result = 0
        while nbits > 0:
            if self.avail >= nbits:
                result <<= nbits
                result |= (self.byte >> (self.avail - nbits)) & ((1 << nbits) - 1)
                self.avail -= nbits
                return result
            else:
                if self.avail > 0:
                    result <<= self.avail
                    result |= self.byte & ((1 << self.avail) - 1)
                    nbits -= self.avail

                bs = self.f.read(1)
                if bs == b'':
                    return None

                self.byte = bs[0]
                self.avail = 8

        return result


class LZFile:
    def __init__(self, f):
        self.iter = iter_unpack(f)
        self.buf = b''

    def read(self, nbytes):
        result = b''
        while True:
            if len(self.buf) >= nbytes:
                result += self.buf[:nbytes]
                self.buf = self.buf[nbytes:]
                return result
            else:
                result += self.buf
                nbytes -= len(self.buf)
                try:
                    self.buf = next(self.iter)
                except StopIteration:
                    return None

    def __iter__(self):
        return self.iter


# The algorithm follows https://en.wikipedia.org/wiki/Lzw
def iter_unpack(f):
    # uint16_t = initial dictionary size
    # uint8_t  = initial bits per symbol
    hdr = f.read(3)
    if len(hdr) != 3:
        raise ValueError("File too short (missing header)")

    dict_size, bits_per_symbol = struct.unpack('<HB', hdr)
    log.debug('dict_size = %d, bits per symbol = %d', dict_size, bits_per_symbol)

    if dict_size < 256:
        dictionary = []
        init = f.read(dict_size)
        if len(init) != dict_size:
            raise ValueError("File too short (missing initial dictionary)")
        for byte in init:
            dictionary.append(bytes([byte]))
    else:
        dictionary = [bytes([byte]) for byte in range(256)]

    # add a special symbol (CLEAR), 2xCLEAR == EOF
    dictionary.append(None)

    clear_dictionary = list(dictionary)
    clear_bits_per_symbol = bits_per_symbol

    br = Bits(f)
    previous_is_clear = False
    while True:
        i = br.read(bits_per_symbol)
        if i is None:
            break  # EOF

        if i == len(dictionary) - 1:
            if dictionary[-1] is None:
                pass
            else:
                dictionary[-1] = dictionary[-1][:-1] + bytes([dictionary[-1][0]])

        if i < 0 or i >= len(dictionary):
            raise ValueError(f"Invalid dictionary index {i} (dict size {len(dictionary)})")

        symbol = dictionary[i]
        log.debug('dict[%d] == %s', i, symbol)

        if symbol is None:
            if previous_is_clear:
                break  # EOF
            dictionary = list(clear_dictionary)
            bits_per_symbol = clear_bits_per_symbol
            previous_is_clear = True
            continue
        else:
            previous_is_clear = False

        if dictionary[-1] is not None:
            dictionary[-1] = dictionary[-1][:-1] + bytes([symbol[0]])

        dictionary.append(symbol + b'\0')

        if len(dictionary) + 1 > (1 << bits_per_symbol):
            bits_per_symbol += 1

        yield symbol


def unpack_bytes(src_path: Path) -> bytes:
    with src_path.open('rb') as f:
        return b''.join(iter_unpack(f))


def default_output_name(src_path: Path) -> str:
    # typicky: BUY.LZ -> BUY.bin, BUY.LZ0 -> BUY.bin, něco.def.lz -> něco.def (bez poslední přípony)
    stem = src_path.stem
    ext = src_path.suffix.lower()

    # když je to "něco.lz" / "něco.lz0" / "něco.lzw"
    if ext in {".lz", ".lz0", ".lzw"}:
        # pokud původní soubor měl jen jednu příponu (BUY.LZ), dej BUY.bin
        # pokud měl víc (map.def.lz), vrať map.def
        if '.' not in stem:
            return f"{stem}.bin"
        return stem  # zachová předchozí příponu

    # fallback
    return f"{src_path.name}.bin"


def decompress_to_folder(src_path: Path, out_dir: Path) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    out_name = default_output_name(src_path)
    out_path = out_dir / out_name

    log.info("Decompressing: %s -> %s", src_path, out_path)
    with src_path.open('rb') as f_in, out_path.open('wb') as f_out:
        for chunk in iter_unpack(f_in):
            f_out.write(chunk)

    return out_path


def run_gui():
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox

    root = tk.Tk()
    root.title("Spellcross LZ Unpacker")
    root.resizable(False, False)

    pad = 10
    frm = ttk.Frame(root, padding=pad)
    frm.grid(row=0, column=0, sticky="nsew")

    in_var = tk.StringVar()
    out_var = tk.StringVar(value=str(Path.cwd()))

    def pick_input():
        p = filedialog.askopenfilename(
            title="Vyber vstupní soubor",
            filetypes=[
                ("LZ files", "*.lz *.LZ *.lz0 *.LZ0 *.lzw *.LZW"),
                ("All files", "*.*"),
            ],
        )
        if p:
            in_var.set(p)

    def pick_output_dir():
        p = filedialog.askdirectory(title="Vyber cílovou složku")
        if p:
            out_var.set(p)

    def set_busy(is_busy: bool):
        btn_unpack["state"] = ("disabled" if is_busy else "normal")
        btn_in["state"] = ("disabled" if is_busy else "normal")
        btn_out["state"] = ("disabled" if is_busy else "normal")
        if is_busy:
            prog.start(12)
            status_var.set("Pracuju...")
        else:
            prog.stop()

    def do_unpack():
        src = in_var.get().strip()
        outd = out_var.get().strip()

        if not src:
            messagebox.showwarning("Chybí vstup", "Vyber vstupní soubor.")
            return
        if not outd:
            messagebox.showwarning("Chybí složka", "Vyber cílovou složku.")
            return

        src_path = Path(src)
        out_dir = Path(outd)

        if not src_path.exists():
            messagebox.showerror("Chyba", f"Vstupní soubor neexistuje:\n{src_path}")
            return

        set_busy(True)
        root.update_idletasks()

        try:
            out_path = decompress_to_folder(src_path, out_dir)
            status_var.set(f"Hotovo: {out_path.name}")
            messagebox.showinfo("Hotovo", f"Uloženo do:\n{out_path}")
        except Exception as e:
            log.exception("Decompression failed")
            status_var.set("Chyba")
            messagebox.showerror("Chyba při extrakci", str(e))
        finally:
            set_busy(False)

    # layout
    ttk.Label(frm, text="Vstupní soubor:").grid(row=0, column=0, sticky="w")
    ent_in = ttk.Entry(frm, textvariable=in_var, width=60)
    ent_in.grid(row=1, column=0, sticky="we")
    btn_in = ttk.Button(frm, text="Vybrat…", command=pick_input)
    btn_in.grid(row=1, column=1, padx=(8, 0))

    ttk.Label(frm, text="Cílová složka:").grid(row=2, column=0, sticky="w", pady=(10, 0))
    ent_out = ttk.Entry(frm, textvariable=out_var, width=60)
    ent_out.grid(row=3, column=0, sticky="we")
    btn_out = ttk.Button(frm, text="Vybrat…", command=pick_output_dir)
    btn_out.grid(row=3, column=1, padx=(8, 0))

    btn_unpack = ttk.Button(frm, text="Extrahovat", command=do_unpack)
    btn_unpack.grid(row=4, column=0, sticky="w", pady=(12, 0))

    prog = ttk.Progressbar(frm, mode="indeterminate", length=220)
    prog.grid(row=4, column=0, padx=(110, 0), pady=(12, 0), sticky="w")

    status_var = tk.StringVar(value="Připraveno")
    ttk.Label(frm, textvariable=status_var).grid(row=5, column=0, columnspan=2, sticky="w", pady=(10, 0))

    root.mainloop()


def print_usage_and_exit():
    exe = Path(sys.argv[0]).name
    msg = f"""Usage:
  {exe} --gui
  {exe} <input.lz> > out.bin
  {exe} <input.lz> <output_dir>

Notes:
  - CLI mode without output_dir writes decompressed bytes to stdout (original behavior).
  - With output_dir, saves the output file into that folder.
"""
    print(msg, file=sys.stderr)
    sys.exit(2)


if __name__ == '__main__':
    # GUI mode
    if len(sys.argv) >= 2 and sys.argv[1] in {"--gui", "-g"}:
        run_gui()
        sys.exit(0)

    # CLI mode (backwards compatible)
    if len(sys.argv) < 2:
        # default: open GUI if no args (friendly)
        run_gui()
        sys.exit(0)

    in_path = Path(sys.argv[1])

    # if user provides output dir, write to folder
    if len(sys.argv) >= 3:
        out_dir = Path(sys.argv[2])
        out_path = decompress_to_folder(in_path, out_dir)
        print(str(out_path))
        sys.exit(0)

    # original behavior: write to stdout
    with in_path.open('rb') as f:
        for chunk in iter_unpack(f):
            sys.stdout.buffer.write(chunk)
