# Stage 6.55 — Mission end no-selection tick fix

## Symptom
After the final mission text was shown, the tactical map could remain on screen with no result/debrief page. Selecting a unit or attempting a move then caused the mission to advance.

## Root cause
`SpellMap::Tick()` returned early whenever `GetSelectedUnit()` returned null. That return occurred before the common tail that calls:

- `ProcEventsList(event_list)`
- `CheckObjectiveNotifications()`
- `CheckAndTriggerMissionEnd()`

So a completed objective could be fully valid while the mission-end request was never queued. A later click selected a unit, the next tick finally reached the common tail, and the mission suddenly advanced.

This also explains why strategic statistics could still be correct: the gameplay result existed; only the tactical mission-end transition was starved by the no-selection early return.

## Fix
The no-selected-unit path now performs the non-unit end-of-tick work before returning:

1. cleanup dead units,
2. show pending start text if appropriate,
3. process pending events,
4. process objective notifications,
5. trigger mission end.

The rest of the unit movement/attack state machine is still skipped when there is no selected unit.

## Original-data flow
The restored presentation remains driven by the original mission data and graphics:

- `MissionEndOKText(...)` / `MissionEndBadText(...)` from mission DEF,
- `M_ACCOMP.LZ` / `M_FAILED.LZ`,
- `WM_STAT.LZ`,
- then configured end video / return to Strategic Level.

The bug fixed here was not an unknown original-game rule; it was a restoration-engine control-flow bug that prevented that already-restored sequence from being reached.
