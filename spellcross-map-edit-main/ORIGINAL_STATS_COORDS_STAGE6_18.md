# Stage 6.18 – original Statistics screen coordinates

Source used for this pass:

1. Original screenshot `statistiky.png` reduced back to logical 640×480.
2. Visual comparison against the restored renderer output.
3. Static `STATS.LZ` composition already restored by `StrategicOriginalRenderer::RenderStats()`.

## Measured layout used by the overlay

These are the logical 640×480 overlay coordinates used for the restored
Statistics screen in Stage 6.18.

- Upper title caption: `x=143, y=37, w=325`
- Upper column captions:
  - Alliance: `x=213, y=60, w=139`
  - Other Side: `x=349, y=60, w=140`
- Upper row labels/values:
  - first row y = `83`, then `+25` per row
  - row label x = `134`, w = `82`
  - alliance value x = `213`, w = `139`
  - enemy value x = `349`, w = `140`

- Lower title caption: `x=143, y=192, w=325`
- Lower column captions:
  - Alliance: `x=213, y=215, w=139`
  - Other Side: `x=349, y=215, w=140`
- Lower row labels/values:
  - first row y = `233`, then `+25` per row
  - row label x = `134`, w = `82`
  - alliance value x = `213`, w = `139`
  - enemy value x = `349`, w = `140`

- Player-info box:
  - player line: `x=142, y=359, w=250`
  - `Hodnost:` label `x=155, y=380`; value `x=224, y=380`
  - `Zkušenost:` label `x=155, y=398`; value `x=232, y=398`
  - `Max. počet stálých jednotek:` label `x=155, y=416`; value `x=359, y=416`
  - `Max. počet velitelů:` label `x=155, y=434`; value `x=331, y=434`

## Pink placeholder cleanup

`STATS.LZ` leaves several title/header interiors visually broken in the restored
frame (pink/garbled speckling visible in the user screenshot). Stage 6.18
repaints these panel interiors before captions are drawn:

- upper title band
- upper alliance header
- upper enemy header
- lower title band
- lower alliance header
- lower enemy header

The repaint uses a dark green fill plus a subtle internal grid so the resulting
look matches the original statistics screenshot much more closely.
