# Stage 6.33 – tactical focus, ESC overlay priority, retreat

Based on Stage 6.32.

## 1. Strategic attack opens on the deployed Alliance force

The strategic launcher now remembers the first successfully deployed player
company and selects + scrolls to it after injection into the tactical map.

`MissionStartEvent()` was also corrected so it:
- preserves an already selected living Alliance unit,
- only auto-selects a newly spawned MissionStart unit when that unit belongs to
  the Alliance,
- never replaces the player's selection with an enemy simply because the enemy
  happens to be the first event unit.

This fixes both the initial camera position and the surprising enemy selection.

## 2. ESC closes tactical HUD overlays before opening the main menu

The MainFrame CHAR_HOOK now gives these tactical overlays priority:
- minimap,
- unit list,
- tactical map options.

ESC queues the normal close event for the active overlay and stops there. The
main game menu opens only when no such overlay is active. Existing message,
video and unit-action forms retain their existing close/key flow.

## 3. Retreat button

The final bottom-right HUD button now has a real `HUD_ACTION_RETREAT` action.
The behavior follows the original BATTLES.QH description:

> Retreat. You will lose all units that are not on the starting cross squares

On click:
1. The game determines all living Alliance units that are NOT standing on a
   map `START` square.
2. A confirmation dialog lists the exact unit types (grouped with counts) that
   will be lost.
3. Choosing No cancels with no state changes.
4. Choosing Yes marks those off-start units dead, leaves units on START squares
   alive, and submits a failed mission result through the existing mission-end
   pipeline.
5. Strategic battle-result synchronisation therefore removes exactly the lost
   strategic UIDs/companies and preserves the units that successfully retreated.

If a malformed mission has no START squares, retreat is blocked rather than
silently destroying the entire army.

## Scope / safety

Only these sources were changed:
- `source/main.cpp`
- `source/main.h`
- `source/map.cpp`
- `source/map.h`

The reconstructed strategic Original UI renderer and strategic result logic are
unchanged.
