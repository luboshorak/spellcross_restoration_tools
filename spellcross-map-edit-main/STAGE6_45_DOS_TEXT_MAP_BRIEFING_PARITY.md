# Stage 6.45 — DOS text colours, conquered borders, briefing layout

This pass uses the supplied side-by-side DOSBox/restoration captures as the visual authority for the remaining strategic-screen details.

## Strategic text palette

Pixel sampling of the lossless DOSBox side of the comparison captures gives the native foreground colours used by the strategic lists:

- normal/usable text: RGB **199,255,199** (pale green-white)
- selected text: RGB **243,44,44**
- category headings: RGB **255,247,4**
- active/installed/status green: RGB **4,219,4**
- blocked/unavailable text: RGB **150,150,150**

`SpellFont` already renders the original black drop shadow. All list category headings now use the yellow category colour, including `Speciální` on the map and `Motory / Zbraně / Obrana / Nový typ` in unit management.

## Conquered territory borders

The CLK border byte may be attributed to either territory on a shared edge. The map overlay therefore checks the four neighbouring CLK cells as well as the border pixel's own territory id. If the border touches any owned territory it is rendered as the strong near-black outline visible in the DOS original, including edges against unknown territory.

## Strategic-map briefing

`STRMAP.QH` defines the briefing rectangle as `(23,326) 370x130`. The DOS screenshot shows the paragraph top-aligned rather than vertically centred. The restored renderer now starts at y=329, uses the native 14-pixel line pitch and allows up to nine lines. This matches the original vertical density and recovers the missing extra line.
