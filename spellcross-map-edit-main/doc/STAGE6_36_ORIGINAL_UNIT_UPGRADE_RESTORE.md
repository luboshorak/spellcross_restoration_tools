# Stage 6.36 - Original strategic unit upgrade restore

## Scope

This patch restores the strategic **unit modification / upgrade** path against the original Spellcross data and manual. It covers researched technological upgrades (engine / weapon / armour), their delayed application, tactical stat effects, cancellation during the purchase turn, persistence, and pending re-arm handling.

## Original sources checked

- `COMMON/UPGRADES.DEF`: 22 upgrades (`0..21`), authoritative `UpgradePrice`, `UpgradeTime`, `Flags`, `SuitableTypes`, and modifiers `Move`, `Defence`, `Attack`, `AttackPT`, `Range`.
- `COMMON/RESEARCH.DEF`: 22 `UpgradeItem` records; every `Data(N)` maps to a valid `UPGRADES.DEF` id.
- `COMMON/UPGRADES.CZ` / `.ENG`: line-indexed upgrade names.
- `STRUPG.QH`: original 640x480 hit rectangles / panel geometry.
- Original `BIG_MAP.SAV` layout already decoded by the project: installed engine/armour/weapon ids are stored separately, and pending re-arm target is separate from current unit type.
- Original manual: modified units are unavailable for a number of turns; in the first turn the modification can be cancelled; one improvement from each of the three categories can be installed; re-arming removes technological improvements and reduces experience.

## Fixed mechanics

1. Upgrade id `0` (AD320) is now selectable/purchasable. The previous `> 0` test accidentally excluded the first original upgrade.
2. Upgrade availability follows completed `RESEARCH.DEF` `UpgradeItem -> Data()` links in game mode and `SuitableTypes()` from `UPGRADES.DEF`.
3. Price and duration come strictly from `UPGRADES.DEF`. Unknown definitions no longer fall back to a fake purchasable price/time.
4. Purchases are pending operations. Money is deducted when ordered; the unit remains unavailable for the original `UpgradeTime`; the module is installed only when the timer expires.
5. During the purchase turn the pending modification can be cancelled by clicking the unit; its cost is refunded. After ending the turn the operation is committed and can no longer be cancelled.
6. Only one installed module per original category (engine / weapon / armour) is retained. Completing a new module replaces the previous module of that category.
7. Installed technological modifiers now affect tactical combat:
   - `Move` -> action-point / movement allowance bonus
   - `Defence` -> defence
   - `Attack` -> attack
   - `AttackPT` -> attacks per turn
   - `Range` -> fire range
   Negative original modifiers are preserved.
8. The five tactical modifier values are carried into battle and persisted by the tactical save format (save version 4; older saves remain readable with zero upgrade modifiers).
9. Re-arm is now pending instead of changing the type immediately; completion changes the type, clears all installed technological upgrades, and refreshes hierarchy labels. Original BIG_MAP imports preserve pending re-arm rather than prematurely applying it.
10. Strategic JSON saves persist pending operation kind/value/cost/cancellability.

## UI restoration

The existing restored 640x480 STRUPG composition and original QH geometry are retained. The upgrade information panel now shows the actual original-data modifiers for the selected module together with original price/time, while unavailable units continue to show their remaining turns.

## Remaining reconstructed constants

The original data files/manual do not expose an exact numeric experience-loss formula or a dedicated re-arm duration constant. Therefore the pre-existing remake policy remains for those two details: re-arm duration `1` strategic turn and experience reduction `20%`. These are deliberately documented as reconstructed rather than presented as source-exact. Recruitment quality multipliers are likewise outside this upgrade restoration and remain reconstructed.
