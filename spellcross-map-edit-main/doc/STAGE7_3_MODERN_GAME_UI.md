# Stage 7.3 - Modern strategic game UI

Scope: presentation only for the alternative wx strategic game frontend.

## Intent
The wx branch is treated as a normal Spellcross game interface, not as an editor or authoring tool.

## Main screen
- Removed the permanent editor-style left navigation rail.
- Strategic map is the dominant central game surface.
- Added a compact top HUD with Alliance identity, funds, research, turn, Launch Mission and End Turn.
- Moved strategic navigation to a bottom command deck: Map, Command, Units, Reinforce, Research, Intel, Resources, Record.
- Retained original Spellcross menu glyphs as a visual link to the 1997 UI.
- Reworded desktop/editor terminology to in-world strategic-command terminology.
- Mission briefing remains directly below the map.
- The right rail remains a compact game-data area for mission force / research / intel lists.

## Other modern wx screens
- Buy/Sell and Units no longer use the original 640px proportional split values.
- Existing controls, event handlers and game actions are retained.
- Legacy menu icons keep their labels instead of becoming icon-only buttons.

## Safety / Original UI
- `source/strategic_original_renderer.cpp` is byte-identical to Stage 6.25.
- Reconstructed Original UI stays the default (`SetOriginalStrategicUi(true)`).
- The modern branch contains no `BindStrategicScreenSlice()` calls in BuildUI, BuildBuyPage or BuildUnitsPage, so it does not reuse or modify Original UI chrome.
- No campaign, mission, save/load, research, unit, hierarchy, resource or progression logic was intentionally changed.

## Verification performed in this environment
- UTF-8 parse of modified `form_level.cpp`: OK.
- C++ brace/comment/string lexical balance: OK.
- Original renderer SHA-256 matches Stage 6.25 exactly.
- Full MSVC build cannot be run in this Linux container, so Visual Studio remains the authoritative compile test.
