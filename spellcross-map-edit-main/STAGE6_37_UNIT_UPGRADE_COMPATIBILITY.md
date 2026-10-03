# Stage 6.37 - Original unit upgrade compatibility

- UPGRADES.DEF is now loaded directly from COMMON.FS when no extracted temp/COMMON copy exists.
- Technology compatibility fails closed: an upgrade is offered only when the original SuitableTypes() explicitly contains the selected unit type.
- Purchase confirmation revalidates the same compatibility list.
- Re-arm targets use canonical original weapon groups and exclude Renegades, commander/convoy/scenario actors and ungrouped units.
- The restored Upgrade panel always shows currently installed Engine / Weapon / Armor slots.
- Original BIG_MAP.SAV import already maps offsets +48/+50/+52 to installed engine/armor/weapon upgrade IDs, so imported upgrades are visible here.

Original data spot-check:
- Commando (unit 3): upgrade 6 Strengthened armor is NOT suitable; armor upgrades 11 UNT Defence and 12 Dragon Armor ARE explicitly suitable in UPGRADES.DEF.
- Piranha (unit 20): upgrade 6 Strengthened armor IS explicitly suitable in UPGRADES.DEF.
