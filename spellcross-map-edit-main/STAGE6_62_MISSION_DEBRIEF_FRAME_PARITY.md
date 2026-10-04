# Stage 6.62 — Mission debrief frame parity

The tactical post-mission debrief text box should appear directly under the
`M_ACCOMP` / `M_FAILED` banner, matching the original DOS presentation.

## Problem

The v31 restoration rendered the debrief inside a dynamically sized frame near
the lower part of the battlefield. Because each frame segment was scaled and
placed independently, widescreen scaling could also introduce visible drift in
the metallic border.

## Fix

- use a fixed native debrief anchor (`x=116`, `y=78`) under the banner,
- keep the original RAM2 frame graphics,
- compose the whole debrief panel at native 640x480 resolution,
- scale the finished panel as one bitmap instead of scaling every frame piece
  separately.

This keeps the briefing box visually aligned with the original Spellcross
mission-end layout while still allowing the text area height to grow when a
particular debrief needs more wrapped lines.
