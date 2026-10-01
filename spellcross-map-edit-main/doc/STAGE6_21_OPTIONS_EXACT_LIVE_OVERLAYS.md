# Stage 6.21 – Options live-overlay geometry cleanup

This pass replaces the remaining hand-written `+4`, `+9`, `+12` style offsets
on the strategic Options screen with explicit native 640×480 rectangles.

## Authoritative parent regions

Source: `source/spellcross_ui_coordinate_catalog/raw/qh/STROPT.QH`.

- resolution: `234,339,166,76` (inactive/obsolete in remake)
- quick help: `434,336,74,77`
- exit: `327,435,113,41`
- saved positions: `115,25,355,277`
- load column: `20,22,75,280`
- save column: `490,22,75,280`
- gamma: `28,333,178,41`
- music: `28,380,178,41`
- sound: `28,425,178,41`

## Native inner/live geometry

The QH format intentionally describes interaction regions, not every live glyph.
The remaining inner geometry is therefore anchored to the original OPTIONS
artwork/runtime layout, still in native 640×480 coordinates:

- save/load row pitch: `31`
- save/load button row 0: `x=20/490, y=22, w=75, h=28`
- saved-position well row 0: `115,27,355,18`
- slider title bands:
  - gamma `28,333,178,18`
  - music `28,380,178,18`
  - sound `28,425,178,18`
- slider control bands:
  - gamma `28,351,178,23`
  - music `28,398,178,23`
  - sound `28,443,178,23`
- slider groove: `x=47..186`, 10 px thumb, 130 px thumb travel
- Quick Help title: `434,339,74,18`
- Quick Help On: `434,357,74,18`
- Quick Help Off: `434,375,74,18`
- Quick Help selection bracket: `x=436..505`, top `y=354`, On line `y=367`, Off line `y=385`
- visible Exit button: `346,440,71,29` inside the STROPT Exit hit region

## Changes

- Save/Load buttons are now 28 px high and aligned to the native 31 px row cadence.
- Save-slot captions use their own native row wells instead of borrowing the
  Load-column Y coordinate.
- Slider titles are vertically centered in their real 18 px header bands.
- The duplicate procedural +/- glyphs were removed: those glyphs are already
  present in `OPTIONS.LZ`.
- Slider hit-testing now follows the real lower control bands and groove limits.
- Quick Help title, On/Off rows, selection bracket and hit-testing use explicit
  native sub-rectangles.
- Exit is drawn as the actual inner 71×29 live button, while the parent
  `327,435,113,41` STROPT rectangle remains the click target.
- Resolution remains intentionally inactive/empty because tactical resolution
  switching is unsupported in the remake.
- Stage 6.20 Save / Don't save / Cancel application-exit behavior is retained.
