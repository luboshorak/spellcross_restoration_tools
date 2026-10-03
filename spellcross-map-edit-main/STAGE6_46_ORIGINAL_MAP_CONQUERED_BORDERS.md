# Stage 6.46 – Original strategic map conquered-territory borders

## Problem

The restored Original strategic-map renderer revealed `LEVEL_XX.LZ` territory
pixels and drew the enemy red hatch, but it never rendered the black borders of
already conquered territories.  An earlier border fix lived in the separate
current-UI overlay path in `form_level.cpp`, so it could not affect the restored
640x480 Original UI.

## Original data

`LEVEL_XX.CLK` encodes territory interior pixels as `ID` and boundary pixels as
`128 + ID`.  Shared edge pixels can be attributed to either adjacent territory.

## Fix

`StrategicOriginalRenderer::RenderStrategicMap()` now performs a final boundary
pass after the red hatch:

- process CLK boundary pixels (`>= 128`),
- normalize the encoded territory id,
- inspect the four neighbouring CLK pixels because a shared edge may belong to
  either side,
- if the encoded territory or any neighbour is `Revealed` (conquered), draw the
  edge pure black,
- draw this pass last so enemy hatch cannot overwrite conquered borders.

This affects only the restored Original strategic map and uses the original CLK
geometry; no synthetic polygon tracing is involved.
