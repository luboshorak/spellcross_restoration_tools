# Original strategic UI — Stage 2 (first playable map screen)

This stage keeps the existing wxWidgets strategic interface intact and adds a parallel
**Strategic UI -> Original / restored UI (experimental)** branch.

The restored branch is one logical 640x480 framebuffer. No legacy list controls,
buttons or sizers are layered over it.

## Implemented on the restored Strategic Map

- original BIG_MAP / VMM chrome and level map composition;
- LEVEL_XX / HMLA__XX / LEVEL_XX.CLK territory visibility;
- original-style enemy hatching;
- original Spellcross bitmap font for dynamic text;
- live Money / Research / Turn values;
- live territory briefing text in the lower original frame;
- live player unit list;
- selected / unselected / cooldown unit state;
- mouse-wheel scrolling for long unit rosters;
- `Vyber všechny` / `Odznač všechny`;
- click-to-toggle individual units;
- functional `Útok` routed to the existing mission launch code;
- functional `Zrušit` selection reset;
- functional lower-right end-turn panel;
- map territory hit testing through the original CLK mask;
- timeout/final-territory/counter-attack markers driven by the existing game state;
- original VM_* toolbar glyphs;
- unfinished toolbar pages fall back to the current UI, so the strategic layer remains usable during restoration;
- integer nearest-neighbour scaling of the complete 640x480 framebuffer.

## Still intentionally not restored

Hierarchy, Units, Buy, Research, Info, Resources, Statistics and Options still use the
current interface when opened from the restored toolbar. They are the next restoration
steps after the Strategic Map is validated in-game.

## Safety / rollback

The original/current strategic interface was not removed. Switch back at any time via:

`Strategic UI -> Current UI`

## Validation

`strategic_original_renderer.cpp` is deliberately wx-free and was compiled standalone with
`g++ -std=c++17 -Wall -Wextra -pedantic`. The full Windows/wxWidgets target still needs its
normal MSVC build on the project machine; this environment does not contain wxWidgets/MSVC.
