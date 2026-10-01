# Original Strategic UI - Stage 6.15 - Complex info grid/buttons

Scope: restored `Komplexní informace` strategic screen and the shared research/info list backing.

Reference used:
- original `info.png` capture supplied with the restoration project
- original `research.webp` capture supplied with the restoration project
- original `COMMON.FS` resources (`VMI_FULL.LZ`, `INFO.LZ`, `VMR_FULL.LZ`, `RSRCH_BG.LZ`)

Findings and fixes:
- `VMI_FULL.LZ` provides the metallic screen silhouette but does **not** contain the live green right-hand browser surface. As on the Research screen, the list backing has to be drawn dynamically.
- Research and Complex info now share one `OriginalDrawStrategicListGrid()` helper, so both screens use the same CRT-green background, 18 px grid cadence and list frame instead of maintaining two independent approximations.
- The Info right-hand list now also gets the native-style upper/lower scrollbar controls and track, with matching click handling in addition to the existing mouse-wheel scrolling.
- The lower `Dolů` / `Nahoru` controls are no longer bare text painted over the metal recess. They are rendered as actual green grid buttons using the same restored action-button style as the other strategic controls.
- `Dolů` / `Nahoru` text is vertically centred in the live button face. This replaces the previous fixed `y=444` baseline that placed the labels visibly too high.

No research/gameplay rules, save format, information filtering, toolbar behaviour, strategic-point logic or other strategic screens were changed.
