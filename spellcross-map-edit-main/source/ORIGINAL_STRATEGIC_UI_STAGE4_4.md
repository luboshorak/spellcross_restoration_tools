# Original Strategic UI - Stage 4.4 (unit management fidelity pass)

This pass rebuilds the restored **Unit management / Rizeni bojovych jednotek** screen against a real 640x480 capture of the original game instead of the approximate Stage 4 layout.

Key changes:

- permanent-unit rows use the original 19 px rhythm and original text columns;
- the second roster column shows the unit type instead of a synthetic `OK` status;
- the selected company is indicated by the original-style green frame/status bars;
- the lower helper-unit grid is left clean when no separate helper units exist;
- mode panel colours and selected-mode brackets follow the original screen;
- the upper-right panel is again the original **Motory / Zbrane / Obrana** upgrade area;
- recruit quality choices moved to the large lower information panel, where the original game places them;
- unit information, strength, level and experience were moved to measured original coordinates;
- the selected unit icon is drawn into the original preview box;
- Disband / Time / Cost / OK now sit inside the native VMU_LST1 plates;
- mouse hit areas were updated to the same geometry;
- RESEARCH.DEF `Data()` is now parsed so an `UpgradeItem` maps to the real UPGRADES.DEF id;
- UPGRADES.DEF flags/suitability and UPGRADES.CZ names are parsed for the restored grouped list.

The current wxWidgets unit screen remains available and uses the same game state/actions.
