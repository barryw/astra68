# Atlas shared toolbar

Atlas uses Interface Kit's `AstraToolbarActions` for New drawer, Open, Delete,
and Refresh. Those buttons query and invoke the same commands as the existing
context menu. Disabled state comes from current directory/selection state;
overflow uses Interface Kit's menu session. The toolbar is rendered into a
hardware draw-list view and appended to Atlas's existing Graphics Kit frame.
Icon tiles and scrolling remain below that row.

Haiku read-only reference: `src/kits/tracker/ContainerWindow.cpp` routes
`kNewFolder` to `PoseView()` and `kOpenParentDir` to `OpenParent()`. Atlas keeps
window commands in one controller and lets Interface Kit own button state,
layout, drawing, and overflow. Atlas currently opens each child directory in a
new process, so in-window Back/Forward history and view switching need a
separate navigation/collection migration before those commands can be shown.
