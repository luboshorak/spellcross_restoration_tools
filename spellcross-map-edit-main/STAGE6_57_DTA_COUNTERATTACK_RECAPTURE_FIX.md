# Stage 6.57 — DTA counter-attack / recapture fix (v27)

## Symptom

After an Other Side strategic counter-attack, declining the defence correctly removes the territory from Alliance control.  Attacking that territory again then failed in the launcher with for example:

`Map DEF not found for mission: m05_02`

## Root cause

This was not a missing-file installation problem.  The original campaign intentionally has strategic territories whose base battlefield exists only as `Mxx_yy.DTA`; there is no matching scripted `Mxx_yy.DEF`.

`LEVEL_05.DEF` is a direct example:

- `Territory(2,none,m05_02,mus04)`
- `M05_02.DTA` exists in `COMMON.FS`
- `M05_02.DEF` does not exist

The DOS strategic layer generated the defence/recapture tactical mission at runtime instead of loading a normal mission DEF.  The original save data confirms that path: `R0502.MIS` contains the generated Other Side force for the `M05_02` battlefield.  Its unit composition matches the `Army(...)` records of the triggering LEVEL_05 event (50 companies in Event 6: unit ids 38×5, 53×12, 61×15, 47×1, 48×3, 57×6, 50×4, 56×4).

v26 handled the strategic ownership change but discarded that attacking force.  `OnLaunch()` then treated every hostile territory as a normal scripted mission, called `ResolveMapDefPathForMissionToken("m05_02")`, and stopped when the intentionally non-existent DEF could not be found.

## v27 fix

### Preserve strategic attackers

`CounterAttackState` now stores:

- `territory_lost`
- `enemy_units`

Level-event `Army(...)` data is copied into the counter-attack state before the player is asked whether to defend.  If the defence is declined or lost, the army remains attached to the occupied territory.  If the defence/recapture succeeds, the stored occupiers are cleared.

The new fields are also serialized in `mission_flow.counter_attacks`, so save/load preserves the occupying force.

### Generate the DTA-only tactical battle

When a mission token has no DEF but has a DTA, `OnLaunch()` now uses the DTA-only strategic-battle path instead of reporting a missing DEF.

`MainFrame::LoadGeneratedStrategicBattleFromDtaPath()`:

1. loads the original `Mxx_yy.DTA` battlefield;
2. creates Alliance deployment squares in the lower map band;
3. places the stored Other Side companies in the opposite map band using the normal `PlaceUnit()` terrain validation;
4. adds the normal `DestroyAllUnits` mission objective;
5. deploys the selected strategic Alliance companies while preserving strategic UID, formation, commander and upgrade metadata;
6. recalculates tactical formations and enters normal game mode.

Normal scripted missions continue to use their original DEF path unchanged.

### Compatibility with v26 strategic saves

A v26 save cannot contain `enemy_units`, so v27 also reconstructs the most recent already-triggered `Army(...)` event from the persisted `triggered_events` set.  This recovers the exact force for the common upgrade case where the player already lost the territory under v26 and installs v27 before recapturing it.

If even that information is unavailable, the code has a final deterministic fallback using the level's `AttackUnits`, `AttackSpecialUnits` and `AttackFlags` pools rather than failing on a missing DEF.

## Files changed

- `source/forms/form_level.h`
- `source/forms/form_level.cpp`
- `source/main.h`
- `source/main.cpp`

## Regression boundary

The fix is deliberately scoped to the strategic generated-battle case: a real scripted mission still requires its DEF.  A missing DEF for a scripted mission therefore does not silently turn into a generic skirmish unless a matching territory DTA exists and the strategic flow provides/reconstructs an Other Side force.
