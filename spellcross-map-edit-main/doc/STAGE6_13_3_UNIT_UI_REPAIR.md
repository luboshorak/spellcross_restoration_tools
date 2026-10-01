# Stage 6.13.3 – unit-management repair

Small corrective pass based on Stage 6.13.2 clean.

- Unit portrait rendering now uses the glyph's native size and scales it down into the original preview box. `SpellGraphicItem::Render(56,48)` could return an empty bitmap when the native glyph was larger than that surface.
- Recruit and Info use the same robust portrait path and a dark gridded preview inset.
- The lower action strip no longer flattens/overpaints the native `VMU_LST1.LZ` plate.
- Only the stray black separator scanlines are painted over, then `Propustit`, `Čas/Cena`, and `OK` are drawn on top of the native strip.

No unit-management gameplay rules were changed.
