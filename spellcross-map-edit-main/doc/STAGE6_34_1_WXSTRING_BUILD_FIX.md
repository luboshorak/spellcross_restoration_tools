# Stage 6.34.1 - wxString build fix

Compile-only fix for the Stage 6.34 first-run data wizard.

The `wxFileDialog` initial-directory argument used a conditional expression
mixing `wxEmptyString` (`const wxChar*`) with `wxString`. MSVC/wxWidgets reports
that expression as ambiguous and may also emit a secondary private
`wxString` constructor diagnostic.

The directory is now built explicitly as a `wxString` before constructing the
dialog. No loader, cache, gameplay, UI-flow, or Original UI behavior changed.
