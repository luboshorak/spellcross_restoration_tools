# Stage 6.46 – Native VM toolbar palette fix

The strategic toolbar glyphs are original `VM_*.ICO` resources. Their sparse
ICO pixels use palette index 196 for the light foreground and encoded colour 0,
which `SpellGraphics::AddICO()` maps to palette index 254 for the solid shadow.

The strategic `SpellData::strategy_pal` previously assembled only the shared
0..127 bank and left 192..255 from `STRATEGY.PAL`. This made the original VM
icons render blue/grey.

The DOS strategic palette composition is now used consistently:

- 0..127 = `_SHARED1.PAL`
- 128..191 = `STRATEGY.PAL`
- 192..255 = `BIG_MAP.PAL`

For the VM glyphs this restores the native colours from `BIG_MAP.PAL`:
index 196 = `(231,222,222)` and index 254 = `(0,0,0)`, i.e. the light/white
symbol with its black shadow seen in the original game.
