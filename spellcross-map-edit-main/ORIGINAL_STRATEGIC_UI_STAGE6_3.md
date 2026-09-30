# Original Strategic UI Stage 6.3 — Research repair

Research was audited against the original COMMON.FS UI help rectangles and the Spellcross manual.

## Native geometry recovered from COMMON.FS
- active research name: 31,8 228x21
- progress indicator: 14,33 262x21
- active BRF text: 19,73 357x130
- browsed research name: 31,236 228x21
- browsed INF text: 19,286 359x130
- available research list: 417,12 136x464
- STOP: 305,227 69x28
- OK: 305,438 69x28

## Mechanics
- Only one project may run at once.
- A running project must be stopped before OK can start another project.
- STOP preserves progress. A stopped project can later be resumed or another project can be selected.
- Progress is advanced only on End Turn and only by the research points produced by Resources.
- Completed research unlocks its results immediately in the same strategic turn.
- Paused projects retain their per-item progress and are marked in the native list.

## Visual fixes
- Restored the missing green/grid research browser instead of leaving the right panel black.
- Added original-style research-list scrollbar and arrow buttons.
- Aligned active title, active BRF, browsed title, browsed INF, STOP and OK to recovered native rectangles.
- Active BRF uses the original green text.
- OK is visibly disabled while a project is running, matching the original screen.
- Browser follows the active project and auto-scrolls the selected row into view.
