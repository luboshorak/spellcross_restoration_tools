# Stage 6.13.2 – unit management visual fixes

Focused follow-up pass for the restored **Units Management** screen.

## Fixes
- moved the roster HP gauge onto its own lower scanline so the selected-unit frame no longer hides it,
- tightened the active **Mód / Úpravy / Nábor / Info** marker so it stays inside the mode menu and no longer collides with the text,
- restored the **unit portrait/preview** in the lower panel for recruit mode as well (and kept a shared rendering path for unit detail),
- made the lower panel consistently show `Typ`, `Stav`, `Úroveň`, and `Zkušenost`,
- added availability/status note under the panel content:
  - `Nedostupná: X kol` for cooldown,
  - `Pomocná jednotka pouze pro tento level` for temporary companies.

## Scope
Visual/layout fixes only for the unit-management screen. No gameplay rule changes.
