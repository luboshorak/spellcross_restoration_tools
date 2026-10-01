# Stage 6.20 — strategic Save / Options screen

This pass uses the extracted coordinate catalogue under:

`source/spellcross_ui_coordinate_catalog/`

The authoritative layout source for this screen is `COMMON.FS/STROPT.QH`.
All rectangles below are native 640x480 top-left screen coordinates and come
directly from the catalogue (`qh_rectangles_strategy.csv` / `raw/qh/STROPT.QH`).

| Control | x | y | w | h |
|---|---:|---:|---:|---:|
| Battlefield resolution (removed in remake) | 234 | 339 | 166 | 76 |
| Quick Help | 434 | 336 | 74 | 77 |
| Exit | 327 | 435 | 113 | 41 |
| Saved-position text area | 115 | 25 | 355 | 277 |
| Load column | 20 | 22 | 75 | 280 |
| Save column | 490 | 22 | 75 | 280 |
| Gamma | 28 | 333 | 178 | 41 |
| Music volume | 28 | 380 | 178 | 41 |
| Sound volume | 28 | 425 | 178 | 41 |

## Remake-specific behavior changes

### Battlefield resolution

The original resolution selector is deliberately not exposed. The remake does
not implement the original tactical rendering modes, so drawing a selector
would offer a control that cannot work correctly. The original OPTIONS.LZ
panel remains as inactive artwork and its original STROPT rectangle consumes
clicks without changing application size.

The old launch-time call that resized the main client area according to the
removed selector has also been removed.

### Exit

The native STROPT Exit rectangle remains `327,435,113x41`, but in the remake it
now exits the entire application rather than only closing the strategic frame.

Before quitting, a three-way dialog is shown:

- Yes — choose one of the 9 save slots present on the original Options screen,
  save, then exit.
- No — exit without creating/updating a manual save slot.
- Cancel — remain in the game.

Cancelling the subsequent slot-selection dialog also cancels application exit.

## Save slots

The original Options screen contains nine visible save/load rows. The renderer
and hit-testing are anchored to the exact STROPT load/save/text-area rectangles;
the 31 px row pitch and 27 px active button height come from the OPTIONS.LZ row
artwork inside those catalogue rectangles.
