# Stage 6.13.4 – native unit UI composition repair

- VMU_LST2.LZ restored to y=292, removing the one-pixel black seam.
- VMU_SLCT.LZ now uses the exact Units palette (_SHARED1 + _UNITS + BIG_MAP).
- Core roster origin corrected from STRUPG.QH to x=15, y=11; text and HP geometry follows.
- Unit portrait is drawn directly from SpellGraphicItem indexed pixels and its own palette.
- Propustit / OK use the shared original-style strategic button renderer; VMU_LST1 stays intact.
