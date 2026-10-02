# Stage 6.34 - first-run game-data setup and cache repair

Stage 6.34 keeps the Stage 6.33 game/UI behavior and changes only how external
Spellcross source data is located and how already-extracted FS cache files are
maintained.

## First-run / broken-config setup

An empty `config.ini` is now a valid starting point. On startup the game checks
individual source files instead of requiring one preconfigured absolute
Spellcross installation directory.

Required sources:

- `COMMON.FS`
- `T11.FS`
- `PUST.FS`
- `DEVAST.FS`
- `UNITS.FSU`
- `TEXTS.FS`
- `INFO.FS`

Optional sources:

- `SAMPLES.FS` - sound effects; Cancel records a persistent skip and the game
  continues in silent-SFX mode.
- `MUSIC.FS` - MIDI music; Cancel records a persistent skip and the game
  continues without music.

Each successful selection is written to `config.ini` immediately. Selecting one
archive also triggers sibling discovery in the same folder and nearby `CD`
folder, so a normal original installation normally needs only one or very few
manual selections.

Existing legacy `[SPELCROS] spell_path` / `spellcd_path` configs are accepted and
migrated into the new per-file `[FILES]` entries automatically. Invalid/missing
per-file paths are requested again on the next startup.

Paths inside the release/runtime tree are stored relatively where possible;
external source files remain absolute.

## Extracted cache integrity

`COMMON.FS` and the three terrain archives still remain the authoritative
sources. Their loose files under `temp` are treated as a persistent cache.

At startup every existing cached member is checked against its archive member.
A matching file is left untouched. A missing, truncated, modified or otherwise
different file is re-extracted individually. The whole cache is no longer
blindly rewritten on every run.

## Compatibility / safety

- Core game archives are still loaded by the same runtime classes; this is not
  a new game-data format.
- The historical directory keys are derived from the selected COMMON/INFO
  locations for older dynamic lookup code.
- A stale absolute `STATE.last_map` from another computer is silently discarded
  rather than producing a startup map error.
- Cancelling the first-run setup is safe even before `SpellData` / `SpellMap`
  objects have been created.
- `strategic_original_renderer.cpp` is unchanged from Stage 6.33.

## New config section

Typical result:

```ini
[FILES]
common_fs = D:\\SPELLCROS\\DATA\\COMMON.FS
t11_fs = D:\\SPELLCROS\\DATA\\T11.FS
pust_fs = D:\\SPELLCROS\\DATA\\PUST.FS
devast_fs = D:\\SPELLCROS\\DATA\\DEVAST.FS
units_fsu = D:\\SPELLCROS\\DATA\\UNITS.FSU
texts_fs = D:\\SPELLCROS\\DATA\\TEXTS.FS
info_fs = D:\\SPELLCROS\\DATA\\CD\\INFO.FS
samples_fs = D:\\SPELLCROS\\DATA\\SAMPLES.FS
music_fs =
music_fs_skip = true
```

The exact paths depend on the user's original Spellcross data location.
