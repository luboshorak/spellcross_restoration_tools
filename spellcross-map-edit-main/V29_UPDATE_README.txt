Spellcross v0.7.1 - v29 update from v28

Purpose:
- removes informational popup/log dialogs when a video fallback is successfully used
- does not change fallback selection, playback, timing, audio, or cutscene flow
- real video playback failures still keep their existing error handling

Apply:
Extract this ZIP over the spellcross-map-edit-main directory from v28, preserving paths.

Changed:
- source/forms/form_video_box.cpp
- STAGE6_59_SILENT_VIDEO_FALLBACK.md

The v28 data/movie_fallback assets are unchanged and are therefore not duplicated here.
