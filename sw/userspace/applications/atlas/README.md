# Atlas

Atlas browses mounted directory trees in one window. Interface Kit supplies the
toolbar, Places sidebar, breadcrumb, search field, Grid/List/Columns switcher,
context menus, and status bar. The shared File Panel has a `BROWSE` mode so
Atlas and Open/Save panels share navigation controls. Atlas owns filesystem
snapshots, path history, folder commands, and file launch policy.

Atlas appends each shared control's draw-list range to one graphics session.
The retained Interface view is limited to 128 commands; Columns with populated
child panes can exceed that limit. Ordered ranges retain one UI context and
one hardware draw submission while allowing the session list to grow.

Back and Forward retain up to 32 visited paths. Grid, List, and Columns use
the same directory snapshot; columns load child directories as they are
selected. Search filters the current directory. Sort by Name, Size, Kind, or
Modified and Hidden files apply to all views. HOME is writable through Atlas's
existing grant; New drawer, Rename, and Delete-to-Trash operate there. System
and volume grants remain read-only. App bundles launch as apps, files go to
OpenWith, and mounted volumes opened from Desktop stay bound to their volume
reference so removal can invalidate the window safely.

Haiku read-only references: `src/kits/tracker/Navigator.cpp` for path history,
`src/kits/tracker/ContainerWindow.cpp` for window commands, and
`src/kits/tracker/PoseView.cpp` for shared directory presentation, and
`src/kits/interface/ScrollView.cpp` for viewport and target sizing. Astra uses
its native File Panel, command, volume, and graphics contracts rather than
copying Tracker code.

Remaining design work: rich List columns and row expansion, selection preview,
Recent and saved queries, live multi-volume sidebar, and direct file operations
such as copy/move/drag-drop. These need shared controls or native volume grants,
not Atlas-only widgets.

Build on beast with `make -C sw/userspace/apps build/.Atlas.app.stamp`.
Focused checks: `make -C sw/userspace/applications/atlas test` and
`make -C sw/userspace/interface test`. DE25 checks use targeted install of
Atlas and Interface Kit; avoid whole-image rebuilds for app changes.
