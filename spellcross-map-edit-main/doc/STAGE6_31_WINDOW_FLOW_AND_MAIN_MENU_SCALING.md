# Stage 6.31 – window flow + main menu scaling

Based on Stage 6.30.

## Main menu

- The startup main menu is now a standalone top-level window instead of an owned
  child of the tactical map frame.
- Default size matches the strategic level window: 1390x1050.
- Original 640x480 menu artwork is scaled using the same crisp integer-scaling
  rule as the reconstructed strategic UI, with centred letterboxing as needed.
- Mouse hit-testing and hover rectangles are transformed back to the original
  640x480 logical coordinate system, so resizing does not break menu controls.
- The tactical map frame starts hidden, so no map window is visible behind the
  startup menu.

## Tactical / strategic lifecycle

The application now treats the major game layers as mutually exclusive visible
windows:

- main menu -> tactical map
- tactical map -> strategic level
- strategic level -> tactical map when launching a mission
- closing the current strategic level -> main menu

The tactical `MainFrame` is hidden rather than destroyed while the main menu or
strategic layer is active. This preserves campaign/runtime ownership but removes
all accidental interaction with a stale tactical map.

Loading a strategic save and returning to strategic after a tactical mission now
hide the tactical frame before showing the strategic window.

Closing the startup/strategic-return main menu exits the application instead of
revealing an old tactical map. Closing an in-battle main menu resumes the battle.

## Safety

- Strategic save/gameplay formats unchanged.
- Tactical mission logic unchanged.
- Reconstructed Original UI renderer unchanged byte-for-byte from Stage 6.30.
