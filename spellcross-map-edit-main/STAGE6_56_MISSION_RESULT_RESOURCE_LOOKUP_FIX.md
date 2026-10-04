# Stage 6.56 — mission result resource lookup fix

## Root cause

`SpellGraphics::AddRaw()` stores most raw graphics with a loose/extensionless
name when `with_ext == false`. Therefore `M_ACCOMP.LZ`, `M_FAILED.LZ` and
`WM_STAT.LZ` are present in `SpellGraphics` as `M_ACCOMP`, `M_FAILED` and
`WM_STAT`.

The v20-v25 mission result renderer requested the original FS filenames with
the `.LZ` extension. `GetResource()` used exact string matching, returned null,
and `DrawMissionResultOverlay()` returned without drawing anything. The modal
mission-result state nevertheless remained active, so later battlefield clicks
advanced the invisible Debrief/Statistics pages and eventually entered the
strategic level. This exactly explains the observed behaviour.

## Fix

- Mission result renderer now requests the canonical in-memory names.
- `SpellGraphics::GetResource()` also accepts an original filename with an
  extension as a fallback and resolves it to the loose resource name.
- Mission result drawing no longer becomes invisible when a result asset is
  genuinely absent: it renders a visible text/panel fallback and logs a warning.

The normal path still uses the original Spellcross assets:
`M_ACCOMP.LZ`, `M_FAILED.LZ`, and `WM_STAT.LZ`.
