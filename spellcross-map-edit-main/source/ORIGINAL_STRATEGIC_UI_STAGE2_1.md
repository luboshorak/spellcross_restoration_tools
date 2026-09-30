# Original Strategic UI – Stage 2.1 runtime-loader hotfix

This hotfix addresses the black restored-UI screen that previously only showed:
`Original strategic renderer could not build this screen.`

Changes:
- original renderer errors are now drawn directly on the canvas (asset name / size / level issue), not hidden only in a tooltip;
- asset lookup first uses the live `COMMON.FS`, then falls back to the decoded/exported resource folders (`temp/COMMON`, the loaded LEVEL DEF folder, and configured data roots);
- if a runtime returns raw compressed `.LZ/.LZ0` data instead of DELZ_ALL output, the renderer now expands it only when its size is below the known decoded size;
- LEVEL_01 is reported explicitly as the intro/tactical level with no original strategic map;
- all LEVEL_02..LEVEL_10 strategic renderer compositions were re-tested against the extracted COMMON resources.

The Current UI branch is unchanged and remains available via `Strategic UI -> Current UI`.
