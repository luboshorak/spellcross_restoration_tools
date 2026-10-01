# Original strategic UI — Stage 6.7 (save/options + default restored UI)

This pass completes the ninth strategic screen and makes the reconstructed
640x480 strategic UI the default branch. The previous wx strategic UI remains
available from `Strategic UI -> Current UI`.

## Native OPTIONS reconstruction

The screen is composed from original `COMMON.FS` resources:

- `BIG_MAP.LZ` / `BIG_MAP.PAL` — common 640x480 right-side chrome
- `VMO_FULL.LZ` — screen-specific shell
- `OPTIONS.LZ` — 569x464 save/options panel at `(6,8)` (verified against original 640x480 frame; right edge lands exactly at x=575)
- `_SHARED1.PAL` + `_OPTIONS.PAL` + `BIG_MAP.PAL` — native palette banks
- `VMO_BAR.LZ` — native 10x12 slider thumb
- `STROPT.QH` — recovered interaction rectangles

Recovered `STROPT.QH` geometry used by the implementation:

- saved positions: `115,25,355,277`
- Load column: `20,22,75,280`
- Save column: `490,22,75,280`
- gamma: `28,333,178,41`
- music: `28,380,178,41`
- sound effects: `28,425,178,41`
- battlefield resolution: `234,339,166,76`
- Quick Help: `434,336,74,77`
- Exit: `327,435,113,41`

## Functional controls

- Nine visible DOS-style save rows.
- Empty slots disable Load while Save remains available.
- Slot captions are generated from the persisted ISO save timestamp.
- Gamma is connected to `SpellMap::GetGamma/SetGamma`.
- Music volume is connected to the live MIDI volume.
- Sound volume is connected to the live sound-channel volume.
- Battlefield resolution selector: **removed in Stage 6.20** because the remake does not implement the original tactical rendering modes. Its native STROPT panel remains intentionally inactive.
- Quick Help selection is represented and clickable; the remake currently has no native contextual Quick Help subsystem to toggle behind it.
- Exit behavior was revised in **Stage 6.20**: the native STROPT Exit button now offers Save / Don't save / Cancel and exits the whole application.
- The ninth toolbar icon stays inside the restored framebuffer instead of
  opening the wx Screen dialog.
- The native selection brackets and +/- slider glyphs are reconstructed at
  pixel level so they remain inside their original plates.
- Mouse-wheel input on Options/Resources/Stats is explicitly kept out of the
  strategic-map unit list, avoiding hidden cross-screen scrolling.

## Default UI

New strategic windows enter the reconstructed UI by default. The older wx
layout is intentionally kept intact and can be selected from the Strategic UI
menu at any time.

## Verification

- `source/strategic_original_renderer.cpp` compiles cleanly as C++17 with GCC.
- OPTIONS palette/resource dimensions were verified directly from the original
  English Spellcross `COMMON.FS` supplied with the project sources.
- Static OPTIONS composition was compared with the supplied `Spell_settings.png`
  reference before adding live labels and slider positions.

## Conservative visual audit

The pass deliberately does not replace already-working screens with new generic
controls. Existing stage 6 fixes were retained: native asset palettes, clipped
DOS-font text, the corrected BUY info-panel seam, the 31px toolbar pitch and
the 591..596 active-screen wedge. The ninth screen uses the same 640x480
framebuffer path, so it cannot overflow its original 575px strategic-content
area into the right toolbar/status chrome.

## Build note

The independent renderer translation unit and a RenderOptions smoke test were
compiled with GCC C++17 against the extracted original assets. A full Windows
GUI executable cannot be produced in this Linux environment because the
solution depends on MSVC/wxWidgets; the package therefore contains the complete
modified source tree plus an applyable patch.
