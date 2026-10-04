# Stage 6.59 — silent successful video fallback (v29)

The v28 video fallback path worked correctly, but its successful-path diagnostics used
`wxLogMessage()`. With the GUI log target this can surface as an informational popup,
interrupting cutscene playback even though nothing is wrong.

v29 changes only the successful-path diagnostics in `FormVideoBox`:

- successful original-archive video selection uses `wxLogDebug()`;
- successful MP4 fallback selection uses `wxLogDebug()`;
- actual playback/load failures still keep their existing error handling.

No fallback lookup, video mapping, Media Foundation playback, timing, audio, or cutscene
flow logic was changed.
