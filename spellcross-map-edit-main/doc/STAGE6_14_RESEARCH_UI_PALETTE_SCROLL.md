# Stage 6.14 – Research UI / shared late-screen palette fix

Focused revision of the restored Research screen plus the palette path shared by the later strategic screens.

## Shared palette correction

The later restored pages (Research, Info, Resources, Stats) used `BuildStrategyPalette()` which previously left the `192..255` palette bank coming from `STRATEGY.PAL`.

That differed from the already-correct Map / Hierarchy / Units / Buy composition and caused the right-side BIG_MAP chrome / toolbar shell to render black or with wrong colours.

`BuildStrategyPalette()` now uses the same strategic three-bank model:

- `0..127` = `_SHARED1.PAL`
- `128..191` = `STRATEGY.PAL` page/general bank
- `192..255` = `BIG_MAP.PAL`

This single fix applies to Research, Info, Resources and Stats.

## Research progress tube

`STRRSR.QH` defines the progress indicator at `14,33,262x21`.
The previous restored UI drew a flat opaque rectangle too low in the tube.

The progress fluid is now placed in the inner cavity and alpha-style tinted over `RSRCH_BG`, preserving the original glass highlights instead of painting over them.

## Research text scrolling

Both text windows now have independent mouse-wheel scrolling:

- active research text: `19,73,357x130`
- highlighted research item text: `19,286,359x130`

These coordinates come directly from `STRRSR.QH`.
Changing the highlighted item resets the lower text scroll; starting a new research project resets the active text scroll.

## Verification

`strategic_original_renderer.cpp` was compiled independently with C++17 and exercised directly against the original `COMMON.FS` assets for Research, Info, Resources and Stats. All four 640x480 base framebuffers rendered successfully with the corrected common palette banks.
