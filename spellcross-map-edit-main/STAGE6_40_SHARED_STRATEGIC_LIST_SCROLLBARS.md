# Stage 6.40 – Shared strategic list frames and scrollbars

This pass removes the per-screen reconstructed list/scrollbar variants and makes the restored strategic screens use one shared rendering and arrow-hit-test implementation.

## Visual source

The strategic MAP list was used as the visual reference because its compact DOS-style scrollbar was already the closest match to the original UI: an 18 px metal channel, dark green track, 12 px proportional thumb, and literal `^` / `v` arrows.

## Shared implementation

`source/forms/form_level.cpp` now contains:

- `OriginalDrawStrategicListGrid(...)` – one thin frame and one CRT/grid treatment for restored list windows. The previous extra shadow/top/bottom frame that produced conspicuous dark/thick borders was removed.
- `OriginalStrategicScrollbarGeometry` – shared x/y/height/button geometry and arrow hit testing.
- `OriginalScrollbarArrowDelta(...)` – common up/down direction logic.
- `OriginalDrawStrategicScrollbar(...)` – common channel, track, arrows and proportional thumb.

The arrow glyphs are drawn explicitly as `^` and `v`; the old procedural triangle routine built the two triangles in the opposite visual direction.

## Screens migrated

The common list frame / scrollbar is used by:

- Strategic map mission-unit list
- Hierarchy unit pool
- Unit management upgrade/re-arm list
- Buy/recruit list
- Research browser list
- Complex information browser list

Screen-specific list heights remain unchanged; only the style and control logic are shared.

## Interaction

- Mouse-wheel scrolling remains available over every list.
- Up/down arrows now use the same hit-test geometry and direction.
- The MAP and Unit Management lists now also respond to scrollbar-arrow clicks, so the visible scrollbar is functional everywhere instead of being decorative on some screens.

