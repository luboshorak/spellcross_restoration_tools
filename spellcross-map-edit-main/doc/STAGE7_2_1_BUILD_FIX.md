# Stage 7.2.1 build fix

This is a compile-only correction of the Stage 7.2 modern strategic designer prototype.

## Fixed
- Removed the mixed `const char[]` / `wxString` conditional expression used while
  constructing the header subtitle.
- The subtitle is now built explicitly as a `wxString` before calling
  `wxStaticText`, avoiding both the ambiguous conditional conversion and the
  cascading `wxStaticText` constructor overload error.
- Removed the non-ASCII bullet from that newly added source string.

## Not changed
- Strategic game/editor logic.
- Reconstructed Original UI renderer.
- Layout or behavior of the Stage 7.2 prototype.

## Encoding diagnostics
The legacy source tree already contains several non-UTF-8 files while the
Visual Studio project uses `/utf-8`. Those diagnostics are unchanged from the
Stage 6.25 baseline and are not introduced by Stage 7.2.
