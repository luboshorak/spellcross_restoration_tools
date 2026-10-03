# Stage 6.43 — native strategic list chrome and buttons

This pass removes the remaining reconstructed list frames/scrollbars and generic strategic action buttons.

## Original COMMON.FS assets restored

The right-hand strategic list is not a plain runtime-painted green rectangle. Spellcross ships the complete normal-state CRT/list chrome:

- `SB_BG01.LZ` — 163x429, strategic map list
- `SB_BG02.LZ` — 163x472, hierarchy / research / information tall list
- `SB_BG03.LZ` — 163x287, unit management / upgrades list
- `SB_BG04.LZ` — 163x287, unit purchase list
- `SB_BAR01.LZ` — 16x373, map scrollbar shaft
- `SB_BAR02.LZ` — 16x417, tall-list scrollbar shaft
- `SB_BAR03.LZ` — 16x231, short-list scrollbar shaft

All four list backgrounds are placed at native screen coordinate `(412,5)`. The scrollbar shaft is placed at `(556,33)`. This was measured directly against supplied 640x480 original captures; `SB_BG04` + `SB_BAR03` matches the BUY screenshot pixel-for-pixel apart from lossy screenshot compression/dynamic text.

The normal up/down arrow wells are already present in `SB_BG*`. `SB_UP.LZ` and `SB_DOWN.LZ` remain available as alternate state graphics; the normal renderer does not procedurally draw arrows anymore.

The controller hit rectangles were changed to the real 22x28 arrow buttons at x=553 and y=5 / bottom of the corresponding `SB_BG*` asset.

## Original action buttons restored

`BIGMB__N/A/D/P.BTN` is the original 70x28 green strategic action button. Dynamic labels are still rendered at runtime, but the button frame/grid itself now comes from COMMON.FS.

Native BIGMB graphics are now used for:

- strategic map Attack / Cancel (already partially restored; procedural fallback removed)
- hierarchy page selector
- unit management Disband / OK
- unit purchase Buy
- research STOP / OK
- information Down / Up
- options Load / Save rows
- options Exit

The old procedural green-grid button painter is no longer used for those controls.

## Architectural rule

Static Spellcross chrome belongs in `StrategicOriginalRenderer` and is composited from original assets before live text/data. `form_level.cpp` only overlays dynamic content and performs hit testing. This prevents later restoration passes from accidentally painting over original metal borders or inventing new scrollbar/button artwork.
