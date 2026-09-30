# Original strategic UI — Stage 1

This branch intentionally does **not** reskin the existing strategic wxWidgets layout.
It adds a parallel renderer selected via **Strategic UI -> Original / restored UI (experimental)**.

Stage 1 currently:
- renders a self-contained logical 640x480 original strategic-map framebuffer;
- composes BIG_MAP.LZ + VMM_FULL.LZ + VMM_LST1/2.LZ;
- composes LEVEL_XX.LZ / HMLA__XX.LZ using LEVEL_XX.CLK;
- builds the original split palette from _SHARED1.PAL, LEVEL_XX.PAL, BIG_MAP.PAL;
- maps the remake's shared owned/visible territory state to revealed / enemy-hatched / hidden visuals;
- uses integer nearest-neighbour scaling whenever possible;
- hit-tests the restored map using the same CLK data and routes selection back through SelectTerritoryById();
- leaves the existing strategic UI untouched and instantly switchable.

Not yet implemented in the restored branch:
- right-hand unit/commander list;
- status values and original toolbar icons/buttons;
- briefing text rendering in the lower frame;
- attack/cancel controls;
- other strategic pages (hierarchy, units, buy, research, info, resources, statistics, options).

> Superseded by `ORIGINAL_STRATEGIC_UI_STAGE2.md` for the first playable restored map screen.
