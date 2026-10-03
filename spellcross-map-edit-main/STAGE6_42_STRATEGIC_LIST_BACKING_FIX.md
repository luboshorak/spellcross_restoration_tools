# Stage 6.42 – Strategic list backing/gutter fix

The black strips around restored strategic lists were not list borders. They were transparent/background pixels from the original base screen left uncovered by the reconstructed live CRT surface.

This pass makes the right-hand strategic list renderer paint one continuous CRT backing from x=414 through x=553, with the shared scrollbar immediately adjacent at x=554 (22 px wide). Screen-specific content viewports remain based on the original QH coordinates:

- STRMAP: 418,8,135x420
- STRHIER: 416,10,137x467
- STRBUY / STRUPG: list region begins at 416,7; shared backing covers the complete live CRT area
- STRRSR: 417,12,136x464
- STRINFO: 417,7,136x467

The common backing deliberately has no synthetic black/shadow frame. The original metal surround is retained from the COMMON.FS screen artwork; only the live green CRT area and scrollbar are reconstructed.

Scrollbar drawing and hit-testing now share x=554 and width=22 so no uncovered gutter remains between content and the scrollbar.
