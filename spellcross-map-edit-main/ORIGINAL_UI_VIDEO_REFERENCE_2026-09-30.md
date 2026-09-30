# Original Spellcross UI video reference - 2026-09-30

Source supplied by the user: `2026-09-30_20h35_27.mp4`.

Capture metadata observed during Stage 6.9 work:
- duration: 59.3 s
- capture: 2560x1380
- frame rate: 30 fps

Useful visual anchors for later manual screen-by-screen restoration:
- ~0-24 s: strategic map and common strategic chrome
- ~6.2-8.0 s: clear end-turn hover enter/leave sequence; the grey arrow panel wipes horizontally over `Kolo 121`
- ~25 s: territory/resources allocation screen visible
- ~30-35 s: research screen visible
- ~40-45 s: unit-management screen visible
- ~50 s: statistics screen visible
- ~55 s: main menu visible

Stage 6.9 asset correlation:
- `ET_PAN.LZ`: native dark 41x36 idle panel texture
- `ET_BTN0.LZ`: native 41x36 grey end-turn hover button with bent left arrow; matches the capture
- `ET_BTN1.LZ`: native 41x36 brighter alternate state; the supplied hover sequence does not establish a separate use for it, so Stage 6.9 does not invent one
- end-turn sprite origin: x=588, y=432 in the logical 640x480 strategic framebuffer

Keep this note as a persistent project-side summary of the uploaded video so later UI revisions do not need to rediscover the same timings and asset geometry.
