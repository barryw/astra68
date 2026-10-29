# Remaining Interface Kit controls

Status: integrated ABI 10 and targeted DE25 trial, 2026-10-29. Design authority: Controls chapter of
`~/Downloads/Astra Desktop (standalone).html`. Shipped-state authority:
`ndk/include/astra/` and `sw/userspace/interface/src/`, not the HTML's old
implementation notes or historical milestones in `INTERFACE_IMPLEMENTATION.md`.

The design names 50 concepts. At audit start, **20 were available, six had only part of the
specified contract, and 24 were absent.** “Available” means a shared native
facility exists; it does not claim every application uses it yet. A composed
control does not need a new `AstraControlKind` when existing controls can own
its layout, focus, paint, and state.

## Integrated checkpoint

All nine work packages below now have shared native implementations in
Interface ABI 10 and the current Display/System contracts. Interface host
tests, sanitizers, library contract, and shipped application cross-build pass
on Beast. Text View and Field use one caller-owned undo manager; Gallery's
editable Text View is bound to it. Fonts and Installer use the shared Open
Panel, reusable directory snapshots, and caller-owned item storage. File
Panel storage can grow through a validated rebind API without an application
writing its internals. Beast Interface Kit host tests, sanitizers, library
contract, shipped-app tests, and NDK `dist-check` pass. Targeted DE25 install
opens Gallery, Atlas, Fonts, and Installer with Interface ABI 10; Fonts and
Installer display the shared Open Panel. Gallery Text View edit/undo/redo
restored exact earlier frame hashes. Gallery's wrapped Choice page scrolls to
its bottom controls after Interface Kit learned to measure a width-constrained
wrapping Container. Evidence is archived under
`/mnt/Documents/astra68/interface-controls-20261029/` on Beast: Gallery
scrolled Choice frame SHA-256 `95bb44cf5437033ccbe0b78529795f2ef15609618843a5df68bd26908e43af1c`,
Fonts main frame `710d594a28c0a2c9fba2e1467d917db506afcdedef87a49b05e72b8354ce0338`,
Fonts Open Panel `3581aa324abefbbb46a7e1b7edd893505a78f168960a1d09b94dbf37b52db863`,
and Installer Open Panel `a1df0e12da2b667b71c56bb735adf940516de61740dc33672d9c2ff8f3960154`.
DE25 Astra, audio host, and remote desktop remain active with zero restarts.
The selected full release image still predates this ABI 10 checkpoint; current
DE25 trial uses targeted, persistent bundle and Kit installation.

Remaining product-level gaps: the shared installed-font chooser does not own
Fonts management actions; font-strike rows lack a per-item disabled state; and
Save-panel accessory choices depend on caller UI. Those are integration and
policy work, not a reason to create one-off controls.

| Group | Available | Partial | Remaining |
| --- | --- | --- | --- |
| Actions (6) | Button, Menu, Context Menu | Button Group (Container + Buttons, no group interaction), Toolbar (flat actions; no groups or overflow) | Split Button |
| Choice (9) | Checkbox, Radio, Switch, Segmented, Tabs, Disclosure | — | Popup, Multi-select Popup, Combo Box |
| Values (8) | Field, Stepper, Slider, Dial | — | Secure Field, Segmented Field, Tag Input, Colour Well |
| Display (7) | Label, Progress | — | Level Meter, Sparkline, Tooltip, Notification, Status Bar |
| Collections (7) | Scrollbar | List (single selection over borrowed static choices, no common collection model) | Table, Tree, icon Grid, Columns, Pagination |
| Surfaces (5) | ScrollView, Splitter | TextSurface (grid and read-only text; incomplete code/flow/find contract), ImageView (hardware blit helper, no retained control), View/custom (Graphics Kit drawing, no retained view contract) | — |
| Containers (5) | Window, Alert | — | Sheet, Form Row, shared Open/Save/Fonts/Colour Panel |
| Interaction (3) | — | — | Drop Target, Drag Ghost, Search Field |

`ASTRA_CONTROL_GRID_VIEW` is a fixed-cell text grid, not the design's icon
collection Grid. `AstraImageViewInfo` renders an already uploaded surface but
does not yet give callers a laid-out image control. Existing Font Manager is
an application, not a shared Fonts panel. Current dialog and popover window
roles do not attach a sheet to one parent window.

## Shared contract before new painters

1. One item identity/selection/scroll contract serves List, Table, Tree,
   icon Grid, Columns, and Pagination. Migrate List; do not keep a second
   selection engine. Preserve stable selection across sort, filter, and row
   updates. Render visible items only.
2. Popup, multi-select popup, Combo Box, Split Button, and toolbar overflow
   use the existing `AstraMenu`, context-menu overlay, command responder chain,
   Field/TextModel, and focus rules. Selection policy varies; menu rendering
   and pointer capture do not fork.
3. Secure, segmented, search, and tag fields use Field/TextModel for editing,
   selection, undo, validation, and keyboard navigation. Secure drawing must
   submit masked glyphs only. Colour Well uses Graphics Kit's pixel-format
   conversion instead of inventing a colour pipeline.
4. Level Meter, Sparkline, and Status Bar reuse theme roles, Progress/Label,
   Container, and draw-list primitives. Tooltip and Notification share one
   compositor overlay lifecycle with placement, timeout, focus, and dismissal
   policy. No application-owned floating mini-window per tooltip.
5. ImageView and custom View join retained layout/damage/clip without CPU
   pixel rendering. Drag Ghost uses protected icon/image sources; Drop Target
   reports accepted operation and precise refusal reason. Typed drag payloads
   should reuse Clipboard representation validation while retaining a distinct
   drag session lifecycle.
6. Sheet attaches to its parent window and blocks only that window. Form Row
   composes Label + control + help/error text. Open, Save, Fonts, and Colour
   panels compose the collection and field controls over Filesystem, Font,
   and Graphics Kits; applications receive results, not panel internals.

Drag/drop implementation reference: Haiku's `src/kits/interface/View.cpp`
(`BView::DragMessage`) starts a drag from a view, while
`src/servers/app/EventDispatcher.cpp` owns cross-window target routing and
the drag image. Astra adopts that ownership split through the opt-in Window
drag events and Display's single typed ACLP session. The Display owns the
hardware-rendered ghost and keeps one read-only document handle; each target
gets a narrowed transferred duplicate on Enter or Drop. The reusable Drop
Target is a Container plus Label, with accepted and refused states following
`Astra Desktop (standalone).html`'s aqua/gray dashed target and precise
read-only refusal text. The Interface Gallery Drag page exercises the
same-window path; other applications use the same Window API across windows.
The source receives FINISHED only after the target acknowledges DROP with
DRAG_COMPLETE; a target doing long work renews its five-minute deadline with
same-token feedback. A dead target or expired deadline fails the session.

Breaking changes during pre-alpha should bump Interface Kit or display
protocol ABI major, update all in-tree clients and manifests together, and
remove superseded paths. New control kinds require behavior that composition
cannot express; each kind uses the existing `AstraUIContext` state, layout,
damage, focus, and render pipeline.

## Work split and dependency order

| Package | Scope and file ownership | Depends on | Acceptance consumer |
| --- | --- | --- | --- |
| C1 Collection foundation | New shared collection model/selection files and NDK header; migrate List in `control.c`; common row geometry and tests | Existing ScrollModel, List | Gallery List; Atlas directory updates without losing selection |
| C2 Collection views | Table, Tree, icon Grid, Columns, Pagination over C1; no independent selection policy | C1 | Atlas grid/list/columns and a tree/table Gallery page |
| P1 Popup family | Popup, multi-select popup, Combo Box, Split Button; menu session and Field composition | Existing Menu, Field, Command | Gallery keyboard/pointer matrix; Atlas Sort menu |
| F1 Field family | Secure Field, Segmented Field, Tag Input, Search Field, Colour Well | Existing TextModel/Field; P1 for suggestions; Graphics Kit for colour | Gallery; Fonts search and colour inspector |
| A1 Toolbar actions | Button Group and command-backed Toolbar groups, labels/icons mode, width fitting, overflow | P1; existing Command/Toolbar | Atlas toolbar at full and minimum widths |
| D1 Local display widgets | Level Meter, Sparkline, Status Bar | Existing Progress/Label/Container/Theme | Gallery; media level and Atlas status |
| D2 Overlay and container lifecycle | Tooltip, Notification, Sheet, Form Row; shared placement and dismissal | Existing display overlays, Alert, Container | Gallery and one real Settings pane |
| S1 Retained surfaces and drag | Complete TextSurface modes/find, retained ImageView/custom View, Drop Target, Drag Ghost | C1 item identity; existing Graphics/Clipboard/Scroll | Atlas cross-volume drag; AstraZ and Terminal text regression |
| X1 System panels | Open, Save, Fonts, Colour shared panels | C2, P1, F1, D2; Filesystem/Font/Graphics Kits | Two unrelated apps use same panel without private picker code |

C1, P1, D1, and D2 can start in parallel in separate worktrees. C1 owns
`control.c` integration until List migration lands; other packages should
prepare separate modules and headers, then integrate one at a time. C2, F1,
A1, and S1 follow their listed dependencies. X1 closes the set. Every package
includes public NDK docs, a Gallery fixture, focused Beast behavior tests,
and a real DE25 consumer trial. Use targeted library/bundle installation for
the edit-test loop; assemble a release image after integrated gates pass.

## Haiku read-only reference

`src/kits/interface/OptionPopUp.cpp`, `MenuField.cpp`, and `PopUpMenu.cpp`
show Popup as a menu-backed field. `ListView.cpp`, `OutlineListView.cpp`, and
`ColumnListView.cpp` separate row selection, tree expansion, and columns.
`ToolTip.cpp` and `ToolTipManager.cpp` centralize tooltip lifecycle.
`SplitView.cpp`, `StatusBar.cpp`, and `ColorControl.cpp` demonstrate composed
layout, status, and colour roles. `src/kits/tracker/FilePanel.cpp` keeps file
panel policy out of applications. Astra adapts these patterns to caller-owned
native controls, capability-backed windows, and hardware draw lists; Haiku
checkout stays read-only.

S1 checkpoint: `src/kits/interface/TextView.cpp` keeps selection as text
offsets, independent of drawing. Astra's shared `TextModel` now finds exact
UTF-8 spans across piece boundaries without changing selection; caller
chooses whether to select and reveal result. Search-bar presentation and
grid history remain separate work.

Gallery page switch: `src/kits/interface/TabView.cpp` `BTabView::Select`
selects one card through `BCardLayout::SetVisibleItem`. Astra currently uses
separate collapsed Container pages; Gallery collapses the old page before
expanding the next, so layout never sees two pages at once. A shared card
container remains preferable when multiple clients need this interaction.

Toolbar icon drawing: `src/kits/interface/Button.cpp` delegates button
appearance to the shared ControlLook; Astra keeps the same shared Button
painter and submits icon segments through Graphics Kit's existing batched
`LINES` command. This uses one draw-list command per icon and reserves payload
for its segments before drawing, so a control-rich page stays within the
retained list limit.

Design-size checkpoint: Haiku's `src/kits/interface/Button.cpp` derives
preferred size from font height and delegates insets to
`src/kits/interface/HaikuControlLook.cpp`. Astra likewise keeps control metrics
in the shared `AstraTheme` rather than scaling Gallery controls separately.
Theme generation 7 uses the resident 16 px AFNT strike for body, title, and
control labels, with 40 px controls, 32 px toolbar actions in a 52 px row,
and 38 px title bars to match the standalone
Controls design at native 1920x1080 resolution. Application-specific text roles
remain separate; the fixed 16 px mono role is unchanged.

Retained surface Gallery fixture: Haiku's `src/kits/interface/View.cpp`
`DrawBitmap` and `Invalidate` separate borrowed image drawing from View damage.
Astra's Layout page uses shared retained View and ImageView in its existing
split panes. The image is painted into a window-owned Graphics Kit surface by
Astraea, borrowed by ImageView, then released when the Gallery window closes.
Window resize changes the custom View model and invalidates that View through
Interface Kit; no application-owned pixel buffer or one-off control is added.

Installer adoption: Haiku's `src/kits/tracker/FilePanel.cpp` keeps the file
panel window and selection policy outside the application. Astra's Installer
uses the shared Open panel for no-argument launch, while the application loads
directories through the shared `directory_snapshot` helper and asks File Type
Kit for package type. Interface Kit's `file_panel_rebind_storage` copies the
current view into larger caller-owned buffers before a large directory change;
selection, search, view mode, and scroll offset remain intact if navigation
fails. On `NAVIGATE` Installer loads the requested directory; on `PICKED` it
copies the chosen path into
caller storage, then reuses its existing Settings pane for review. Direct
`.apkg` launch still enters that review pane.

Gallery scrolling: Haiku's `src/kits/interface/ScrollView.cpp` owns one target
view and lets its scroll bars move that view. Astra's Gallery now composes each
page as one wrapped Container inside a shared ScrollView. Interface Kit
measures width-constrained wrapping rows before setting the scroll extent, so
the viewport reaches controls below the normal window height without a
Gallery-only scrollbar or guessed content height.
