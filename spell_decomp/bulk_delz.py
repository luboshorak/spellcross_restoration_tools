#!/usr/bin/env python3
from __future__ import annotations
from pathlib import Path
import subprocess
import sys

# === UPRAV TADY: cesta k delz.exe ===
DELZ_EXE = r"Z:\Games\Spellcross\Spellcross - utility\spell_delz\spell_delz.exe"

def main():
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    lzs = sorted(root.rglob("*.LZ"))
    if not lzs:
        print("No .LZ files found")
        return

    # kontrola, že delz existuje
    if not Path(DELZ_EXE).exists():
        raise FileNotFoundError(f"delz.exe not found: {DELZ_EXE}")

    ok = 0
    skip = 0
    fail = 0

    for lz in lzs:
        out = lz.with_suffix(".bin")
        if out.exists() and out.stat().st_size > 0:
            skip += 1
            continue

        try:
            subprocess.check_call([DELZ_EXE, str(lz), str(out)])
            ok += 1
            print(f"[OK] {lz.name} -> {out.name}")
        except Exception as e:
            fail += 1
            print(f"[FAIL] {lz}: {e}")

    print(f"\nDone. ok={ok} skip={skip} fail={fail}  (root={root})")

if __name__ == "__main__":
    main()
