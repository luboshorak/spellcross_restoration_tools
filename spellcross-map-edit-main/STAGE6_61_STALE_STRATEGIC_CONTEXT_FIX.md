# Stage 6.61 - stale strategic context after standalone/direct mission load

## Symptom
A campaign save from a later strategic level (for example LEVEL_05) could be loaded, then the opening mission M01_01A could be started directly/New Game. After completing M01_01A the rescued ArmyUnit was transferred correctly, but the game returned to the previously loaded LEVEL_05 strategic map instead of progressing to LEVEL_02.

## Root cause
`MainFrame::OpenStrategicAndLoadNext()` treated any non-null `m_strategicLevel` pointer as authoritative. A hidden strategic window can legitimately remain alive while the tactical window is shown. New Game and standalone tactical loads did not always detach an unrelated old strategic frame.

The mission-end enrichment in `OnTimer()` had the same assumption: as long as `m_strategicLevel` existed, it skipped deriving the parent LEVEL_XX.DEF, level outro and `next_level_def` from the mission that had actually just ended.

## Fix
- Added mission-family matching (`M05_06A`, `M05_06B` and `M05_06.DTA` all normalize to `M05_06`).
- A strategic frame is accepted for mission result handling only when it has a valid pending mission and that pending mission belongs to the currently loaded tactical map.
- New Game explicitly discards any previous strategic frame and mission-end presentation state before loading M01_01A.
- Loading a tactical save now discards an unrelated pre-existing strategic frame unless campaign context was actually restored from the save or from a matching legacy strategic autosave.
- Mission-end flow performs the same stale-context guard before deciding whether to infer parent-level outro/transition data.
- `OpenStrategicAndLoadNext()` repeats the check defensively, so alternate result paths cannot resurrect the stale frame.

## Expected opening-campaign flow
`M01_01A` -> LEVEL_01 parent data -> ALEX outro -> derived `LEVEL_02.DEF` -> LEVEL_02 strategic screen.

The Stage 6.60 ArmyUnit/MissionUnit lifetime handling is unchanged: the palisade commando (`ArmyUnit`) remains permanent, while the blue-striped rescued infantry (`MissionUnit`) remain tactical-only.
