# Stage 6.35 - original CD / mounted ISO import

Built on Stage 6.34.2.

## Why

A clean original Spellcross CD does not expose the installed `COMMON.FS`,
`T11.FS`, `PUST.FS`, `DEVAST.FS`, `UNITS.FSU`, etc. as loose files. The DOS
installer keeps those files inside `DATA/INSTALL.DTA`, so the per-file startup
wizard could not be completed directly from an untouched CD.

## Verified INSTALL.DTA format

The supplied original Czech installer payload was inspected directly. It uses a
simple, uncompressed container:

- `u32 directory_block_size`
- `u32 directory_count`
- NUL-terminated directory names
- `u32 file_count`
- one 25-byte record per file:
  - `char name[13]`
  - `u32 absolute_data_offset`
  - `u32 data_size`
  - `u32 directory_index`
- raw file payloads at the recorded offsets

The supplied image contains 51 install files. Its `DATA` group contains:

- `COMMON.FS`
- `DEVAST.FS`
- `MUSIC.FS`
- `PUST.FS`
- `RESEARCH.FS`
- `SAMPLES.FS`
- `T11.FS`
- `TEXTS.FS`
- `UNITS.FSU`

`INSTALL.FS` is the installer's own resource archive (buttons, installer font,
Czech installer messages, etc.) and is not needed by the game runtime.
`INSTALUJ.PCX` is likewise installer presentation artwork.

## Startup flow

When required game data is missing, the startup wizard now first offers:

**Import from original CD / mounted ISO**

The user selects either the CD root or its `DATA` folder. The program locates
`INSTALL.DTA`, extracts its `DATA` files to:

`game_data/DATA`

under the runtime root, then runs the normal source auto-discovery so `config.ini`
gets portable per-file paths.

If present directly beside `INSTALL.DTA`, these CD-side archives are copied too:

- `INFO.FS`
- `MOVIE.FS` (also accepts `MOVIES.FS` as an alternate source name)
- `SPEAKER.FS`

If a required archive such as `INFO.FS` is not present on that particular CD
selection, the existing per-file wizard continues and asks for it normally.

## Integrity / repeat import

Files extracted from `INSTALL.DTA` are compared byte-for-byte with an existing
local copy when sizes match. Matching files are left untouched; missing or
modified copies are re-extracted. Writes use a `.partial` file followed by a
rename so a failed read does not leave a half-written archive in place.

## Scope

Only `source/main.cpp` startup/source-discovery code was changed. Strategic,
tactical and reconstructed Original UI rendering/gameplay paths are unchanged.
