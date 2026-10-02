# Stage 6.34.2 - main menu panel/cache fix

Compile/runtime correction on top of Stage 6.34.1.

The main 640x480 MAINMENU background can load successfully while the central
MAINM_BG overlay fails, leaving the large black hole visible. The loose-file
fallback treated every `.LZ` as compressed, even though the persistent
`temp/COMMON` cache stores COMMON.FS members already de-LZ'd while retaining
the `.LZ` filename.

Changes:
- Prefer MAINM_BG directly from the already-loaded COMMON.FS.
- Accept MAINM_BG / MAINMBG / MAINM-BG naming variants.
- Loose/cache fallback detects the known decoded 255x272 (69360 byte) payload
  and does not decompress it again.
- Keep 255x237 and generic dimension fallbacks for compatibility.

No strategic renderer/gameplay logic was changed.
