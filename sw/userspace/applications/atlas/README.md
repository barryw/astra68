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
and Paste use a typed native clipboard file selection; removable sources carry
their exact volume identity, and copying keeps the source grant pinned. The
shared Filesystem Kit copies between source and destination namespaces without
overwriting an existing destination. App bundles launch as apps, files go to
OpenWith, and mounted volumes opened from Desktop stay bound to their volume
reference so removal can invalidate the window safely.

Haiku read-only references: `src/kits/tracker/Navigator.cpp` for path history,
`src/kits/tracker/ContainerWindow.cpp` for window and Copy/Move commands, and
`src/kits/tracker/PoseView.cpp` for shared directory presentation and selection
state, including its default Name, Size, and Modified columns, and
`src/kits/interface/ScrollView.cpp` for viewport and target sizing, and
`src/kits/interface/Bitmap.cpp` for checked backing storage, and
`src/kits/interface/View.cpp` and `MenuItem.cpp` for focus and shortcuts. Astra uses
its native File Panel, command, volume, and graphics contracts rather than
copying Tracker code.

The shared File Panel composes List from Interface Kit's Table control. Rows
show Name, Size, Kind, and UTC Modified columns without an Atlas-only widget;
Grid and Columns still use the same directory snapshot and selection model.
Interface Kit 12.2 supplies the selection inspector, composed from its existing
Container, Image View, and Label controls. Atlas binds the selected entry and
icon, then toggles the shared inspector with Preview. It owns no inspector
renderer or metadata formatter.
Interface Kit ABI 12 includes shared collection multi-selection; ABI 11 added
the table model and File Panel dispose call.
List, Grid, and root Columns bind one caller-owned stable-ID selection set.
Interface Kit owns Ctrl/Command toggle, Shift range, painting, and primary
selection mirroring across views. Atlas owns only storage growth and status
text. Haiku's `src/kits/tracker/PoseView.cpp` (`SelectPoses` and
`MoveOrChangePoseSelection`) supplied the selected-pose list and pivot pattern;
Astra keeps it in its native collection model and stable IDs.
Interface Kit reports context-menu row hits for all collection controls,
including Table's header offset. File Panel maps that row to its source entry
and Columns depth; Atlas uses that mapping for its context menu instead of
maintaining separate geometry logic for each view.
File Panel also owns Places selection: navigation and sidebar refresh choose
the longest matching path component prefix. Atlas only supplies current volume
rows and directory path.

Copy and Paste serialize one or many selected paths through Filesystem Kit's
versioned VFS selection clipboard payload. Filesystem Kit checks payload bounds
and paths. Atlas resolves each selected stable ID through its directory
snapshot, pins exact removable volume identity, then calls the existing
cross-volume tree-copy operation. Move to Trash applies the existing shared
folder operation to each selected entry. Partial failures show that earlier
items succeeded; the directory refreshes afterward. Open and Rename require
one selection. A shared-control focus helper updates the active Grid, List, or
Columns view after folder creation and context selection. Completing Rename
refreshes the directory and clears selection.
Cut marks the same shared clipboard selection as a Move. Paste uses Filesystem
Kit's tree move: rename within one volume; staged copy and source removal across
volumes. It never overwrites a destination. If removal fails after publication,
Atlas tells the user the destination exists and original remains partly or
wholly; successful moves clear completed entries from clipboard and keep
pending entries available after batch failure. Pasting cut item into its own
directory is a no-op. Ctrl+X and the File and context menus call the same
native command. Exact volume references prevent stale media from being moved.
Blank-space context menu uses shared menu hierarchy with root items before
submenus, and clears selection through the active shared collection control.

Haiku read-only references: `src/kits/tracker/PoseView.cpp` (`DoMoveToTrash`)
iterates selected poses; `src/kits/tracker/ContainerWindow.cpp` dispatches
Copy and Move over Tracker selection. Astra keeps selection gestures in
Interface Kit and file operations in Filesystem Kit and common folder code.

Filesystem Kit 15.8 owns durable saved search presets. Atlas saves the current
directory and Search Field's case-insensitive name substring as one user file
under `HOME:/Saved Searches`; each file is synced, then published with a hard
link without replacing another preset. File Panel's existing virtual Place
action rows show saved labels and restore path plus filter. Refresh reloads the
directory and sidebar. Presets survive Atlas restart and can be removed as
ordinary user files. Right clicking a saved search offers Open and Remove Saved
Search. Shared File Panel maps Places context hits; Atlas reuses its saved-query
navigation and Filesystem Kit file operations. Other Places rows cannot open a
folder context menu. Search remains a filter of the current directory, not a
recursive or indexed query. Haiku read-only references:
`src/kits/tracker/FindPanel.cpp` stores saved queries as filesystem items and
`src/kits/tracker/QueryPoseView.cpp` binds query items to results. Astra uses
its native Filesystem Kit and File Panel, with no Atlas-only search control.

Remaining design work: List row expansion, Recent items, saved-search rename,
and drag and drop. These need shared controls or native volume
grant transitions, not Atlas-only widgets.

Keyboard bindings use Interface Kit's shared command shortcut dispatcher and
existing responder chain. Atlas owns only its binding table and suppresses
global commands while its shared File Panel Search field has focus. Ctrl or
Command+C/V/N/O/R and Alt+Left/Right use the same actions as menus and toolbar.
The exact-volume source setup for Paste remains local because it pins a
clipboard reference to one removable incarnation; process namespace lookup
could otherwise select a replacement mounted at the same spelling.

Draw-list storage must be 4-byte aligned on MC68040. Atlas explicitly aligns
its static view and inline-edit buffers; Graphics Kit rejects misaligned
storage at view initialization. Desktop and Display use the same alignment.

Build on beast with `make -C sw/userspace/apps build/.Atlas.app.stamp`.
Focused checks: `make -C sw/userspace/applications/atlas test` and
`make -C sw/userspace/interface test`. DE25 checks use targeted install of
Atlas and Interface Kit; avoid whole-image rebuilds for app changes.
