# Stage 6.25 – remove post-mission strategic flow popup

The post-mission `Spellcross Information` dialog was caused by an active
`wxLogMessage()` in `StrategicLevelFrame::HandleMissionResult()`:

`[STRATEGIC FLOW] result terr=... end=... token=... successor=... complete=...`

With the application's wxWidgets log target this informational log message was
shown as a modal GUI dialog every time a tactical mission returned to the
strategic layer.

Stage 6.25 removes that log call entirely.

This is separate from the Stage 6.24 cleanup, which removed the old strategic
startup state-selection/debug dialog and the `[FINAL TERRITORY]` briefing tag.
