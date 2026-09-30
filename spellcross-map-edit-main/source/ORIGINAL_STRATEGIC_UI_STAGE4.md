# Original Strategic UI – Stage 4

Stage 4 extends the parallel restored strategic renderer without removing the current wxWidgets UI.

## Changes

- Removed the duplicate turn number from the upper status plate. The turn is shown only in the original lower-right `Kolo NN` plate, matching the DOS UI.
- When a newly generated commander arrives, the game now announces: `Nový důstojník přišel do generálního štábu.`
- Added the third restored strategic page: **Unit Management / Řízení jednotek**.
  - built from the original `VMU_FULL.LZ`, `UNITS.LZ`, `VMU_LST2.LZ`, `VMU_LST1.LZ` and common `BIG_MAP.LZ` chrome;
  - reads the same player roster and unit state as the current UI;
  - permanent-unit roster is selectable and scrollable;
  - damaged/cooldown/selected units are visually distinguished;
  - original mode panel supports `Úpravy`, `Nábor`, and `Info`;
  - recruit quality selection supports rookies / veterans / elite;
  - researched tech upgrades and same-category re-arm targets are selectable;
  - cost/time are shown in the original lower panel;
  - `Propustit` and `OK` call the existing unit-management game logic;
  - money/research and the lower-right end-turn plate stay live;
  - restored toolbar switches directly between Map / Hierarchy / Units.
- Temporary/helper-unit area remains intentionally empty until the remake exposes a separate temporary-unit collection; no fake data are invented.

## Architecture

No second copy of strategic game logic was introduced. The restored screen edits `m_playerUnits`, `m_unitStates`, research/upgrades, money and cooldowns through the existing controller methods. The current UI remains available through the `Strategic UI` menu.

## Validation performed here

- `strategic_original_renderer.cpp` compiles cleanly as a standalone translation unit with GCC 14 (`-std=c++17 -Wall -Wextra -pedantic`).
- `RenderUnits()` was executed against the original decoded COMMON assets and produced a 640×480 frame.
- The C++ renderer output was compared pixel-for-pixel with an independent reconstruction of the same original assets: **0 differing pixels** for the static Unit Management screen.

A complete Windows wxWidgets/MSVC build still needs to be performed on the normal project toolchain.
