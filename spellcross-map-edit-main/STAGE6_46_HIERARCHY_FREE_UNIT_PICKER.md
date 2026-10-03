# Stage 6.46 – Hierarchy free-unit picker

The hierarchy assignment dialog now lists only permanent/core companies that
are not already placed in another physical battalion unit slot. The company
currently occupying the edited slot remains visible so an existing assignment
can be left unchanged or cleared with `<none>`.

Commander host/reference slots (`battalion_X_commander_unit`,
`regiment_X_unit`, `brigade_X_unit`) are deliberately ignored when deciding
whether a company is already used: they reference a company that is already in
the commander's subtree and are not independent placements.

The same definition of a physical placement is used by the final uniqueness
check, preventing false "already assigned" errors caused by commander-host
references while still guaranteeing that one company cannot be placed in two
battalion slots.
