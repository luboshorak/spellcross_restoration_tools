# Stage 6.38 – STRUPG support grid + parallel upgrades

This pass fixes two behaviours directly documented by the original `STRUPG.QH` coordinate/help records.

## Native roster geometry

The unit-management screen uses two different roster rectangles and they must not share the same column geometry:

- core units: `(15,11)`, `302x306` => two 151 px columns, 16 rows at 19 px
- support units: `(20,329)`, `290x137` => two 145 px columns, 7 rows at 19 px

The support roster is therefore shifted 5 px right and has narrower columns than the core roster. Rendering, selection overlay clipping and mouse hit-testing now use these native rectangles separately.

## Multiple upgrades at one time

`STRUPG.QH` describes Upgrade mode as allowing a unit to be upgraded with **one or more upgrades at one time**.

Tech upgrades now have independent pending slots for:

1. engine
2. weapon
3. armour

Each pending upgrade keeps its own remaining turns. The unit's strategic unavailability is the maximum remaining time among those pending upgrades, not their sum. Thus e.g. engine=2, weapon=1, armour=3 completes weapon after one turn, engine after two, armour after three, while the unit is unavailable for three turns total.

Recruitment and complete re-arm remain exclusive operations and cannot overlap with these tech-upgrade jobs.

Pending category jobs are persisted in strategic saves. Same-turn cancellation refunds only jobs that are still cancelable in that purchase turn.
