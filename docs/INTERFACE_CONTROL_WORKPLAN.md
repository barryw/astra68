# Remaining Interface Kit controls

Status: source audit, 2026-10-29. Design authority: Controls chapter of
`~/Downloads/Astra Desktop (standalone).html`. Shipped-state authority:
`ndk/include/astra/` and `sw/userspace/interface/src/`, not the HTML's old
implementation notes or historical milestones in `INTERFACE_IMPLEMENTATION.md`.

The design names 50 concepts. **20 are available, six have only part of the
specified contract, and 24 are absent.** “Available” means a shared native
facility exists; it does not claim every application uses it yet. A composed
control does not need a new `AstraControlKind` when existing controls can own
its layout, focus, paint, and state.

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
