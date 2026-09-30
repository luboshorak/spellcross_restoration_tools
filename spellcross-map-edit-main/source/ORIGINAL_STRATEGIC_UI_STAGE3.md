# Original Strategic UI – Stage 3

Stage 3 keeps the existing/current strategic UI intact and extends the parallel
`Original / restored UI` branch.

## Fixed in this stage

### Czech bitmap-font text
The restored view used UTF-8 narrow C++ literals such as `"Peníze"` and
`"Výzkum"`. On Windows/MSVC those literals could be converted through the
system locale before the Spellcross CP895 conversion, causing accented glyphs
to disappear while text coming from game data rendered correctly.

Restored-UI labels now use Unicode wide literals (written with `\uXXXX`
escapes so the source file encoding is irrelevant). They are then converted by
the existing Spellcross CP895 path. This covers e.g. `Speciální`, `Peníze`,
`Výzkum`, `Útok`, `Zrušit`, `Velitelé`, `Část` and the empty-selection prompt.

## New restored screen: Battle hierarchy

The hierarchy icon now stays inside the restored branch instead of falling back
to the current wxWidgets hierarchy page.

Static original artwork is reconstructed from:

- `BIG_MAP.LZ`
- `VMH_FULL.LZ`
- `HIERARCH.LZ` (406x464 at 6,8)
- `_SHARED1.PAL`
- `_HIERAR.PAL`
- `BIG_MAP.PAL`

The hierarchy screen then overlays live game state:

- current unit roster on the right,
- current commander roster,
- money / research / turn,
- formation slots for battalions, regiments and brigade,
- commander assignment slots (`?` until assigned),
- native `Část 1 / Část 2` page selector,
- common original strategic toolbar.

### Interaction

- click the hierarchy toolbar icon from the restored map -> restored hierarchy;
- click map icon -> restored map;
- click a unit slot -> existing unit assignment dialog;
- click a commander slot -> commander assignment dialog added in Stage 3;
- click the `Část` button -> switches between the two hierarchy halves;
- lower-right turn panel remains active;
- screens not restored yet still fall back to Current UI.

Both restored and current hierarchy views edit the same existing hierarchy data
model, so the new view does not fork game logic.

## Validation performed

- independent strategic renderer compiles with GCC (`-Wall -Wextra -pedantic`;
  only pre-existing `LZ_spell.cpp` warnings remain),
- strategic maps LEVEL_02 through LEVEL_10 still render in the standalone test,
- hierarchy static renderer produces a 640x480 framebuffer and was verified
  pixel-for-pixel against an independent reconstruction from the original
  assets,
- brace/string/comment structural check on modified `form_level.cpp` passes.

A full Windows wxWidgets/MSVC build cannot be run in the current environment,
so that remains the first runtime check on Windows.
