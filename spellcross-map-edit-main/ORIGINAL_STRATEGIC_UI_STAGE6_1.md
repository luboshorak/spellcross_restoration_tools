# Original strategic UI – Stage 6.1 functional fix

This pass fixes the Stage 6 Research / Info / Resources / Statistics integration so the restored 640x480 UI uses the same campaign state as the working strategic backend instead of maintaining placeholder state.

## Research / Info
- Research definitions and titles are loaded from extracted files when present, with direct `COMMON.FS` fallback.
- BRF/INF texts have direct `RESEARCH.FS` fallback, so a clean runtime does not depend on manually extracted temp folders.
- `Time(0)` records are retained for Info but cannot be started as research.
- Research availability uses the actual strategic level, completion state and `ORconnections` prerequisites.
- STOP pauses without destroying progress; OK starts/resumes the browsed project.
- Research progress consumes the per-turn research allocation once (no Stage-6 x2 multiplier and no fake research-bank reset).
- Info visibility uses completed research plus campaign `SetResearchFlag`/unit unlock state.
- Original UI list selection and wheel scrolling operate on the same filtered rows that are rendered.

## Resources
- `DefineStrategicPoints(territory,total,perTurn)` is parsed as the finite strategic-point pool used by the original game.
- Owned territories yield `min(perTurn, remaining)` SB each strategic turn.
- Resource conversion follows the manual/original UI: 1 SB = 1 money, 3 SB = 1 research point.
- The top-right Research value mirrors the current per-turn research allocation, matching the Resources screen.
- The allocation survives save/load through the id=0 resources metadata record.
- Old Stage-6 synthetic 20/20 resource saves are migrated back to the real `LEVEL_XX.DEF` values.
- Removed the old unconditional `+50 money` end-turn placeholder, which previously double-counted the economy.
- Left-click arrows change research allocation by 1; right-click arrows change it by 10, matching the manual.
- Territory labels show the current SB/turn and remaining rounds, and depleted territory pools stop producing.

## Statistics
- The restored Statistics page reads the same cumulative and current-level loss counters as the working statistics backend and the live John Alexander rank/experience/capacity data.

## Validation performed here
- `level.cpp` compiles as C++17.
- `strategic_original_renderer.cpp` compiles as C++17.
- Source diff passes `git diff --check` (no whitespace errors).
- Full wxWidgets/Visual Studio application linking is not available in this Linux container, so the final Windows executable itself is not built here.
