# Stage 6.39 - STRUPG physical roster grid fix

The STRUPG.QH rectangles describe semantic/help regions. They are not the
physical geometry of each company slot.

For the restored unit-management screen the physical slot layout is now taken
from the original UNITS.LZ (406x464), which the renderer composites at screen
position (6,8), together with VMU_SLCT.LZ (146x17).

Measured native screen geometry:

- Core roster: first cell (15,11), cell 146x17, row pitch 19, column pitch 152,
  16 rows per column.
- Support roster: first cell (15,320), cell 146x17, row pitch 19, column pitch
  152, 8 rows per column.
- Support capacity therefore matches the 2x8 physical grid (16 cells).

Rendering, selection overlay, HP bar, cooldown indicator and mouse hit testing
use the same geometry. Text remains clipped to the cell interior.

The parallel Engine/Weapon/Armor upgrade mechanic introduced in Stage 6.38 is
unchanged by this correction.
