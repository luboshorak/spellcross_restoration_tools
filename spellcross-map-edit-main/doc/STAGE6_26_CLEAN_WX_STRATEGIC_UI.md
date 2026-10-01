# Stage 6.26 - clean wx strategic UI

Presentation-only cleanup of the fallback/current wxWidgets strategic game UI.

## Scope

- Based directly on Stage 6.25.
- Reconstructed Original UI remains the default UI and its renderer is unchanged.
- No campaign, mission, unit, research, resource, save/load or progression logic was rewritten.

## wx UI cleanup

- Removed legacy Spellcross screen slices from the wx branch (`VMM_FULL`, `VMH_FULL`, `VMU_FULL`, `VMB_FULL`, `VMR_FULL`, `VMI_FULL`, `VMF_FULL`, `VMS_FULL`).
- Removed green grid overlays from wx list controls.
- Removed original-game icons from wx navigation buttons; navigation is text-only.
- Replaced custom strategic colours with platform/system colours.
- Replaced the custom Fixedsys-style strategic font with the platform GUI font.
- Standardized navigation buttons to a 32 px minimum height and a fixed 150 px sidebar.
- Standardized button spacing across Map, Buy/Sell and Units views.
- Replaced the special black/hover End Turn control with a plain `End turn` button.
- Aligned Money / Research / Turn into the same two-column status grid on every view.
- Removed DOS-frame spacer geometry from the map briefing area.
- The wx strategic map now shows the map itself without the reconstructed original frame/grid chrome.
- Hierarchy keeps the same slots, drag/drop and assignment logic but uses native borders, neutral connector lines and a normal page-switch button instead of the HIERARCH artwork.
- Damaged/cooldown/selected list-row cues now use system colours instead of hard-coded green/orange/red values.

## Original UI safety

`source/strategic_original_renderer.cpp` is byte-for-byte unchanged from Stage 6.25.
The Original UI paint/input branch remains separate and is still selected by default.
