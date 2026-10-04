# Stage 6.64 — Mission-earned unit latch

## Symptom

The Commando rescued in `M01_01A` is declared as `ArmyUnit` in the original
mission data and therefore belongs to the permanent strategic army.  The
restoration could nevertheless lose it on the transition to `LEVEL_02`.

## Root cause

The mission-end UI froze tactical `Tick()` only while the result overlay itself
was visible.  The opening mission skips the separate statistics page and starts
the Alexander outro immediately after the debrief.  At that point the overlay
was hidden but the tactical map was still running behind the video.  `M01_01A`
finishes when Alexander reaches the escape square, so enemies can still be alive.
They could continue moving/firing during the outro and kill the rescued Commando
after the mission had already been declared successful.

The standalone transition then scanned the *live* map only after the outro and
therefore sometimes found no surviving `ArmyUnit`.

## Fix

- freeze tactical simulation for the complete mission-end flow, including
  debrief, statistics and outro video;
- snapshot surviving mission-created `ArmyUnit` companies on the exact tick when
  `MissionEndRequest` is consumed;
- use that snapshot when creating the next strategic level instead of rescanning
  the battlefield after the outro;
- remove the early return in strategic battle-result collection so missions
  which deploy no existing roster company can still award `ArmyUnit` or
  `VoluntUnit` survivors.

`MissionUnit` and `SpecUnit` remain tactical-only and are not promoted to the
permanent strategic roster.
