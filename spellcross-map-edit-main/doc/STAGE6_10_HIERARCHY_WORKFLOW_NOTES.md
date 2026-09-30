# Stage 6.10 – hierarchy workflow revision

Targeted revision focused on the restored **Hierarchy** screen and the shared hierarchy data model.

## Main changes

### 1) Native right-hand pool selection on the restored hierarchy screen
- units in the right list can now be **selected directly**,
- commanders in the lower-right list can now be **selected directly**,
- the selected pool row is highlighted,
- clicking the same pool row again clears the selection.

### 2) Two-step placement workflow
After selecting a unit or commander in the right-hand pool:
- click a compatible slot in the hierarchy tree to place it,
- invalid target clicks show an explanatory message instead of silently doing something odd,
- if nothing is selected in the pool, the old chooser-dialog workflow still works as fallback.

### 3) Shared assignment helpers
Hierarchy assignment now goes through centralized helpers:
- `AssignCommanderToHierarchySlot(...)`
- `AssignUnitToHierarchySlot(...)`

This unifies:
- original restored hierarchy clicks,
- current chooser dialogs,
- drag/drop payload handling.

### 4) Better unit-slot rules
- regular unit slots still enforce uniqueness of a concrete unit instance,
- assignment slots under commanders only accept units already present in that commander's subtree,
- replacing a unit clears stale commander-assigned references when needed,
- moving commanders continues to keep one commander instance in only one hierarchy slot.

### 5) Better drag payloads
Unit drag payloads now carry the concrete roster UID, so drag/drop no longer depends only on visible text.

## Expected impact
This should make hierarchy editing feel much closer to the intended original workflow and remove a large part of the “half-implemented” feeling around:
- selecting units/commanders from the list,
- placing them into hierarchy fields,
- keeping the shared hierarchy model coherent for other parts of the strategic layer.

## Files touched
- `source/forms/form_level.h`
- `source/forms/form_level.cpp`
