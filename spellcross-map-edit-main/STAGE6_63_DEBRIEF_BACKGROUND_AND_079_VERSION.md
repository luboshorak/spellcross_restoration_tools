# Stage 6.63 — Debrief background/placement and 0.7.9 release

## Mission-end debrief

- Keeps the corrected frame geometry from Stage 6.62.
- Moves the result text frame slightly lower (`panelY = 92`) to restore the
  visible gap below the `M_ACCOMP` / `M_FAILED` banner.
- Removes the opaque green fill.
- Samples the current tactical render underneath the debrief and passes it
  through the same `terrain->filter.darkpal` table used by `FormMsgBox`. The
  battlefield therefore remains visible as the original dark/translucent-like
  message background.
- Renders the metallic RAM2 frame and text as a transparent keyed overlay and
  scales the completed overlay as one bitmap, preserving frame alignment.

## Application version

The application release version is bumped from 0.7.1 to 0.7.9 in both
`app_version.h` and the Windows VERSIONINFO resource. Save-format versions are
unchanged.
