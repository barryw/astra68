# Atlas

Atlas browses mounted directory trees in one window. Interface Kit supplies the
toolbar, Places sidebar, breadcrumb, search field, Grid/List/Columns switcher,
context menus, and status bar. The shared File Panel has a `BROWSE` mode so
Atlas and Open/Save panels share navigation controls. Atlas owns filesystem
snapshots, path history, folder commands, and file launch policy.
Interface Kit 12.8 draws shared glyphs for the File Panel's Grid/List/Columns
switcher. This keeps the design's compact icon switcher available in Atlas and
Open/Save panels without an Atlas-only control.

Interface Kit's Breadcrumb caps segment Buttons at 160 px; generic Button
rendering elides long labels. Updating a path temporarily folds Breadcrumb
Buttons so replacement labels cannot fail before layout, then reveals from
the current folder toward root. Atlas uses the shared toolbar fitter after
every directory change, folding older ancestors before their parent/current
pair. The parent button remains reachable at narrow widths. Haiku's read-only
`src/kits/tracker/Navigator.cpp` informed the location/Up composition; Astra
keeps path targets in its existing shared Breadcrumb rather than adding an
Atlas path widget.
File, Edit, and View menu-bar commands share those same actions. Menu state
follows selection, writable path, current view, sort order, and hidden-file
state. They publish through the common Menu control; Atlas owns only the
command names, grouping, and invocation. Haiku's read-only
`src/kits/interface/MenuBar.cpp` and `Menu.cpp` informed that split.
The Volume menu opens the existing Disk Setup app. For a physical mounted
volume, Atlas rechecks its exact catalog identity and uses the shared NDK
volume launch helper to select that source in Disk Setup. Fixed and RAM paths
open Disk Setup without a target. Display places its shared Window heading
before a trailing app Help heading, while preserving the app's menu commands.
The same Volume menu offers Unmount for a removable mounted volume and Eject
for a mounted physical optical disc. Atlas and Desktop link one common volume
action state machine and one common exact-holder dialog. The native Volume
client owns admission, provider retirement, and exact references; Interface
Kit owns menu and dialog controls. If a process retains a file, the dialog
names its PID, command, handle, inode, and path. An intentional successful
Unmount closes that Atlas window. Unexpected device loss keeps its unavailable
view so the user can see why browsing stopped. System and RAM volumes cannot
be removed from Atlas. Haiku's read-only
`src/kits/tracker/ContainerWindow.cpp` informed the menu placement and action
scope; Astra keeps native provider lifecycle and precise holder diagnostics.
Optical Eject checks every mounted partition under one medium for holders,
unmounts those partitions in sequence, then ejects the source. Atlas, Desktop,
and native `unmount --eject` share this lifecycle; Atlas adds no device policy.

Atlas appends shared controls through Interface Kit's generic
`astra_interface_ui_append_control`. A control that exceeds one 128-command
view is clipped into ordered strips and appended to one graphics session.
Grid, List, and Columns share this path; Atlas has no private collection
renderer or batching policy.

Back and Forward retain up to 32 visited paths and exact public-volume
references. Atlas switches among HOME, fixed volumes, and public mounts in one
window, pins its active mount's read grant, and rejects history entries if
media is replaced at the same path. `navigate_to` validates every ordinary
history index against both active count and the 32-slot bound before reading
it; `NAVIGATION_STAY` and `NAVIGATION_PUSH` remain explicit sentinels.
Atlas also binds a mounted volume when launched with a directory path argument,
before recording its first history entry. Fixed paths keep their startup
authority without an extra volume query; mounted paths require the volume
watch and exact read grant. RAM0 uses the existing RAM broker; refresh and
move operations retain the same exact volume identity. Display name and
writability come from one
record-binding helper shared with navigation and volume launch. Grid,
List, and Columns use the same directory snapshot; columns load child
directories as they are
selected. Search filters the current directory. Sort by Name, Size, Kind, or
Modified and Hidden files apply to all views. HOME, RAM0, and writable mounted
volumes support New drawer, file or folder Rename, and per-volume Trash. Copy
and Paste use a typed native clipboard file selection; removable sources carry
their exact volume identity, and copying keeps the source grant pinned. The
shared Filesystem Kit copies between source and destination namespaces without
overwriting an existing destination. App bundles launch as apps, files go to
OpenWith, and mounted volumes opened from Desktop stay bound to their volume
reference so removal can invalidate the window safely.
Filesystem Kit performs bounded, heap-backed tree traversal and staged cleanup,
so deep copies use the same operation from Atlas, native `cp`, and other
clients without consuming Atlas's application stack. Traversal follows valid
path length, with no separate nesting cap. Atlas adds no private directory-copy
walker.
The supervisor resolves static assign aliases for app bundles: opening one
through DH0's physical `apps` directory has the same identity and provider-
rooted grants as opening it through Applications. Trusted bundles retain their
startup policy identity; other static bundles use the shortest resolving
assign path. Atlas passes the selected path unchanged to the native launcher
and has no alias policy.

Symbolic links appear in the shared File Panel and inspector. Opening one
follows its target through Filesystem Kit; a missing target reports an open
error. Selection, copy, rename, and Trash keep the link itself as the object
being changed. Shared folder operations reserve the link's destination before
rename, as they already do for files and directories. Haiku's read-only
`src/kits/tracker/ContainerWindow.cpp` follows links for activation, while
`src/kits/tracker/PoseView.cpp` keeps link identity distinct from target kind.
Drag targets use the shared File Panel's presented path and Filesystem Kit's
followed `stat`: a link to a directory accepts drops, while a link to a file
or a missing target does not. Transfer still validates the destination before
writing. Haiku's read-only `src/kits/tracker/Model.cpp` (`IsDropTarget`) and
`PoseView.cpp` (`CanHandleDragSelection`) informed the target-kind check;
Atlas uses no separate row geometry or link resolver.

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
Interface Kit also applies collection selection on right-click: clicking a
selected item retains the multi-selection and makes that item primary; clicking
an unselected item selects it alone. Atlas resolves the path and builds file
commands without duplicating this gesture. Haiku's read-only
`src/kits/tracker/PoseView.cpp` right-click handling informed this shared
selection rule.
File Panel also owns Places selection: navigation and sidebar refresh choose
the longest matching path component prefix. Atlas only supplies current volume
rows and directory path.
Places section headings and row glyphs use the shared Collection presentation
contract. Shared List skips headings for pointer, keyboard, range selection,
activation, and context actions; File Panel validates presentation against
Place paths. Atlas supplies labels, paths, and glyph choices without a private
sidebar control or renderer. Haiku's read-only
`src/kits/tracker/FavoritesMenu.cpp` uses titled separators for grouped places;
Astra adapts that grouping to native List rows and Astraea draw commands.
Atlas's View menu and Ctrl/Command+Alt+S call the shared File Panel Places
visibility API. Hiding Places keeps its rows and selection while the directory
view expands; showing it restores the same sidebar. Haiku's read-only
`src/kits/tracker/ContainerWindow.cpp` (`ShowNavigator`) informed the
show/hide plus relayout pattern.

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
ordinary user files. Right clicking a saved search offers Open, Rename Saved
Search, and Remove Saved Search. Rename uses the shared inline editor and
Filesystem Kit's synced, atomic label replacement. Shared File Panel maps
Places context hits; Atlas reuses its saved-query navigation and Filesystem Kit
file operations. Other Places rows cannot open a
folder context menu. Search remains a filter of the current directory, not a
recursive or indexed query. Haiku read-only references:
`src/kits/tracker/FindPanel.cpp` stores saved queries as filesystem items and
`src/kits/tracker/QueryPoseView.cpp` binds query items to results. Astra uses
its native Filesystem Kit and File Panel, with no Atlas-only search control.

Drag/drop uses display-owned ACLP documents with the same Filesystem Kit VFS
selection payload as Copy/Paste. System Kit finds typed representations in
either document; Interface Kit supplies pointer and row hit testing, the drag
threshold, and File Panel directory resolution for Places, Grid, List, and
Columns. Atlas only chooses copy or move, acquires exact volume grants, and
calls Filesystem Kit's existing tree operations. Same-volume drags move by
default; cross-volume drags copy; Alt requests copy. Read-only destinations
refuse the drop before transfer. Haiku's
`src/kits/tracker/PoseView.cpp` (`FindDragNDropAction` and
`CanCopyOrMoveForeignDrag`) informed the action/target split without adopting
Tracker-specific messages or UI geometry.

Recent Items uses Filesystem Kit 15.9 for one private HOME store of validated,
synced records. Atlas records successful opens and shows an owned snapshot in
Interface Kit's Table and ScrollView, using File Panel's shared content switch
to preserve List, Grid, or Columns state. Removable records retain exact volume
identity and disappear when that incarnation is gone. Atlas owns only the
opened-path policy and Table binding. Opening a folder records the stable
current directory after File Panel navigation, since its action target is
mutable. Filesystem Kit's document storage and stack budget gate also cover
saved queries. Haiku read-only references: `src/servers/registrar/RecentEntries.cpp`
for centralized recent-entry state and `src/kits/tracker/PoseView.cpp` for
opening entries through the same file actions.
The Table borrows Recent Items rows and strings until `recent_enter` replaces
its binding or Atlas closes. Sidebar Places refresh owns only its own rows and
snapshots; it must not release the Recent Items binding. Atlas releases the
Recent Items binding during process cleanup. When Recent is active,
Atlas restores that virtual Place through the shared List selection API after
refresh. No separate Recent Items control or selection renderer is used.

Interface Kit 12.7 binds the shared Outline Table to File Panel's directory
outline model. It composes Table columns, sorting, C1 selection, and Tree
disclosure drawing. File Panel owns stable row IDs, visible projection,
collapse state, and nested path lookup. Atlas loads a child snapshot when
requested and passes its entries to File Panel; its selection, inspector,
context actions, drag/drop, and activation use those shared paths. The shared
collection text-region API also positions the inline rename editor over any
visible List, Table, or Outline Table row. Haiku's read-only
`src/kits/interface/OutlineListView.cpp` informed the separate visible-row
presentation and parent navigation; Astra retains its Table columns.
File Panel's presented-path and presented-directory calls resolve nested List
rows as well as flat Grid, Columns, and Places targets. Atlas uses them for
drag source and destination hits, without its own row-to-directory mapping.

Keyboard bindings use Interface Kit's shared command shortcut dispatcher and
existing responder chain. Atlas owns only its binding table and suppresses
global commands while its shared File Panel Search field has focus. Primary+N
opens a new Home window through the existing application launcher;
Shift+Primary+N creates a drawer in the current writable directory. Copy,
Cut, Paste, Open, Refresh, and Alt+Left/Right use the same actions as menus and
toolbar. Primary+1/2/3 choose List/Grid/Columns through the same responder
commands as the View menu. Menu enable and checked state query those commands;
Atlas has no second view-switch dispatcher.
Save Search, Hidden Files, and Preview also publish through that responder;
toolbar and menu actions share each transition and menu state query.
The toolbar uses Interface Kit's Toolbar Actions, shared New Folder and Open
glyphs, command state, overflow, and peer fitting. Toolbar Actions measures
the same Popup, Checkbox, Breadcrumb, Segmented, and Search Field controls
used elsewhere; whole command groups fold first, then lower-priority peers.
Folded sort, hidden-file, preview, and view controls remain in the View menu.
If pinned controls cannot fit, Atlas requests the measured minimum width from
the window server. No Atlas-specific toolbar renderer or substitute control
exists. Haiku's read-only `src/kits/tracker/Navigator.cpp` uses
`BToolBar::AddAction` for navigation; `src/kits/shared/ToolBar.cpp` retains
each action's icon, tooltip, and label separately. Astra adapts both patterns
to its native command scope and retained Interface Kit controls.
The exact-volume source setup for Paste remains local because it pins a
clipboard reference to one removable incarnation; process namespace lookup
could otherwise select a replacement mounted at the same spelling.

Draw-list storage must be 4-byte aligned on MC68040. Atlas explicitly aligns
its static view and inline-edit buffers; Graphics Kit rejects misaligned
storage at view initialization. Desktop and Display use the same alignment.

Build on beast with `make -C sw/userspace/apps build/.Atlas.app.stamp`.
Focused checks: `make -C sw/userspace/applications/atlas test` and
`make -C sw/userspace/interface test`. DE25 checks use targeted install of
Atlas and Interface Kit; avoid whole-image rebuilds for app changes. Atlas's
bundle manifest launches `/apps/Atlas.app/bin/m68k-68040/Atlas`; targeted
installs must use that exact guest path. `dev-deploy-de25.py` validates staged
application targets before board mutation.
