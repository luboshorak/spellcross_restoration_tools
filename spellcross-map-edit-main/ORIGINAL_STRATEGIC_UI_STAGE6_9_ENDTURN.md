# Original Strategic UI - Stage 6.9 - End-turn animation

Scope: lower-right strategic `Kolo NN` / end-turn control only. The Stage 6.8 strategic-map fixes are otherwise unchanged.

Reference used:
- original-game capture `2026-09-30_20h35_27.mp4`, 59.3 s, 2560x1380, 30 fps
- original `COMMON.FS` resources from the supplied Spellcross installation

Findings and fixes:
- Stage 6.8 interpreted `ET_BTN0.LZ` / `ET_BTN1.LZ` as 36x41. The decompressed payload is 1476 bytes, but the native layout is **41x36**. At 41x36 `ET_BTN0` is the exact grey bent-arrow end-turn button seen in the original capture.
- The native end-turn sprite is positioned at logical **(588, 432)**.
- The original hover effect is not a 2-frame blink. It is a **horizontal left-to-right wipe** of `ET_BTN0` over the idle `Kolo NN` plate. Leaving reverses the wipe right-to-left.
- The supplied capture shows the transition taking about 0.6-0.7 s. The reconstruction uses 4 px every 60 ms, reaching 41 px in about 0.66 s.
- The animation is now shared by all reconstructed strategic pages, matching the original common lower-right control.
- The Stage 6.8 enemy-territory hatch keeps its previous effective ~120 ms animation cadence.

No gameplay state, save format, toolbar behavior, territory logic, or other reconstructed screen rendering was changed.
