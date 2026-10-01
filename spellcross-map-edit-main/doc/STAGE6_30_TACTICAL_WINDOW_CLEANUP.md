# Stage 6.30 - tactical window cleanup

Built directly on Stage 6.29.

## Changes

- Replaced the old tactical main-window title `Spellcross Map Editor` with a
  game-facing title:
  - `Spellcross - <current map/mission stem>` when a map is loaded.
  - `Spellcross` as the fallback.
- The title is refreshed after:
  - normal map loads,
  - strategic mission launches,
  - tactical save loads,
  - new-map creation.
- In game mode the editor toolset `wxRibbonBar` is now hidden completely.
  Previously only its panels were hidden, leaving the empty blue ribbon strip
  below the menu bar.
- Returning to editor mode recreates the ribbon normally.
- `LoadToolsetRibbon()` now also respects game mode, preventing the blue strip
  from reappearing after a map/ribbon reload.

## Intentionally unchanged

- Tactical gameplay logic.
- Strategic gameplay logic.
- Save formats.
- Reconstructed Original Strategic UI renderer.
