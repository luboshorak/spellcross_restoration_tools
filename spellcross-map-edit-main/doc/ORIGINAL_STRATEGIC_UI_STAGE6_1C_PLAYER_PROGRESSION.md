# Original strategic UI – Stage 6.1c player progression

This patch restores the strategic progression of John Alexander from the original
Spellcross campaign data instead of deriving his rank from tactical kill counts.

## Original data semantics

`COMMON.FS/HODNOSTI.DEF` defines:

```
DefineCommander(rank, max_units, commander_actions, player_xp, max_commanders)
```

The third and fourth parameters are **different progression systems**:

- `commander_actions` is the number of combat actions required for a subordinate
  commander to reach that rank.
- `player_xp` is the strategic experience threshold for John Alexander.

Canonical original table:

| Rank | Permanent units | Commander actions | Player XP | Max commanders |
|---:|---:|---:|---:|---:|
| 0 | 2  | 2  | 0     | 0  |
| 1 | 4  | 6  | 0     | 0  |
| 2 | 9  | 10 | 0     | 2  |
| 3 | 14 | 18 | 300   | 4  |
| 4 | 18 | 26 | 2550  | 6  |
| 5 | 22 | 36 | 5350  | 8  |
| 6 | 28 | 48 | 10000 | 10 |
| 7 | 34 | 66 | 16000 | 12 |
| 8 | 40 | 84 | 26000 | 14 |

The original data therefore deliberately puts a fresh player with 0 strategic XP
at rank 2 (Captain), because ranks 0, 1 and 2 all have XP threshold 0. This gives
the player two commander slots immediately after the introductory mission.

`LEVEL_XX.DEF` mission blocks use:

```
EndOK(money, player_experience)
```

For example the original campaign contains `EndOK(50,40)`, `EndOK(250,150)`,
`EndOK(780,2800)` and later much larger rewards. These values now feed the
strategic money and John Alexander XP counters when a mission succeeds.

`LEVEL_01.DEF` contains no explicit `EndOK(...)` reward. Thus the introductory
mission can legitimately leave the strategic XP counter at 0; the important
part is that 0 XP still resolves to Captain with two commander slots.

## Fixed behavior

- John rank is now based only on the `player_xp` column from `HODNOSTI.DEF`.
- Successful missions award the exact `EndOK(money, experience)` values.
- Tactical enemy kills no longer invent strategic player XP.
- Old saves with a stale serialized rank are repaired when loaded.
- Rank is normalized when a campaign is started, loaded, continued, or moved to
  the next strategic level.
- Missing external `HODNOSTI.DEF` falls back to the exact original table above.
- Newly offered commanders start at rank 1 (1st Lieutenant / `npor.`), as in the
  original manual/data model.
- Original UI commander abbreviations and player rank labels are aligned with
  the original rank indexing.

Subordinate commander promotion by their own combat-action count is a separate
mechanic from John Alexander's strategic XP and is intentionally not conflated
with this player-progression fix.
