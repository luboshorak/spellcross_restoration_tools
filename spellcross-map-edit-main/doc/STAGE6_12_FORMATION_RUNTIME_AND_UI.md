# Stage 6.12 – live formations, hierarchy highlights and strategic formation shortcuts

This stage continues the Stage 6.11 formation implementation and targets the remaining behaviour visible in the original game/reference captures.

## Original data used

The implementation is based on the original Spellcross assets/data in `COMMON.FS`:

- `FORMACIE.DEF` – live formation combat bonuses
  - battalion / prapor: Attack +1, Defence +1
  - regiment / pluk: Attack +2, Defence +1
  - brigade: Attack +4, Defence +3
- `UNT_ACT.LZ0` – active company outline
- `PRP_ACT.LZ0` – active battalion outline
- `PLK_ACT.LZ0` – active regiment outline
- `BRG_ACT.LZ0` – active brigade outline
- `STRHIER.QH` – hierarchy interaction/list geometry

`STRHIER.QH` describes the right hierarchy roster as `416,10,137,467`, which is why the restored hierarchy list must continue almost to the bottom of the 480 px screen.

## 1. Tactical formations now collapse/downgrade live

Every deployed strategic company now keeps a persistent battalion membership plus a commander-host bit mask:

- bit 0: battalion commander is in this company
- bit 1: regiment commander is in this company
- bit 2: brigade commander is in this company

The tactical map calls `RecalculateTacticalFormations()` whenever a formation member is added/removed from the battlefield. This recalculates all surviving allied companies:

- active battalion requires at least 3 surviving companies and its surviving commander host,
- active regiment requires both child battalions plus its surviving commander host,
- active brigade requires both child regiments plus its surviving commander host.

Consequences are immediate. Example: if the brigade commander's company dies, the brigade bonus disappears immediately, but surviving regiments can remain active and their companies fall back to regiment-level bonuses instead of losing all formation benefit.

The tactical save payload is version 3 and stores persistent formation membership and commander-host masks. Older v1/v2 saves remain readable.

## 2. Dead commander host is removed from strategic state

Mission launch now stores the exact strategic UID of every company sent into battle. On return, surviving tactical UIDs are compared with the sent UID set.

If a company that hosted one or more commanders did not survive:

- the commander(s) assigned to that company are removed from the owned commander pool,
- their hierarchy commander slots are cleared,
- their assignment slots are cleared,
- the dead company hierarchy slot is cleared,
- selected-formation shortcut state is cleaned up.

Tactical campaign context was bumped to version 2 so the exact sent UID list also survives tactical Save/Load.

## 3. Original active-formation graphics in Hierarchy

The restored hierarchy now draws the original `*_ACT.LZ0` assets over functional formations:

- `UNT_ACT` over occupied companies belonging to an active battalion,
- `PRP_ACT` over an active battalion,
- `PLK_ACT` over an active regiment,
- `BRG_ACT` over an active brigade.

The assets use the original hierarchy palette (`_SHARED1.PAL`, `_HIERAR.PAL`, `BIG_MAP.PAL`) and palette index 0 is treated as transparent.

## 4. Hierarchy right list extends to the bottom

The hierarchy list no longer reuses the shorter strategic-map list height. Its background/grid and scrollbar are extended to the native hierarchy height, removing the large black area at the bottom.

## 5. Strategic map `Special` formation shortcuts

Functional formation commanders are listed below `Select all / Deselect all` in the reconstructed strategic map, as in the original game.

Clicking a commander:

- selects all currently deployable companies below that commander,
- clicking again deselects that formation,
- nested selected formations are reconciled correctly,
- manual per-company changes clear a commander shortcut when its full formation is no longer selected.

Selected commander shortcuts and selected companies use the original-style red selection treatment; selected company rows also receive the `*` prefix seen in the original reference.

## Validation in this environment

- structural C++ delimiter/string/comment scan passed for all modified source/header files,
- formation-state simulations verified battalion/regiment/brigade promotion and live downgrade after commander-host loss,
- original `FORMACIE.DEF` values and `STRHIER.QH` geometry were checked directly,
- only formation/hierarchy/tactical-related source files differ from Stage 6.11.

A full Windows MSVC + wxWidgets build still needs to be run in the normal project build environment.
