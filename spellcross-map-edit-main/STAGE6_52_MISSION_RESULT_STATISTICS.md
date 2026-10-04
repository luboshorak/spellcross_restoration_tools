# Stage 6.52 – Original mission result statistics

This pass restores the original end-of-mission casualty table and makes the
same counters authoritative across tactical play, strategic statistics and
save/load.

## Original UI assets

The tactical result overlay uses the original COMMON.FS resources:

- `M_ACCOMP.LZ` – mission accomplished title (340 px wide)
- `M_FAILED.LZ` – mission failed title (340 px wide)
- `WM_STAT.LZ` – 408x175 casualty table

They are drawn over the finished tactical battlefield at native 640x480
coordinates. Dynamic labels/numbers use the original SpellFont shadow renderer.
The tactical simulation is frozen while the result table is visible; a click
continues to the existing video/strategic-return flow.

## Casualty accounting

Casualties cannot be reconstructed reliably by scanning the unit vector at the
end of a battle because destroyed units are extracted and deleted during play.
`SpellMap` therefore owns a per-mission loss ledger. It records a casualty
immediately before a destroyed unit is removed.

Counters are split exactly like the original result/statistics tables:

- light units
- heavy units
- air units
- commanders

for Alliance and Dark Side separately. A commander is an additional casualty,
not a replacement for the destroyed host company's light/heavy/air casualty.
If one strategic company hosts more than one hierarchy commander, all commander
bits in `formation_commander_mask` are counted.

The ledger is also appended to tactical `.scsave` campaign context (context
version 3), so saving and loading in the middle of a mission does not forget
units that had already been destroyed.

## Strategic accumulation

On mission return the tactical ledger is added to both:

- current strategic level losses
- all-campaign losses

The existing restored Statistics page reads those same `m_lossStats` buckets,
so the mission-result table and strategic statistics now share one source of
truth.

The opening tactical mission is handled as well: when no StrategicLevelFrame
exists yet, its result ledger is transferred into the newly created campaign
state.

## Modern save persistence

Strategic JSON state is now version 2 and embeds a `statistics` object containing
all four Alliance/Dark Side loss buckets plus mission/territory/turn counters.
This removes dependence on the old installation-global `strategic_stats.json`
sidecar when loading a save slot or an embedded tactical campaign snapshot.
The sidecar is retained only as a legacy/debug output and is no longer allowed
to overwrite statistics restored from an exact save snapshot.

## Original BIG_MAP.SAV

The original DOS save already stores the strategic loss counters. Import uses:

- `0x5101` enemy all-time light
- `0x5105` enemy all-time heavy
- `0x5109` enemy all-time air
- `0x510D` Alliance all-time light
- `0x5111` Alliance all-time heavy
- `0x5115` Alliance all-time air
- `0x5119` Alliance all-time commanders
- `0x511D` enemy current-level light
- `0x5121` enemy current-level heavy
- `0x5125` enemy current-level air
- `0x5129` Alliance current-level light
- `0x512D` Alliance current-level heavy
- `0x5131` Alliance current-level air
- `0x5135` Alliance current-level commanders

The original save format does not expose separate Dark Side commander counters
at the corresponding block, so those import as zero. The restoration currently
imports original `BIG_MAP.SAV`; it does not write a replacement DOS BIG_MAP.SAV.
