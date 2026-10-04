# Stage 6.53 - Native mission-end flow

This pass fixes the tactical mission completion sequence introduced with the
mission statistics restoration.

## Original flow restored

For ordinary campaign missions the runtime now uses:

1. finished tactical battlefield remains visible and frozen,
2. `M_ACCOMP.LZ` / `M_FAILED.LZ` banner,
3. mission end debrief text (`MissionEndOKText` / `MissionEndBadText`) in the
   existing native framed Spellcross message window,
4. `WM_STAT.LZ` losses page,
5. optional mission/level cutscene,
6. return to the strategic level and apply the mission result.

The opening Escape mission (`M01_01`) is a special case matching the original
campaign transition: debrief -> story cutscene -> first strategic level.  It
skips the separate losses page in that intro transition.

## Deadlock fix

`SpellMap::CheckAndTriggerMissionEnd()` no longer waits for a tactical child
message-box callback before publishing `MissionEndRequest`.  It queues the
completed result immediately and MainFrame owns the complete presentation
state machine.  This removes the state where a completed `DestroyAllUnits`
mission (for example `M02_02A`) could remain on the tactical map without ever
returning to strategy.

## Level outro distinction

`LevelData` now preserves `PlayEndCANAnim` separately from
`PlayEndDeltaAnim`.  When a story/talking-head CAN outro exists it is preferred
for the post-mission cutscene (e.g. `ALEX.SMK` in LEVEL_01), instead of being
silently overwritten by the later delta animation token.
