# Stage 6.13.2 clean – unit-management micro-fix

Based strictly on Stage 6.13.1. No gameplay changes and no lower-panel/footer redesign.

Changes:
- selected roster row now uses the original `VMU_SLCT.LZ` overlay; its native gap leaves the HP bar visible,
- active mode marker uses only edge/corner brackets inside the original `STRUPG.QH` hit strip and no longer crosses the label,
- Recruit mode now renders the selected unit portrait in the same original VMU_LST2 location used by Info,
- existing cooldown/unavailability text from 6.13.1 is preserved unchanged.
