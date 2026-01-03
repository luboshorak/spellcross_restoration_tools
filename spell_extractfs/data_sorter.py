#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
sort_by_extension_in_leaf_dirs.py

Třídí soubory do podsložek podle přípony.
Zachová existující adresářovou strukturu; zasahuje jen v koncových (leaf) adresářích.

Použití:
  python sort_by_extension_in_leaf_dirs.py "Z:\Games\Spellcross\...\data" --dry-run
  python sort_by_extension_in_leaf_dirs.py "Z:\Games\Spellcross\...\data"

Volby:
  --all-dirs   třídí v každém adresáři (nejen leaf)
  --dry-run    nic nepřesouvá, jen vypíše, co by udělal
"""

from __future__ import annotations
from pathlib import Path
import argparse
import shutil


def is_leaf_dir(d: Path) -> bool:
    """Leaf = nemá žádné podsložky (ignoruje skryté/systemové podle FS)."""
    try:
        return not any(p.is_dir() for p in d.iterdir())
    except PermissionError:
        return False


def ext_folder_name(p: Path) -> str:
    """
    Vrátí název složky pro danou příponu.
    .PAL -> 'pal', bez přípony -> '_no_ext'
    """
    suf = p.suffix.lower()
    if not suf:
        return "_no_ext"
    return suf.lstrip(".")


def unique_target_path(dst: Path) -> Path:
    """Když existuje cílový soubor, přidá suffix _1, _2, ..."""
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


def sort_dir(d: Path, dry_run: bool) -> tuple[int, int]:
    """
    Vytřídí soubory v adresáři d do podsložek podle přípony.
    Vrací (moved, skipped).
    """
    moved = 0
    skipped = 0

    for f in d.iterdir():
        if not f.is_file():
            continue

        folder = ext_folder_name(f)
        dest_dir = d / folder

        # pokud už je soubor ve správné složce, nic nedělej
        # (typicky nenastane, protože třídíme jen leaf, ale je to pojistka)
        if f.parent.name.lower() == folder.lower():
            skipped += 1
            continue

        dest_dir.mkdir(exist_ok=True)
        dest = unique_target_path(dest_dir / f.name)

        if dry_run:
            print(f"[DRY] {f}  ->  {dest}")
        else:
            shutil.move(str(f), str(dest))
            print(f"[OK ] {f}  ->  {dest}")

        moved += 1

    return moved, skipped


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("root", help="Root složka, ve které se má třídit", type=str)
    ap.add_argument("--all-dirs", action="store_true", help="Třídit v každém adresáři (nejen leaf)")
    ap.add_argument("--dry-run", action="store_true", help="Jen vypsat, nic nepřesouvat")
    args = ap.parse_args()

    root = Path(args.root).expanduser().resolve()
    if not root.exists() or not root.is_dir():
        print(f"ERROR: Root neexistuje nebo není adresář: {root}")
        return 2

    total_moved = 0
    total_skipped = 0
    touched_dirs = 0

    # bottom-up traversal, aby leaf logika dávala smysl a nerozbila se přidáním složek
    for d in sorted(root.rglob("*"), key=lambda p: len(p.parts), reverse=True):
        if not d.is_dir():
            continue

        # přeskoč “příponové” složky, které skript sám vytvoří, pokud bys jel --all-dirs
        if d.name.lower() in {"_no_ext"}:
            continue

        if not args.all_dirs:
            # Jen leaf adresáře (bez podsložek)
            if not is_leaf_dir(d):
                continue

        # Má v adresáři vůbec nějaké soubory?
        try:
            if not any(p.is_file() for p in d.iterdir()):
                continue
        except PermissionError:
            continue

        moved, skipped = sort_dir(d, args.dry_run)
        if moved or skipped:
            touched_dirs += 1
            total_moved += moved
            total_skipped += skipped

    print(f"\nDone. dirs={touched_dirs}, moved={total_moved}, skipped={total_skipped}, dry_run={args.dry_run}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
