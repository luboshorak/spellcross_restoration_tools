# Original Strategic UI Stage 6.5 - mission flow repair

This pass repairs campaign mission sequencing and strategic-map mission markers.

## Verified against original COMMON.FS

The original LEVEL_XX.DEF scripts use:

- `End(n)` for the final territory shown with `LASTTERT.ICO` (crossed swords),
- `Mission(...){ Time(n) ... }` for time-limited mission variants,
- `EndOKMission(next)` / `EndBadMission(next)` for chained variants,
- `NextLevel(...)` at level scope for campaign progression.

Important original examples include chained final territories `M07_14A -> M07_14B` and
`M10_03A -> M10_03B`. A successful A mission must therefore not immediately conquer the
territory or advance to the next strategic level.

## Fixed

- Removed premature `EndOKMission` progression from `OnLaunch()`. Mission state now changes only
  after a tactical result is known.
- A territory is conquered only when the successful mission chain has no `EndOKMission` successor.
- A final territory advances the strategic level only after its final mission stage is actually complete.
- The crossed-swords marker consequently remains visible while a chained final territory still has a
  mission to play.
- Time limits are resolved from the **current mission variant**, not from a one-time territory copy.
  This restores timers introduced by later B/C variants (for example `M03_05B`).
- Switching to an OK/BAD successor clears/restarts the timer according to that successor's own
  `Time(...)` value.
- Timed mission deadlines, triggered level events, activated relative events and counter-attack state
  are now persisted in strategic saves. Loading no longer grants a fresh timeout window.
- Existing old saves remain loadable. Since old saves did not contain absolute timeout deadlines,
  their first load can only initialize a deadline from the current mission/turn; all subsequent saves
  preserve it exactly.
