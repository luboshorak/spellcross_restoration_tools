# Original Strategic UI – Stage 6

Stage 6 extends the restored 640×480 Spellcross strategic UI from the Stage 5 Buy screen with the next four native pages:

- **Výzkum / Research** – `VMR_FULL.LZ` + `RSRCH_BG.LZ` + `VMR_LST1.LZ`
- **Info / Encyclopedia** – `VMI_FULL.LZ` + `INFO.LZ`
- **Správa území / Resources** – `VMF_FULL.LZ` + `FACTORY.LZ`
- **Statistiky / Statistics** – `VMS_FULL.LZ` + `STATS.LZ`

All four pages are composited over the common `BIG_MAP.LZ` right-side chrome and use the strategic palette (`STRATEGY.PAL`, with `_SHARED1.PAL` for indices 0..127), matching the palette assembly already used by `SpellData`.

## Live data / controls

The artwork is kept original while dynamic content is shared with the existing remake state:

- Research list, selected research, progress, BRF/INF descriptions, Start/Stop and OK.
- Encyclopedia categorized list, discovered-item filtering in campaign mode, text scrolling.
- Territory-resource map from `LEVEL_XX.CLK`, live owned/depleted/selected territories, remaining resource labels, global research/money allocation arrows.
- Whole-game and current-level loss counters plus John Alexander rank/experience/unit limits.
- Money, research and turn counters remain live on all four screens.
- The original right toolbar now switches directly among Map, Hierarchy, Units, Buy, Research, Info, Resources and Statistics without falling back to the wxWidgets strategic pages. Options remains the existing options dialog.
- The active toolbar button receives the original red corner marker.

## Renderer verification

`strategic_original_renderer.cpp` is intentionally wxWidgets-independent. Its Stage 6 changes compile as C++17 with `g++ -std=c++17 -c strategic_original_renderer.cpp`.
