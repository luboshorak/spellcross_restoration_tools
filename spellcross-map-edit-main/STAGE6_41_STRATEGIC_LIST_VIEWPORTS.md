# Stage 6.41 - strategic list viewports and clipping

This pass removes the remaining screen-specific assumptions from restored strategic list windows.

## Shared behaviour

- Every dynamic strategic list uses `OriginalDrawStrategicListGrid()` for the complete CRT backing surface.
- Every list scrollbar uses `OriginalDrawStrategicScrollbar()` / `OriginalScrollbarArrowDelta()`.
- List row capacity is now derived by `OriginalStrategicListVisibleRows()` from the real viewport, font height and row pitch instead of hard-coded row counts.
- `OriginalDrawSpellTextClipped()` clips both the glyph and its black shadow to the list viewport. A last row can no longer paint into the neighbouring lower panel/chrome.
- Mouse hit testing and mouse-wheel maximum scroll use the same calculated visible-row count as rendering, preventing renderer/input drift.

## Screens covered

- strategic map unit list
- hierarchy pool
- unit management upgrade/re-arm list
- buy/recruit list
- research browser
- complex information browser

The fixed native company grids on the left side of BUY/UNITS are intentionally not treated as scrolling list windows.
