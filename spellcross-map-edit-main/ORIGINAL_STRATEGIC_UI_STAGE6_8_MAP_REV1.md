# Original Strategic UI - Stage 6.8 - Strategic map revision 1

This pass intentionally touches only the reconstructed strategic-map screen and the common native strategic toolbar/end-turn interaction.

Changes:
- Restored the real 37x24 strategic toolbar button plates from `BMPAN__N.BTN` / `BMPAN__A.BTN` at the `STRBAR.QH` coordinates (x=590, 31 px pitch). The VM_* glyphs are now drawn on the native plates instead of floating directly over the stone background.
- Added toolbar hover states without changing toolbar navigation logic.
- Replaced hand-drawn Attack/Cancel rectangles with native `BIGMB__*.BTN` sprites. Their draw coordinates were measured against the reference 640x480 frame (421,439 and 496,439); hit rectangles remain the original `STRMAP.QH` regions.
- Removed the extra dark 1 px vertical line previously added beside the map unit-selection panel.
- Added original-UI mouse tracking for native hover states.
- Added animated enemy-territory hatch on hover. The original 2 px / 7 px diagonal pattern now scrolls only under the hovered revealed unconquered territory; other territory states remain static.
- Added native end-turn hover animation using `ET_BTN0.LZ` / `ET_BTN1.LZ`. Idle appearance remains unchanged so the static frame still matches the reference capture.
- No gameplay state, save format, legacy wx UI, hierarchy, units, buy, research, info, resources, statistics or options logic was changed.
