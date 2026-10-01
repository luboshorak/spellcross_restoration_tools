# Stage 7.0 — Modern Strategic Designer prototype

Scope is deliberately narrow: only the existing wxWidgets **Strategic Level**
working UI is restyled. The reconstructed 640x480 Original UI, tactical editor,
main application and all strategic gameplay/campaign models remain unchanged.

## Design direction

- Graphite/charcoal work surfaces replace the dominant bright green wx skin.
- The original strategic artwork is still used as a visual reference, but is
  desaturated/tinted into a subtle graphite/olive texture in the wx designer.
- The actual strategic map remains in colour.
- Original Spellcross command icons are retained in the command rail, now with
  readable text labels beside them.
- The right rail is wider and intentionally desktop-oriented instead of copying
  the original 65 px DOS control strip.
- Typography uses Segoe UI on Windows (generic sans-serif elsewhere) instead of
  the pixel font in the wx designer. The reconstructed Original UI still uses
  the authentic Spellcross font renderer.
- Mission briefing is laid out as a normal modern content pane rather than being
  forced into the original DOS frame spacers.
- Existing list/text controls use a slightly elevated dark surface for contrast.

## What was intentionally NOT changed

- strategic/campaign state and save format
- territory logic, mission flow and mission launch
- research/resources/economy logic
- unit/commander/hierarchy logic
- buy/sell or unit-management logic
- event handlers and control IDs
- reconstructed Original UI renderer and coordinates
- any non-strategic editor UI

The menu entry formerly called `Current / wx UI` is now `Modern designer`.
`Original game UI` remains the second presentation of the same strategic state.
