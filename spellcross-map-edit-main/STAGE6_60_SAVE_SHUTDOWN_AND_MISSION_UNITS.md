# Stage 6.60 — unified save/load, deterministic shutdown, mission-earned units

Revision target: v0.7.1 / v30

## Strategic top-menu Save / Load

The strategic frame no longer has a separate user-facing slot picker in its top menu.
`StrategicLevelFrame::OnSaveGame()` and `OnLoadGame()` now use the same MainFrame save/load entry points as the tactical/main window.

* Load accepts `.scsave`, remake strategic `.json`, and original `BIG_MAP.SAV` (`.sav`) from either screen.
* Save is context-aware: tactical screen writes `.scsave` including campaign context, strategic screen writes the strategic `.json` state.
* The strategic Options screen keeps its original nine save/load buttons and slot semantics, because those controls are part of the restored DOS UI. This change is only for the modern top menu requested by the restoration UI.
* The strategic Exit -> "save before exiting?" YES branch uses the same shared Save path as the top menu.

## Shutdown / invisible process fix

There were two independent classes of shutdown problems:

1. Several restored windows are top-level wx windows. Closing only `MainFrame` could leave an invisible strategic/main-menu window alive and therefore keep the wx event loop/process running.
2. map/audio teardown contained unbounded waits. In particular the view-range halt logic waited for the wrong worker state while the map mutex could already be held, and sound shutdown could wait forever for an audio backend callback.

v30 routes all application-exit paths through `MainFrame::RequestApplicationExit()` and makes the path idempotent. It explicitly tears down the main menu, strategic frame and video wrapper, stops MIDI, closes the tactical map, destroys stray top-level windows, and explicitly ends the wx main loop.

Worker/audio teardown is hardened as follows:

* `SpellMap::Close()` halts async unit range/view workers before taking `map_lock`.
* `ViewRange::Halt()` waits while the worker is `BUSY` (the previous `IDLE` wait was reversed and could participate in shutdown races/deadlocks).
* map sounds and global RtAudio channels use bounded shutdown waits; if the backend does not stop, the callback owner is deliberately kept alive rather than freed underneath a live callback.
* narration/video audio shutdown uses the same bounded/no-use-after-free rule.

Debug-only `[SHUTDOWN]` messages mark the terminal path; no new player-facing popup is added.

## Mission-created player units

Original mission DEF unit lifetime is now authoritative when a surviving allied unit was created by the tactical mission itself:

* `MissionUnit` — tactical mission only; never copied to strategic roster.
* `SpecUnit` — mission-critical/special unit; never copied merely because it survived.
* `ArmyUnit` — permanent player army company; copied to the strategic roster with surviving strength, exact tactical experience and name.
* `VoluntUnit` — support/volunteer company for the current strategic chapter; copied with `temporary=true` and removed by the existing chapter-transition cleanup.

The first mission is the important regression case. `M01_01A.DEF` creates the infantry encounters as `MissionUnit` but the commando in the palisades as `ArmyUnit`. The campaign-opening tactical mission runs before a strategic frame exists, so MainFrame also transfers surviving `ArmyUnit` companies when it creates the first strategic screen.

The original EN campaign data additionally uses `VoluntUnit` in `M08_18A.DEF`; this is handled by the normal strategic mission-result path and uses the already-existing temporary-company lifetime.
