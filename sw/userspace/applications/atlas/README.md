# Atlas

Atlas browses mounted directory trees in one window. Interface Kit supplies the
toolbar, Places sidebar, breadcrumb, search field, Grid/List/Columns switcher,
context menus, and status bar. The shared File Panel has a `BROWSE` mode so
Atlas and Open/Save panels share navigation controls. Atlas owns filesystem
snapshots, path history, folder commands, and file launch policy.
File and View menu-bar commands share those same actions. Menu state follows
selection, writable path, current view, sort order, and hidden-file state.

Atlas appends each shared control's draw-list range to one graphics session.
The retained Interface view is limited to 128 commands; Columns with populated
child panes can exceed that limit. Ordered ranges retain one UI context and
one hardware draw submission while allowing the session list to grow.

Back and Forward retain up to 32 visited paths and exact public-volume
references. Atlas switches among HOME, fixed volumes, and public mounts in one
window, pins its active mount's read grant, and rejects history entries if
media is replaced at the same path. Grid, List, and Columns use
the same directory snapshot; columns load child directories as they are
selected. Search filters the current directory. Sort by Name, Size, Kind, or
Modified and Hidden files apply to all views. HOME, RAM0, and writable mounted
volumes support New drawer, file or folder Rename, and per-volume Trash. Copy
and Paste use a typed native clipboard file reference; removable sources carry
their exact volume identity, and copying keeps the source grant pinned. The
shared Filesystem Kit copies between source and destination namespaces without
overwriting an existing destination. App bundles launch as apps, files go to
OpenWith, and mounted volumes opened from Desktop stay bound to their volume
reference so removal can invalidate the window safely.

Haiku read-only references: `src/kits/tracker/Navigator.cpp` for path history,
`src/kits/tracker/ContainerWindow.cpp` for window and Copy/Move commands, and
`src/kits/tracker/PoseView.cpp` for shared directory presentation and selection
state, and
`src/kits/interface/ScrollView.cpp` for viewport and target sizing, and
`src/kits/interface/Bitmap.cpp` for checked backing storage. Astra uses
its native File Panel, command, volume, and graphics contracts rather than
copying Tracker code.

Remaining design work: rich List columns and row expansion, selection preview,
Recent and saved queries, Cut/Move, drag and drop, multiselection, and keyboard
shortcuts. File operations are single-item, and the native clipboard has Copy
and Paste only. These need shared controls or native volume grant transitions,
not Atlas-only widgets.

Draw-list storage must be 4-byte aligned on MC68040. Atlas explicitly aligns
its static view and inline-edit buffers; Graphics Kit rejects misaligned
storage at view initialization. Desktop and Display use the same alignment.

Build on beast with `make -C sw/userspace/apps build/.Atlas.app.stamp`.
Focused checks: `make -C sw/userspace/applications/atlas test` and
`make -C sw/userspace/interface test`. DE25 checks use targeted install of
Atlas and Interface Kit; avoid whole-image rebuilds for app changes.
