# Stage 6.22 — exact origin of 569×464 strategic panels

The full-width strategic panel resources `FACTORY.LZ`, `STATS.LZ` and `OPTIONS.LZ` are all 569×464 pixels.

The previous renderer placed them at `(3,8)`. This was incorrect: it left the right edge at x=572 instead of filling the 575-pixel screen-specific strategic surface.

Registration against the original logical 640×480 captures gives the native placement:

- `FACTORY.LZ`: `(6,8)`
- `STATS.LZ`: `(6,8)`
- `OPTIONS.LZ`: `(6,8)`

At x=6, `6 + 569 = 575`, so the panel terminates exactly at the right edge of the screen-specific area, immediately before the common 65-pixel toolbar/status strip.

This is a translation fix only. The assets remain at their native 569×464 size; no stretching or fractional scaling is applied. Native QH coordinates therefore continue to be interpreted directly in the 640×480 framebuffer.

Both the independent renderer and the legacy/static composition path were changed so they cannot disagree.
