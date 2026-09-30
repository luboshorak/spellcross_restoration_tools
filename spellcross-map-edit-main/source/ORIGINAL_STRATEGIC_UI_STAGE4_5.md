# Original Strategic UI – Stage 4.5 (unit-management pixel alignment)

This pass is based on a direct comparison with a native 640x480 capture from the DOS game.
It intentionally changes presentation geometry only; game logic is unchanged.

## Corrected native geometry

- `UNITS.LZ`: `(6,8)`, unchanged and verified.
- `VMU_LST2.LZ`: `(334,291)` (Stage 4.4 used y=292).
- `VMU_LST1.LZ`: `(421,434)` and is now also composited by the in-game renderer.
- permanent roster first live row: y=10, 19 px pitch.
- roster text origins: x=31 / x=183.
- roster status bars: x=38 / x=190.
- selection frame: native 146 px width (`x=16..161`).

## Other alignment fixes

- original mode labels/brackets shifted 2 px right to match the DOS capture;
- option-list item text shifted into the native list-cell inset;
- lower unit title and cost/time text aligned to the native plates;
- Disband/OK controls use the original high-set DOS text placement instead of generic vertical centering;
- hit-testing follows the corrected roster and button geometry.

The standalone renderer and the live strategic compositor now use the same VMU layer coordinates.
