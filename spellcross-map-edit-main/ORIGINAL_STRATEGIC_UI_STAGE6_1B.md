# Original Strategic UI – Stage 6.1b

Focused repair pass for the restored BUY screen.

## Fixed

- BUY availability now recomputes John Alexander's rank from the authoritative
  action/experience counters before deriving the permanent-unit and commander
  limits. This fixes stale autosaves where the restored screen incorrectly
  considered every free unit slot locked.
- Legacy/current-UI saves that already contain more roster entries than the
  saved rank limit are handled without drawing existing units outside the
  available roster: the restored view advances to the smallest existing rank
  capacity that can contain the current roster.
- The same buy-limit calculation is used by the base renderer, shop row state,
  button state, and the actual `Koupit` click path, so the button cannot be
  visually enabled while the click handler rejects the purchase (or vice versa).
- Strategic toolbar geometry uses the native 31-pixel button pitch. The active
  red wedge is now at native x=591..596 instead of x=581..588, so it stays
  inside the toolbar instead of protruding into the main UI.
- BUY `VMB_LST2.LZ` is placed at y=292. The previous y=291 placement exposed a
  one-pixel black row from `VMB_FULL.LZ` above the lower action strip.

## Verification

- `source/strategic_original_renderer.cpp` compiles as C++17 with GCC.
- Native toolbar marker coordinates were measured against the supplied 640x480
  original screenshots (Map, Hierarchy, Buy, Research, Info, Resources, Stats).
