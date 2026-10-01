# Stage 7.2 – Modern Strategic Designer prototype

Scope: **only the wx-based Strategic Level designer** (`StrategicLevelFrame`).
The reconstructed 640x480 Original UI renderer is intentionally unchanged.
No campaign/game-state data model, mission flow, save format, research logic,
unit logic or strategic rules were rewritten.

## What changed

The old 412:163:65 DOS-derived wx layout is replaced by a modern desktop layout:

- top command header with Money / Research / Turn metrics,
- primary Launch Mission and End Turn actions in the header,
- left navigation rail using the original Spellcross command icons as a visual link,
- large central workspace,
- dedicated right-side contextual panel for commanders/units, research index,
  encyclopedia index and contextual information,
- strategic map shown as the actual map rather than inside the old VMM metal frame,
- modern mission briefing panel,
- clean native lists instead of the green CRT grid overlay,
- modern sans-serif typography in the wx designer,
- hierarchy uses the existing slot/drag/drop logic but draws on a clean modern canvas
  instead of compositing VMH_FULL/HIERARCH art,
- the old VMB/VMU chrome is no longer composited behind the wx Buy/Units controls.

## Intentionally unchanged

- `strategic_original_renderer.cpp`
- Original UI 640x480 composition and scaling path
- Original UI input/hit-testing
- all strategic gameplay/campaign logic and persisted state
- tactical/map editor and every other application window

The frame size remains the Stage 6.25 `1390x1050`, so selecting Original game UI
retains the existing 2x integer presentation when the client area allows it.
