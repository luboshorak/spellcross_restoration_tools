# Original Strategic UI – Stage 5 (Buy units)

Stage 5 restores the original **Nákup jednotek** screen inside the 640×480 Original UI branch. The page no longer jumps back to the wxWidgets/current strategic interface when the fourth toolbar icon is pressed.

## Native resources / geometry

The renderer composites the original COMMON.FS assets directly:

- `BIG_MAP.LZ` – common 640×480 strategic chrome
- `VMB_FULL.LZ` – Buy-screen right toolbar/status shell
- `BUY.LZ` at `(6,8)` – 32 permanent-unit slots + 14 commander slots
- `VMB_LST2.LZ` at `(334,291)` – lower unit-information panel
- `VMB_LST1.LZ` at `(412,434)` – time/cost/buy strip
- `VMB_DIS.LZ` – 146×17 locked permanent-unit slot overlay
- `VMB_DIS2.LZ` – 132×16 locked commander slot overlay

Permanent slots use two columns of 16 rows (`x=16/168`, `y=10 + row*19`). Commander slots use two columns of 7 rows (`x=21/174`, `y=331 + row*19`). Locked slots are derived from the current John Alexander rank (`HODNOSTI.DEF` via the existing rank table).

## Live data and behaviour

- owned permanent units and their condition/cooldown are drawn into the original left grid;
- owned commanders are drawn into the lower-left grid;
- the upper-right purchase list is generated from the researched/available Alliance units, grouped into the original-style categories and followed by temporary commander offers;
- mouse wheel and the native scrollbar buttons scroll the purchase list;
- selecting a unit displays its original combat-stat block and portrait in the lower-right panel;
- affordability and rank capacity dim unavailable choices;
- `Koupit` purchases the selected permanent unit/commander and updates the same strategic state used by Current UI;
- newly bought permanent units receive the original two-turn availability delay;
- money/research/turn and the common End Turn plate remain live;
- toolbar pages 1–4 (map, hierarchy, unit management, buy units) now remain entirely in Original UI.

The implementation intentionally keeps all dynamic content in the single restored 640×480 framebuffer; no wx child controls are overlaid on this screen.
