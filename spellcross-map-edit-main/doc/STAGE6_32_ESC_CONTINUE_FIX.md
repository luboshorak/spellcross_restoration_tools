# Stage 6.32 - ESC / Continue tactical resume fix

Built on Stage 6.31.

## Bug
When the player pressed ESC during a tactical mission, the main menu correctly
opened and hid the tactical frame. Choosing Continue then called
`LoadMapFromDefPath(spell_map->GetTopPath(), {})`, which reloaded the mission DEF
instead of resuming the existing in-memory battle.

## Fix
`FormMainMenuAction::Continue` now checks the Stage 6.31 pause-flow flag
`m_mainMenuReturnToTactical`. If the menu came from a live tactical game, it
only clears that flag, shows the already existing tactical `MainFrame`, and
returns focus to its canvas through `ShowTacticalWindow()`. No map, save, units,
events, campaign context, or game state are reloaded.

Outside that pause flow, Continue no longer reloads a DEF and falsely acts as a
resume operation; it keeps the main menu open and reports that there is no
paused tactical game to continue.

No strategic renderer or strategic gameplay code was changed.
