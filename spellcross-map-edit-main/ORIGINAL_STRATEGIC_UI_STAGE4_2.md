# Original Strategic UI - Stage 4.2 layout polish

This patch is intentionally limited to geometry/presentation fixes in the restored strategic UI.
Game logic is unchanged.

## Hierarchy
- Keeps the original green `?` assignment placeholders.
- Vertically centers hierarchy-slot text and centers the `?` inside its target slot.
- Restores the right-hand roster scrollbar visuals (up/down buttons, track, thumb).
- Scrollbar buttons and mouse wheel now scroll the permanent-unit pool.

## Unit management
- Rebuilds the missing upper-right dynamic list window instead of leaving it black.
- Adds its grid/frame and native-style scrollbar area.
- Clips option text before the scrollbar so labels do not spill out of their panel.
- Vertically aligns roster text to the original row cells.
- Re-aligns `Propustit` and `OK` to the native lower button wells and updates their hit boxes.

The Current UI branch remains untouched and can still be selected from the Strategic UI menu.
