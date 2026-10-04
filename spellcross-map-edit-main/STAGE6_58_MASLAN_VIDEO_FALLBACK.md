# Stage 6.58 — reliable cutscene fallback from Maslan SMK conversions (v28)

## Problem

The restoration's legacy `SpellVideo::DecodeCAN()` only recovers the audio stream from the Czech proprietary CAN movies. `FormVideoBox::OnPaintTab()` deliberately skipped CAN video frames (`!m_video->isCAN()`), so sequences such as `INTRO.CAN` and the chapter-opening `L_01.CAN` played sound with no picture.

This is not a missing-map or campaign-script problem: the image codec for the Czech CAN payload is simply not implemented in the restoration.

## Source used

Stanislav Maslan's `Spellcross-Mod-CZ-Hard` solves the same compatibility problem for the English engine by capturing the Czech videos through DOSBox and converting them to Smacker (`SMK2`). The supplied `MOVIE.zip` contains 37 such converted movies. Rather than attempting to guess/reverse-engineer the unknown CAN picture codec again, v28 uses those known-good visual conversions as a compatibility source.

## v28 implementation

* `FormVideoBox` looks for a basename-matched fallback in `data/movie_fallback`.
* Requests for `.CAN` and `.SMK` prefer the fallback immediately.
* `.DPK` / `.DP2` keep the existing native decoder first; if that load/decode fails and a basename-matched fallback exists, the fallback is used.
* The fallback files are standard H.264/AAC MP4 and are played on Windows through native Media Foundation / MFPlay. No FFmpeg process or third-party runtime codec is required by the game.
* Playback remains inside the existing Spellcross movie frame/chrome. ESC still closes the cutscene and the existing parent `wxEVT_CLOSE_WINDOW` completion flow is preserved.
* Missing/failed original and fallback paths are logged with `[VIDEO]` diagnostics instead of silently losing the picture.
* The Visual Studio post-build target now copies `data/movie_fallback` into the executable's runtime data directory.

## Assets

All 37 SMKs from the supplied pack were transcoded, not just the currently failing CAN clips. This means the same pack can also rescue a DPK movie if its original archive entry is absent or its native decode fails.

The MP4 transcode preserves the source dimensions (320x200), exact source frame count, frame cadence and total duration. See `data/movie_fallback/MANIFEST.tsv` for per-file verification and hashes.

## Deliberate non-change

The original CAN audio decoder is retained. If the fallback pack is removed, the old legacy path still exists; v28 does not rewrite original Spellcross archives or campaign definitions.
