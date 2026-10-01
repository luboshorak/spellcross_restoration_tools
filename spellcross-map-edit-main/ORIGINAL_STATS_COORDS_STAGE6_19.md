# Stage 6.19 – Statistics restored from original game data

This pass deliberately does **not** use a screenshot as the source of layout coordinates.

## 1. COMMON.FS / STRSTAT.QH

Extracted directly from the original game's `DATA/COMMON.FS`:

```text
;//////////Strategic INFO/////////
;
132,38,362,146
Overall Game statistics
132,193,362,146
Current level statistics
132,344,256,102
Player information
```

These three rectangles are the authoritative native 640x480 areas used by the
Statistics screen.

## 2. STATS.LZ cell geometry

The original decompressed `STATS.LZ` is 569x464 and is composed at screen
coordinate `(3,8)`. Its actual frame/cell edges were read directly from the
asset pixels. The live text overlay is therefore centered/aligned to those
native cells rather than to guessed screenshot coordinates.

Important screen-space cell edges used by Stage 6.19:

- table columns: x = 128, 217, 352, 493
- upper title band: y = 33..56
- upper column header: y = 56..80
- upper data rows: y = 80..104, 104..130, 130..156, 156..180
- lower title band: y = 188..211
- lower column header: y = 211..235
- lower data rows: y = 235..259, 259..285, 285..311, 311..335

## 3. Pink/garbled placeholders

`_STATS.PAL` in the original archive is only the two-byte file `0D 0A`; it is
not a 64-colour page palette and is not used as one.

The actual cause of the pink garbage is in `STATS.LZ`: palette indices
128..191 are used as dynamic-text placeholder/mask pixels. Mapping those
indices opaquely through `STRATEGY.PAL` makes them pink. In the original final
screen these pixels do not survive as visible artwork; the underlying
`VMS_FULL.LZ` panel remains visible and live/localized text is drawn on top.

Stage 6.19 therefore composites `STATS.LZ` while skipping indices 128..191.
No hand-painted cleanup rectangles are used.
