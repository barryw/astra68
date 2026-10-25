# Desktop folders

Desktop user content lives in `HOME:Desktop` (`/home/Desktop`). It is ordinary
filesystem data on the writable home volume. Mounted volume icons are virtual
views of the Volume catalog; they are never copied into that directory.
Installed application bundles remain under `APPS:` and do not automatically
become Desktop files. The Applications drawer opens Atlas at `/apps`.
Application organization can later use explicit references to bundles without
moving the bundles themselves. Dragging bundles into Desktop folders is outside
the current folder milestone.

Desktop creates `HOME:Desktop` only if absent. Its blank-space context menu
uses the shared Interface Kit menu control for `New > Folder`. Numbered names
start at `Folder1` and skip collisions. The new folder's name opens in the
shared Field-based inline editor. Enter commits, Escape cancels; invalid or
existing names leave the editor active and show an exact error. Folder icon
context menus provide Rename and Delete. Delete moves the folder and all its
contents to `HOME:.Trash` under a unique name. Atlas offers the same actions
inside Desktop folders, and opens a child folder in an Atlas window.

Interface Kit 7.4 owns the inline editor's Field, TextModel, focus, event
dispatch and hardware draw surface. It also owns icon tile painting, label
fitting and viewport-aware hit testing for both Desktop and Atlas. Desktop
and Atlas supply their existing grid/container geometry, icons, labels and
folder commands; each retains its filesystem and selection policy. This keeps
common interaction and visual metrics in one library without moving folder
state into a generic control.

The helper validates a single UTF-8 path component before creating or
renaming. Destination reservation prevents ordinary rename collisions; native
VFS rename remains the sole state-changing operation. Home permissions come
from the existing Startup assign and grant contract. Read-only volume browsing
retains its separate exact Volume root grant.

Haiku reference reviewed before implementation:

- `~/Git/haiku/src/kits/tracker/DesktopPoseView.cpp`,
  `InitDesktopDirentIterator`: merge persistent Desktop entries with virtual
  volume icons. Astra adapts that split to its native Volume catalog.
- `~/Git/haiku/src/kits/tracker/PoseView.cpp`, `NewFolder`: create real folder,
  select it, then start inline name edit. Astra composes the existing Field and
  TextModel controls for that editor.
- `~/Git/haiku/src/kits/tracker/TextWidget.cpp`, `StartEdit`/`StopEdit`:
  place an inline editor over a selected item, select its initial text and
  return focus to the item view when editing ends. Astra keeps this as an
  Interface Kit composition and leaves persistence to the caller.
- `~/Git/haiku/src/kits/tracker/Pose.cpp` and `PoseView.cpp`: item data stays
  separate from rendering and pointer geometry. Astra's shared icon tile uses
  application-supplied frames from the existing Desktop grid or Atlas controls.
- `~/Git/haiku/src/kits/tracker/ContainerWindow.cpp`,
  `SetupNewTemplatesMenu`, and `TemplatesMenu.cpp`: use a parent `New` menu
  with child actions. Astra extends its existing shared Menu control and
  window-owned context menu wire contract rather than adding an app-local
  popup implementation.

## Physical DE25 qualification

On 2026-10-25, release
`44c0675b54a2506a3a6f236938bbf9a6d5cf602195a9f55a9fb2adc7bf2cdd33`
passed a physical RFB-driven UI gate. Blank Desktop right-click hovered
`New > Folder`; `Folder1` appeared in inline edit mode. Enter committed
`DE25VerifyStart`; its context menu renamed it `DE25VerifyFinal`. Double-click
opened its empty Atlas root. Atlas created `InsideDE25` in inline edit mode;
Enter committed it, and double-click opened its empty root. After a clean
`astra.service` restart, Desktop still showed `DE25VerifyFinal` and Atlas
still listed `InsideDE25`. Atlas deleted the child and Desktop deleted the
parent. Both icons disappeared. Native Terminal confirmed the two expected
Trash names, `InsideDE25-1` and `DE25VerifyFinal-1`; only those empty test
directories were then removed through the existing storage-provider mount.
Native Trash listing was empty, Desktop matched its original layout, release
manifest verification passed, service was active with zero restarts, and the
retained panic log did not change.

The temporary RFB probe initially imported `de25-rfb.py` from the immutable
release tree and Python wrote `bin/__pycache__/de25-rfb.cpython-310.pyc`.
Release verification correctly refused startup on restart. We stopped the
service, removed only that generated file and its empty directory, verified
the release manifest, and set `sys.dont_write_bytecode` in the temporary probe
before resuming. This was a test-probe artifact, not a kernel panic or a
Desktop data failure. No product file was changed on the board.
