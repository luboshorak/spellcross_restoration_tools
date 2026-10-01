# Stage 6.24 – remove strategic debug start dialog and FINAL TERRITORY tag

Two restoration-only UI artefacts were removed from normal campaign play.

## Strategic startup / mission return

The old `StrategicLevelFrame` constructor contained a `wxSingleChoiceDialog`
with `[DEBUG] Start new...` options. This dialog is no longer shown.

When `skipAutosave == false`, startup is now deterministic:

1. If the current level has an autosave, load it.
2. Otherwise, if a previous-level campaign save exists, import that campaign state.
3. Otherwise start from the level's normal initial state.

Mission-return and automatic level-transition paths already use
`skipAutosave == true`, so their live state is not overwritten.

## Territory briefing

The restoration-only literal prefix:

`[FINAL TERRITORY]`

was removed from the player-facing territory briefing. This does **not** change
`End(n)`, `is_final`, campaign completion, LASTTERT graphics, or any final-
territory gameplay logic.
