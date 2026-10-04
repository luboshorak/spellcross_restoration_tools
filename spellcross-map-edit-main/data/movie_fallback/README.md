# Movie fallback pack (v28)

This directory is a compatibility fallback for Spellcross cutscenes that the restoration cannot decode reliably from the original Czech `CAN` format.

Source material: the Czech Smacker conversions from Stanislav Maslan's `Spellcross-Mod-CZ-Hard` / the `MOVIE.zip` supplied for this restoration work. Maslan's README explains that the original Czech proprietary videos were captured through DOSBox and converted to Smacker for the English engine.

For the remake the supplied SMK files were transcoded to standard Windows-friendly H.264/AAC MP4 (320x200, original frame cadence preserved). The game keeps using the original `DPK`/`DP2` decoder first. For `CAN`/`SMK` requests it prefers this pack, and for any legacy movie that fails to decode it can fall back by matching the basename (`INTRO.CAN` -> `INTRO.mp4`, `LEVEL1_1.DPK` -> `LEVEL1_1.mp4`, etc.).

The transcode used FFmpeg with frame timestamps preserved (`-fps_mode passthrough`). `MANIFEST.tsv` records the source name, frame count, duration, codec details, size and SHA-256 of every bundled MP4. All 37 supplied SMK files were checked to retain the exact source frame count and duration.
