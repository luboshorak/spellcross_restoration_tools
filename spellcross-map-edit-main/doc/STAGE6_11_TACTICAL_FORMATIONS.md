# Stage 6.11 - tactical formation propagation

This revision connects the reconstructed strategic hierarchy to the tactical battle map.

## Source verification

The original manual explicitly describes the tactical feedback for battle formations:

- a unit in a battle formation uses a smaller floating status plate and shows the formation number;
- the unit carrying an active commander is marked on that plate;
- higher formation membership is shown in the unit information HUD;
- an active battalion requires at least three companies and its commander must be placed in one of them;
- commanders/formation membership improve subordinate unit combat properties.

The original game data in `COMMON.FS/FORMACIE.DEF` defines the direct formation bonuses:

- battalion / prapor: Attack +1, Defence +1
- regiment / pluk: Attack +2, Defence +1
- brigade / brigada: Attack +4, Defence +3

The original tactical assets are already present and loaded by the project:

- `WM_FORM0.ICO` - battalion mark
- `WM_FORM1.ICO` - regiment mark
- `WM_FORM2.ICO` - brigade mark

The existing restoration code also already contained two dormant original-game hooks:

- `MapUnit::commander_id` + `MapUnit::is_commander` are rendered by the floating unit HUD;
- `map.cpp` loaded `WM_FORM0/1/2`, but the `command_level` used to draw them was hard-coded to zero.

Stage 6.11 wires these original mechanisms to the strategic hierarchy instead of reimplementing their graphics.

## Behaviour

When units are launched from the strategic map, each concrete roster instance now carries:

- strategic roster UID,
- visible formation number,
- highest active formation level,
- original attack/defence formation bonuses,
- whether that company currently carries an active commander.

Formation activation follows the hierarchy:

- battalion: at least 3 participating companies + battalion commander hosted by one of them;
- regiment: both subordinate battalions active + regiment commander hosted in its subtree;
- brigade: both subordinate regiments active + brigade commander hosted in its subtree.

Only units that actually enter the mission count. A company on cooldown cannot keep a tactical formation active off-map.

## Tactical effects

- floating unit HUD shrinks and displays the formation number;
- commander host unit gets the original commander mark;
- unit information HUD displays `WM_FORM0/1/2` according to the highest active formation;
- `MapUnit::GetAttack()` and `MapUnit::GetDefence()` include the original `FORMACIE.DEF` bonus, so the displayed totals and combat model use the same values.

## Battle save compatibility

The tactical binary save payload is bumped from v1 to v2 to preserve formation metadata. The loader accepts both v1 and v2; v1 saves load with no formation metadata, matching their previous behaviour.
