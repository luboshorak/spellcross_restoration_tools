# Stage 6.23 – full-width live-overlay registration

Stage 6.22 corrected the three 569x464 strategic artwork layers to their real
screen origin `(6,8)`:

- `FACTORY.LZ`
- `STATS.LZ`
- `OPTIONS.LZ`

This is now independently verified by direct pixel registration against the
native 640x480 original-game captures.  Testing candidate offsets around the
expected origin gives the minimum image error at `(6,8)` for all three panels.

The Stage 6.22 regression was elsewhere: Resources and part of Statistics still
contained live overlay coordinates measured when those artwork layers were
incorrectly composed at `(3,8)`.  Moving only the artwork therefore separated
live data/hit areas from their frames by three pixels.

Stage 6.23 keeps the correct `(6,8)` artwork origin and migrates only the old
asset-relative overlays by `+3 px` in X.

## Resources

Asset-relative geometry moved by +3 px:

- map live layer / CLK origin: `96 -> 99`
- yield cells: `85 -> 88`
- total-pool visual box/text origin: `259 -> 262`
- research box/text: `164 -> 167`
- money box/text: `339 -> 342`
- slider interior: `248 -> 251`
- slider placeholder recolour region: `220..343 -> 223..346`
- decrement/increment arrow hit regions: `222 -> 225`, `316 -> 319`

The same changes are applied to both LMB and RMB resource allocation paths.

`STRRES.QH` remains authoritative for the absolute native 640x480 control
regions; the +3 correction is only for geometry previously derived from the
misregistered `FACTORY.LZ` artwork.

## Statistics

The table cell edges documented as being read from `STATS.LZ` after the old
`(3,8)` composition are shifted +3 px:

- `128 -> 131`
- `217 -> 220`
- `352 -> 355`
- `493 -> 496`
- decorative title spans `149..472 -> 152..475`

The Player information block remains at the absolute `STRSTAT.QH` rectangle
`132,344,256,102`; it is not shifted.

## Options

No rollback is applied.  `OPTIONS.LZ` is correctly registered at `(6,8)`, and
Stage 6.21/6.22 Options live controls already use absolute STROPT.QH / verified
runtime coordinates rather than the old `(3,8)` asset-relative coordinate set.
