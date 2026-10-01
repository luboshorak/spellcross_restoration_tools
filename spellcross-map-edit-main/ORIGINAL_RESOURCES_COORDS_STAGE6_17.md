# Original Resources screen coordinate verification (Stage 6.17)

Reference sources used for this correction:

1. Original game `COMMON.FS` member `STRRES.QH`:

```
89,336,401,21   Each areas resources in this turn
167,408,59,38   Total resources allocated to research this turn
230,425,111,16  Money/research resource allocation slider
342,408,59,38   Total resources converted to cash this turn
266,381,50,27   Total pool of resources to use this turn
101,21,376,256  Map of all your current lands ...
```

2. User-supplied original-game screenshot `resources.png`, normalized back to the native 640x480 framebuffer.
3. Decoded original `FACTORY.LZ` from `COMMON.FS`.

Pixel geometry verified in the renderer's current `FACTORY.LZ` composition:

- resource yield row: borders x=85..486, 16 cells, 25 px pitch, y=335..356
- total-resource box: x=259..312, y=376..406
- research box: x=164..223, y=406..446
- money box: x=339..398, y=406..446
- centre slider placeholder: x=248..314, y=422..437 (67x16)

Text origins used after comparing FONT_001.FNT glyph rows with the original native screenshot:

- resource values: y=339, centred in each 25px cell
- total pool: y=384, centred in the 54px total box
- research/money captions: y=410
- research/money values: y=430

Important: palette-index 131 in the FACTORY allocation control decodes as pink RGB `(219,100,129)` in the reconstructed palette. In the original running game those placeholder pixels are recoloured green. The original screenshot shows the unused slider background as approximately `(4,134,4)` and the allocated fill as `(4,219,4)`. Stage 6.17 therefore recolours the entire placeholder control region before drawing the allocation fill, rather than covering only a smaller centre rectangle.
