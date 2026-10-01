# Stage 6.29 - load strategic campaign saves from the main menu

Built on Stage 6.28.

## Problem

The first in-game main menu already allowed selecting `*.json`, but the loader
assumed the strategic JSON had a `LEVEL_XX.DEF` file in the same directory.
Current strategic saves are deliberately stored under:

`save/strategic/level_XX/slot_YY.json`

so that assumption is false. Even when a neighbouring DEF happened to exist,
the old path constructed a StrategicLevelFrame normally and therefore loaded
its autosave instead of the exact JSON selected by the player.

## Fix

- Main-menu `Load game` now loads the exact selected strategic JSON.
- The corresponding `LEVEL_XX.DEF` is resolved in this order:
  1. the `level_def` path embedded in the JSON, if it still exists;
  2. the embedded DEF filename in the configured Spellcross data roots;
  3. the stable `save/strategic/level_XX` directory name;
  4. a neighbouring DEF as compatibility fallback for old development saves.
- Added `StrategicLevelFrame::LoadStrategicStateFromPath()` so an explicitly
  chosen JSON can be applied without changing or copying the save file.
- The Strategic Level is created with `skipAutosave=true`, preventing an
  unrelated autosave from replacing the selected slot.
- The desktop File -> Load strategic game entry uses the same loader.
- Load dialogs start in `save/strategic` when that directory exists.

## Intentionally unchanged

- Strategic save file format.
- Slot save/load inside Strategic Level.
- Tactical `.scsave` loading and campaign-context support.
- Reconstructed Original UI renderer.
- Strategic gameplay/progression logic.
