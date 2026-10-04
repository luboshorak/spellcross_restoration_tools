# Stage 6.54 - Mission-end presentation latch

## Original data-driven sequence

The retail mission DEF files explicitly provide `MissionEndOKText(...)` and
`MissionEndBadText(...)`. The original COMMON.FS also contains the native
`M_ACCOMP.LZ`, `M_FAILED.LZ` and `WM_STAT.LZ` result graphics. Therefore the
final objective must transition into the mission result presentation; it must
not be interposed by the restoration-only generic objective-completed popup.

## Fixed race

The previous restoration could finish a mission while a normal tactical
`FormMsgBox` was still open. MainFrame created the result state underneath the
child window, but the canvas was not repainted when the child closed. The next
battlefield click then dismissed the invisible result page and appeared to jump
straight to strategy.

The fix is deliberately redundant:

- when all mission objectives are complete, `CheckObjectiveNotifications()` no
  longer queues the generic `*** objective ***` message;
- a consumed `MissionEndRequest` is stored as a pending presentation until no
  tactical message/video window is active;
- closing a tactical message explicitly schedules the pending result page;
- the completed mission becomes modal immediately: battlefield clicks can no
  longer select/move/attack units while mission-end presentation is pending;
- the result page forces a canvas repaint when it becomes visible.

This keeps intermediate objective notifications intact while making the final
objective follow the original mission-end resources and text fields.
