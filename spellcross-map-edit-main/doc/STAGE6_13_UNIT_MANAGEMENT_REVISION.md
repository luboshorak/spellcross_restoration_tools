# Stage 6.13 – Unit management revision

This stage revises the strategic **unit management / repair / upgrade** screen and the campaign unit model behind it.

## Source-backed behaviour restored

The Czech manual distinguishes two strategic unit pools:

- **stálé jednotky** (permanent/core companies) persist to later large strategic maps, may be organized into formations, and are limited by the player's rank;
- **pomocné jednotky** (support companies) last only for the current large strategic map. They can still be repaired/recruited and upgraded while present, but may not enter formations.

The original LEVEL_xx.DEF files also establish the actual meaning of `AddUnitToPlayer`:

`AddUnitToPlayer(unit_id, experience_level, health_percent, name)`

Each command adds one company. The second argument is its experience level, not a quantity. Event-created companies are therefore treated as support units.

## Unit roster / graphics

- Strategic companies are normalized to one concrete roster instance per row/slot.
- The upper permanent pool fills the original two-column capacity grid sequentially (first column, then second) rather than mirroring the same company into both columns.
- The separated lower block contains support units.
- Hit testing maps every visible slot to exactly one company, eliminating the apparent double selection.
- The green line in each slot is now a real health/strength gauge and scales with current health.
- Mode selector, lower information area and action/footer widgets were constrained to the geometry defined by `STRUPG.QH` to prevent drawing outside their original frames.

## Recruitment

The manual describes three replacement qualities:

1. rookies – cheapest / fastest, experience falls strongly;
2. veterans – middle option;
3. elite – most expensive / slowest, current unit experience is preserved.

Recruitment now uses the original unit-specific `cost_replace` value extracted from `JEDNOTKY.DEF` (`data/units.json`) as its base cost. The current quality cost/time multipliers remain reconstructed fixed policy because their exact constants have not yet been proven from the original executable.

The experience model follows the documented qualitative behaviour: rookie replacements contribute no retained average experience, veteran replacements retain an intermediate share, and elite replacements preserve the unit's current average experience.

## Upgrades / re-arm

- Researched suitable improvements are available by engine / weapon / armour category.
- Only one installed improvement per category is retained, matching the manual.
- Re-arm stays within the unit category.
- Re-arm removes previous technological improvements and reduces experience.
- Hierarchy labels are refreshed when a permanent company changes type.

## Info

Info mode reports the concrete selected company, including health, experience level/XP and whether it is permanent or support.

## Persistence and tactical identity

- Strategic state persists each concrete company's UID and support/permanent flag.
- Tactical deployment carries the concrete strategic UID and experience.
- Survivors return damage and experience to the correct company even when several companies share the same unit type.
- Support companies are discarded only when transitioning to the next large strategic LEVEL.
- Disbanding/re-arming cleans or updates hierarchy references for the same concrete company.

## Validation

- `source/level.cpp` compiles standalone as C++17 with GCC.
- Modified C++ files pass a structural brace/string/comment scan.
- A complete wxWidgets/MSVC build still needs to be performed in the Windows build environment.
