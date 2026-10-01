# Stage 6.27 - Native wx strategic UI polish

Presentation-only cleanup of the optional wx strategic game UI. The reconstructed Original UI renderer and strategic game logic are not modified.

Changes:
- Uses native/system control colours instead of painting button/list/text backgrounds.
- Removes custom button foreground/background overrides so Windows can draw normal hover/pressed/disabled states.
- Increases the system GUI font slightly for readability; headings are larger/bold.
- Raises the shared button height to 36 px and sidebar width to 170 px.
- Uses themed borders in hierarchy slots.
- Rebuilds the clean Statistics screen with native wxStaticText + wxStaticBox controls.
- Statistics now uses the formerly empty middle pane for Player information and explicitly relayouts after navigation.
- Unit mode tab selection uses bold text instead of black background colouring.
- Removes custom dialog background/foreground colouring in the strategic branch.

Not changed:
- tactical/editor UI outside StrategicLevelFrame
- strategic game state / campaign logic
- save/load format
- Original UI renderer
