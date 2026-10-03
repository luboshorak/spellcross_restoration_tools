# Stage 6.50 - Tactical mission load: stale cache + missing EventText

## Symptom
Launching an attack could abort while parsing a tactical DEF with an error such as:

`Text 'u0310_01' in command 'EventText(u0310_01)' not found in loaded resources!`

## Root causes

1. `temp/COMMON` was maintained as an append-only cache. Existing members were verified/repaired, but files which no longer existed in the currently loaded `COMMON.FS` were never removed. Switching between data versions/languages could therefore leave stale mission DEF files available to the strategic mission resolver.
2. `SpellMapEvent` treated a missing `EventText` resource as fatal even though `EventText` is presentation-only. A missing/mismatched language text could therefore prevent an otherwise valid tactical mission from loading.

The supplied original English `TEXTS.FS` contains `U0310_00` but not `U0310_01`. A validation pass over the active original mission DEF files found no unresolved `EventText(...)` references, supporting the stale/mismatched DEF diagnosis for the reported case.

## Fix

- `FSarchive::DumpToFolder()` now supports `prune_stale`.
- COMMON and terrain cache exports use `prune_stale=true`, making `temp/<archive>` a mirror of the currently loaded archive instead of an accumulating overlay.
- Missing `EventText` records no longer abort tactical map loading. The text message is skipped, while the event's gameplay commands continue to load and execute.

This does not hide structural map errors: malformed command arguments, invalid unit types/positions and other gameplay-critical failures remain fatal.
