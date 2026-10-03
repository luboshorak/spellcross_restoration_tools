# Stage 6.44 – Original strategic text-state colours

This pass removes the remaining per-screen colour drift in the restored strategic UI.

## Shared semantic palette

All live strategic lists now use one state language:

- **bright neutral** `(218,222,211)` – usable / available item
- **red** `(242,48,40)` – current selection
- **yellow** `(232,232,0)` – category heading or native selected mode
- **green** `(0,242,0)` – already active/installed/queued state
- **dim neutral** `(126,132,118)` – blocked / unavailable item

The values are the same family already used by the restored Map / Research / Info screens and visually match the supplied original captures much more closely than the old Unit/Buy `(150,150,150)` text.

## Behavioural colour checks

Unit modifications no longer colour every researched/suitable row as if it were usable. The renderer evaluates the same conditions as the action path: money, installed state, pending category slot, recruit/re-arm exclusivity, cooldown and tech-work conflicts. Rearm choices are evaluated the same way.

Recruit quality rows are dimmed when that quality cannot currently be ordered.

BUY uses bright neutral for purchasable rows, dim neutral for rows blocked by money/rank capacity, red for the selected row and yellow for category headings.

Research uses red for the browsed selection and green for the currently active project when it is not the current browse selection.

Hierarchy pool selection now uses the shared red selection colour while retaining the native green selection band.
