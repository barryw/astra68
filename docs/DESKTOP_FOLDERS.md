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
- `~/Git/haiku/src/kits/tracker/ContainerWindow.cpp`,
  `SetupNewTemplatesMenu`, and `TemplatesMenu.cpp`: use a parent `New` menu
  with child actions. Astra extends its existing shared Menu control and
  window-owned context menu wire contract rather than adding an app-local
  popup implementation.
