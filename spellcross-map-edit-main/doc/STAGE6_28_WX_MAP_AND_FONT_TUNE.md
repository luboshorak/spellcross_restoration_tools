# Stage 6.28 – clean wx UI map + font tune

Built on top of Stage 6.27.

## What changed

- The clean wx strategic-map page now presents the real rendered strategic map
  as a first-class visual element inside a normal `wxStaticBox` section titled
  **Strategic map**.
- The mission text area below the map is also wrapped into a clean form section
  titled **Mission briefing**.
- Removed the old sliced `VMM_FULL.LZ` under-panel background from the clean wx
  map page so the layout stays consistently Windows-like.
- Increased the minimum map canvas size to keep the map visually prominent.
- Increased the default strategic font size for the clean wx branch.
- Slightly increased button height and sidebar width so the larger type has
  enough room and reads more evenly across pages.

## Intentionally unchanged

- Strategic logic and handlers.
- Reconstructed Original UI path.
- Other non-strategic parts of the application.
