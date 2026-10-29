# Interface Kit

`interface.library.9` is the shared implementation for native window controls,
integer flex layout, shared transient menus, TextSurface presentation, the typed
system clipboard, and per-document undo histories. Applications call its
versioned ELF symbols directly; they do not compile private Interface Kit
sources or speak directly to display and clipboard services.

## Field family

`astra_interface_secure_field_init` keeps the ordinary Field/TextModel editing
contract. The draw list contains only one mask glyph per visible Unicode
scalar, and the control does not emit Copy or Cut actions. The caller owns the
TextModel and must protect or clear its backing storage as appropriate; a
credential service is not yet available. `astra_interface_search_field_init`
uses the same model with search and clear affordances. Escape or the clear
button empties the model and emits its normal text-change action.

`astra_interface_segmented_field_init` composes one Container, one Field per
segment, and separator Labels. Reserve exactly twice the number of segments
in consecutive control slots. Each segment has its own borrowed TextModel;
Tab follows ordinary Interface Kit focus order. `astra_interface_tag_input_init`
composes a wrapped Container, one removable tag Button per tag, and an inline
draft Field. Reserve `tag_count + 2` controls. Tag Button activation requests
removal; draft Field activation requests a new tag. Rebuild the composition
after changing the tag list. The caller owns validation and persistence.

`astra_interface_colour_well_init` creates one activatable, themed swatch.
The Colour Well draws a literal RGB color, current theme role, or resolved
palette slot through Graphics Kit RGB565 conversion and hardware draw-list
commands. Activation is an ordinary `ASTRA_UI_ACTION_ACTIVATE`; a shared
Colour panel can respond when installed. Use `astra_interface_colour_well_set`
to change binding or resolved color and `astra_interface_control_set_text` to
change the displayed binding label.

## Display widgets

Use `astra_interface_level_meter_init` with a borrowed
`AstraLevelMeterModel` containing one value and held peak per channel. Values
and peaks share a nonzero maximum, with `value <= peak <= maximum`. Use
`astra_interface_sparkline_init` with a borrowed `AstraSparklineModel` of
chronological samples and an optional nonzero warning threshold. Both are
noninteractive retained controls, so place them using ordinary flex items and
render the UI context to a hardware draw list. After changing caller-owned
arrays, call `astra_interface_display_refresh` to validate and damage the
control. Keep model storage alive for the control's lifetime.

`astra_interface_status_bar_init` fills three consecutive control slots:
Container, left Label, and right Label. Attach its Container to the desired
parent; the labels are already attached to it. Update either label with
`astra_interface_control_set_text`. The bar's surface, text role, and layout
come from the system theme and shared controls.

## Child context menus

Interface Kit 7.3 adds `astra_interface_context_menu_open_tree` for one level
of hover-activated child rows. Supply top-level command rows first, followed
by child rows grouped by their one-based parent index; zero marks a top-level
row. Command query labels and enabled state are copied when the menu opens.
The display composes the child from the same shared Menu control and returns
the selected child command to the caller's normal command scope. `New > Folder`
on Desktop and in Atlas uses this contract. Existing flat context menus keep
using `astra_interface_context_menu_open`.

## Shared settings panes

Interface Kit 7.2 adds `astra_interface_settings_pane_init`, a composition of
two ordinary Labels and two Containers. It reserves four consecutive IDs
starting at `AstraSettingsPaneInfo.first_id`. The `ASTRA_SETTINGS_PANE_*`
constants are both array indices and offsets from that first ID.

Put those four controls and your application controls in one retained UI array.
Assign content controls' flex `parent_id` to `first_id + ASTRA_SETTINGS_PANE_BODY`
and action buttons to `first_id + ASTRA_SETTINGS_PANE_ACTIONS`. Then initialize
the UI and apply `ASTRA_SETTINGS_PANE_LAYOUT_INIT`. The shell supplies
24-pixel insets and 12-pixel gaps, grows its body and stacks measured actions.
Title and description are nonempty and remain caller-owned. Ordinary event,
focus, rendering, font-change and resize behavior comes from existing controls.

Fonts uses this shell for browsing, import, preview, information and removal.
See `examples/settings_pane.c` for another composition. Settings policy stays
with its owner: system panes call a typed service API; applications use their
private Config Kit grant. The shell owns no settings, converter or renderer.

## Label font previews

Ordinary Labels keep their theme font and fixed role size. To preview an
installed face independently, initialize {c:type}`AstraLabelFont` with
`ASTRA_LABEL_FONT_INIT`, set `gui`, the counted catalog `id`/`id_length`, and
an exact installed `pixel_height`, then assign it to `AstraLabelInfo.font`.
The descriptor is copied; its ID and the Label's text remain caller-owned.

`astra_interface_label_set_font` updates that same shared Label in place.
Measurement and fitting use Display's renderer metrics, and drawing emits
the existing named-face hardware command. Missing strikes return an error;
failed updates keep the previous font. Resetting to `ASTRA_LABEL_FONT_INIT`
restores the theme font and role size. Previews do not change Config Kit's
saved system choice or the process's adopted system-font context.

## Context menus

Interface 5.4 and System 3.3 provide window-owned context menus. The same
{c:type}`AstraMenu` control paints and handles the compositor's ASTRA menu and
application popups. It uses the existing surface and draw-list renderer.

`astra_interface_ui_handle_event` emits `ASTRA_UI_ACTION_CONTEXT_MENU` on a
secondary-button release, the Menu key, or Shift-F10. `control_id` identifies
the deepest visible control under the pointer, including passive and disabled
controls, or the focused control for keyboard input. Zero identifies the
window background. These gestures never activate or toggle the control.
An input-loss event clears pending pointer and keyboard interaction.

Keep one {c:type}`AstraContextMenu` per window. Pass command references and the
existing {c:type}`AstraCommandScope` to `astra_interface_context_menu_open`.
Queries supply current labels, enabled and checked state. Labels are copied
immediately; a zero command reference creates a separator. The compositor
copies and validates the complete counted document before presenting it. The
wire document has the same 1 MiB byte budget as managed draw-list submissions;
rows are allocated on demand, with no separate item-count slab. Rendering uses
the existing compositor replay and grouped glyph runs. A 200-row menu paints
only its visible viewport.

Feed every window event to `astra_interface_context_menu_handle_event` before
ordinary control dispatch. It consumes correlated selection/cancellation
results and invokes through the same command responder chain, which queries
enabled state again. Keep the scope, command references and argument documents
alive and unchanged until the terminal event or successful explicit close.
Close the session before closing its window if it will be reused; otherwise
discard the session together with the window.
Stale results are consumed without invocation. Results are mandatory regardless
of the window's event subscription mask. A full event queue retains a
cancellation with `ASTRA_WINDOW_EVENT_LOSS`; the helper clears the session and
returns `ASTRA_ERROR_IO`. The application may report that cancelled interaction
without terminating its event loop.

Anchors use client coordinates. For a keyboard request, use the focused
control's retained geometry rather than the pointer position. Popup bounds
flip and clamp into the display work area. Arrow, Home/End and Page keys skip
disabled rows and separators; Enter/Space selects, Escape dismisses, and the
wheel scrolls long menus. Outside clicks are consumed, including releases of
buttons still held when a menu closes. Focus stays with the owning window.
Owner loss, geometry/focus changes, pointer grabs and input reset dismiss it.
One popup owns input at a time; another open request returns
`ASTRA_ERROR_BUSY`. Explicit close names its request cookie, so an old close
cannot dismiss a newer popup and succeeds if that old request already ended.
A terminal event with a rendering error clears the session and returns its
translated result (for example `ASTRA_ERROR_NO_RESOURCES`).

Interface Kit 7.3 adds one level of child rows to context menus. Application
menu-strip publication remains future work. The low-level `astra_window_menu_open/close`
transport and direct menu-control APIs are available to framework hosts;
applications normally use the command-backed helper.

```{literalinclude} ../../examples/interface_menu.c
:language: c
:linenos:
```

## Opening a compatible library

The library follows semantic versioning. The ABI major is the breaking-change
boundary, a minor adds backward-compatible functions or behavior, and a patch
fixes behavior without breaking the ABI. A consumer links directly and records
the oldest compatible version in its bundle manifest:

```c
requires interface.library 9 9.0.0
```

The linker records the ABI-major SONAME and required symbol versions. The
process loader rejects a missing or incompatible dependency before application
code runs, so applications need no second discovery path or runtime API table.

## Controls and responsive layout

Controls and {c:type}`AstraUIContext` live in the application. Initialize every
object with its `ASTRA_*_INIT` initializer, create controls with the matching
`astra_interface_*_init` call, assign {c:type}`AstraFlexItem` constraints, then
initialize and lay out the complete array. IDs are nonzero and unique. A child
names its container by `parent_id`; parents must precede descendants in the
array.

Editable Field controls and Terminal use `ASTRA_TEXT_CARET_STANDARD`, the
platform underline caret drawn by TextSurface. Text View can invert its theme
foreground and background with `astra_interface_text_view_set_inverted()`;
status lines use this shared appearance rather than private drawing.
TextSurface centers glyphs and the underline caret within each row height.

The flex engine uses deterministic integer arithmetic and has no child-count or
nesting-depth cap of its own. The caller's control array and memory are the
resource limit. A window frame event passed to `astra_interface_ui_handle_event`
recomputes the entire descendant layout from the new parent extent. Pointer and
keyboard events use the same function and return semantic {c:type}`AstraUIAction`
records. Render only after handling input, then use `astra_interface_ui_damage`
and `astra_window_present_region` to publish the changed rectangle.

Animated controls own their animation state and active-list link; the shared
display-refresh epoch keeps them synchronized. Include
`ASTRA_WINDOW_SUBSCRIBE_VBLANK` in the event mask, retrieve the window-owned
borrowed handle with `astra_window_vblank_wait_handle`, include it in the UI
thread's multi-object wait, and call `astra_interface_ui_vblank` once after
each coalescing pulse. Interface Kit visits only active controls;
indeterminate progress indicators and focused carets create neither timer
threads nor application polling. Dispatch the returned semantic action exactly
like one returned by `astra_interface_ui_handle_event`; this is how
pointer-held steppers report repeat changes without a second callback path.
Stop subscribing when `astra_interface_ui_animations_active` becomes false.
`astra_window_close` closes the vblank handle with the rest of the window
state.

Labels, buttons, checkboxes, radios, switches, sliders, dials, numeric steppers,
segmented selectors, tab strips, disclosures, progress indicators, fields,
lists, and containers share this lifecycle. Label, button, toggle, disclosure, and
choice-item text spans are borrowed, not copied; keep
their UTF-8 bytes alive for the control lifetime. Segmented selectors divide
their frame evenly, change value on pointer release, and support
Left/Up/Right/Down plus Home/End. Steppers accept signed decimal input,
Up/Right/Down/Left plus Home/End, immediate pointer changes, and pointer hold
repeat after 400 ms; unavailable arrows remain visible but muted at a bound.
All value changes return the same `ASTRA_UI_ACTION_VALUE_CHANGED` action.
Numeric actions carry a signed 64-bit value; `decimal_places` is zero for
integer controls and gives the Dial's fixed-point scale. The controls in the
following example require `interface.library.9` version 9.0.0 or newer.

`astra_interface_collection_init` validates caller-owned UTF-8 items and
unique, nonzero 64-bit IDs. Selection uses an item ID; zero means none.
`astra_interface_list_init` binds one List to that model. Empty lists are
valid. Put List inside ScrollView when its rows exceed viewport. Pointer
release selects a row; Up/Down and Home/End change selection and reveal it.
Actions and `astra_interface_control_get_value` retain zero-based row indexes
(`-1` for none); `model.selected_id` gives stable identity.
`astra_interface_list_set_items` replaces array and preserves selection by ID
across reorder/filter, clearing it when selected item disappears. It
remeasures and redraws List; failed updates leave prior model intact. Caller
must keep item storage alive until next successful replacement or UI teardown.

### Popup and toolbar Split Button

`astra_interface_popup_init` creates a single-choice field from borrowed
`AstraChoiceItem` labels. Its retained control handles layout, drawing, focus,
pointer activation, and Enter/Space activation. On `ASTRA_UI_ACTION_ACTIVATE`,
call `astra_interface_popup_open` with a caller-owned `AstraPopup` session.
Feed window events to `astra_interface_popup_handle_event` before the ordinary
UI handler. A correlated menu selection updates the retained choice and emits
`ASTRA_UI_ACTION_VALUE_CHANGED`; dismissal leaves the value intact. Call
`astra_interface_popup_close` before destroying the owning window. Each window
can have one transient menu open at a time.

`astra_interface_split_button_init` composes two ordinary toolbar Buttons: the
primary action and a 28-pixel menu segment. Put them next to each other in a
zero-gap row. Their separate control IDs let the primary action run directly
while the menu segment opens the existing command-backed Context Menu. The
menu segment uses the shared button painter and a hardware-rendered chevron.

Haiku reference: `src/kits/interface/OptionPopUp.cpp` composes `MenuField`
with `PopUpMenu`; `MenuField.cpp` keeps keyboard and pointer opening in the
shared control; `PopUpMenu.cpp` owns transient menu lifetime. Astra adopts
that separation with a retained choice field and a caller-owned, correlated
display-menu session. The display service copies menu rows on open, so a
Popup's borrowed labels remain owned by the choice control.

### Command-backed toolbar groups

`AstraButtonGroup` is the smaller composition for a row of related actions.
Allocate one Container and one Button per `AstraToolbarCommand`, then call
`astra_interface_button_group_init`. The declarations and command scope remain
caller-owned. After UI initialization, call
`astra_interface_button_group_refresh` when command state changes and pass
Button activations to `astra_interface_button_group_handle_action`. It uses
the same command query and invocation contract as toolbar actions.

`AstraToolbarActions` composes the existing toolbar Container, one row
Container per group, ordinary Buttons, and one overflow Button. Declare
contiguous `AstraToolbarGroup` and `AstraToolbarCommand` arrays, plus a
borrowed `AstraCommandScope`. Allocate the number of controls returned by
`astra_interface_toolbar_actions_control_count`, then initialize the
controller and UI context. Call `astra_interface_toolbar_actions_refresh`
when command enabled or checked state changes. Pass Button actions to
`astra_interface_toolbar_actions_handle_action` and correlated menu events
to `astra_interface_toolbar_actions_handle_event` before the ordinary UI
handler. Close its menu before destroying the window.

Call `astra_interface_toolbar_actions_fit` after layout and on window resize.
It first tries icon and label buttons, switches *all* actions to icons if
needed, then folds complete unpinned groups by descending priority. Equal
priorities fold from the right. The returned `minimum_width` includes pinned
groups and the overflow control; a window narrower than that receives
`ASTRA_ERROR_NO_RESOURCES`. Labels remain on the control as stable names in
icon mode. The overflow menu uses the same command query and invocation path
as the main toolbar, with separators between folded groups. Search-field
collapse, group headings and shortcuts in the overflow menu, and persistent
toggle selection require later shared menu and search-field work.

Haiku reference: `src/kits/shared/ToolBar.cpp` uses a group view, ordinary
buttons, separators, and command targets. Astra adopts its composition and
keeps one command responder chain for both visible and overflow actions.
Width fitting is measured against the retained controls before the next
layout pass; no new toolbar painter or application-owned hit testing is used.

```{literalinclude} ../../examples/interface_controls.c
:language: c
:linenos:
```

## Tabs and conditional pages

Tabs are peer navigation, not segmented selectors: each item keeps its natural
width and the selected item owns the ion-cyan underline over one quiet rail.
Pointer selection commits on release inside. Arrow and Home/End keys use the
same value-change action as other selection controls.

Use `ASTRA_CONTROL_COLLAPSED` on page containers. A collapsed container and all
of its descendants leave layout, rendering, hit testing, focus traversal, and
animation together; applications do not maintain separate hidden states in
each subsystem. `astra_interface_ui_set_state` reflows after a collapse change
and clears any interaction owned by a descendant that left the active page. Tabs and
collapsed containers require `interface.library.9` version 9.0.0 or newer.

```{literalinclude} ../../examples/interface_tabs.c
:language: c
:linenos:
```

## Disclosures

A disclosure is a button-like header associated with one sibling container.
The container's `ASTRA_CONTROL_COLLAPSED` bit is the sole expanded-state
authority; the header does not retain a second copy. Pointer activation,
Enter/Space, Left to close, and Right to open all update that container through
the same layout and damage transaction and return
`ASTRA_UI_ACTION_VALUE_CHANGED` with zero or one.

Use a disclosure for optional settings that should reflow in place. The target
must be an ordinary sibling container, which keeps the header reachable while
its body is closed and prevents ambiguous ownership. State remains
application-owned and can be restored by setting the target container before
or after UI initialization. Disclosures require `interface.library.9` version
6.0.0 or newer.

```{literalinclude} ../../examples/interface_disclosure.c
:language: c
:linenos:
```

## Scrolling

{c:type}`AstraScrollModel` is the single source of truth for content extent,
viewport extent, integer offset, line step, and semantic marks. A ScrollView
clips and translates exactly one direct child; use a container as that child
when the viewport contains multiple controls. Separate horizontal or vertical
scrollbars bind to the same model and may live inside or outside the viewport.
No scrollbar is required.

ScrollView itself accepts focus, so a viewport containing only passive content
can scroll by keyboard after a click or Tab. Focused descendants retain the
same keyboard scrolling behavior. This requires version 5.3.0 or newer.

For custom drawing, {c:func}`astra_interface_ui_geometry` returns the retained
frame and effective clip for an indexed control after layout. Use the shared
layout and scrolling geometry rather than duplicating placement or reading
private fields. The query requires version 5.3.0 or newer.

Wheel and keyboard input move in model line units. Nested views consume a
gesture only while they can move in that direction, then chain to the parent.
The 10-pixel track owns a six-pixel thumb with a 24-pixel minimum. Track clicks
page; Option-click jumps; Shift-drag maps one pointer pixel to one content
pixel. Overlay bars fade after 800 milliseconds on the shared vblank clock.

On draw-list surfaces, an isolated scroll transaction reuses retained pixels
with one overlap-safe hardware copy and clips repaint to the newly exposed
strip. Submit `astra_interface_ui_render` to a newly initialized draw list: commands queued
before it select the safe normal repaint because their effect on retained
source pixels is unknown. Any unrelated damage or multiple view mutation in
the same transaction also selects the normal path. These are correctness
fallbacks, not different application APIs.

Scroll models, their bound views, and scrollbars require
`interface.library.9` version 9.0.0 or newer.

```{literalinclude} ../../examples/interface_scroll.c
:language: c
:linenos:
```

## Split views

A split view is a composition, not a second layout system. Place a splitter
between two ordinary sibling panes in a row or column flex container. Dragging
or using the arrow keys updates the adjacent panes' existing flex bases while
honouring both panes' minimum and maximum constraints. Shift plus an arrow
moves one logical pixel; Home and End move to the permitted extremes. The same
live change is reported as `ASTRA_UI_ACTION_VALUE_CHANGED`, and
`astra_interface_control_set_value` restores a saved first-pane extent.

Splitters require `interface.library.9` version 9.0.0 or newer.

```{literalinclude} ../../examples/interface_splitter.c
:language: c
:linenos:
```

## Dials

A dial is a compact scalar editor for angle, gain, pan, and similar values.
Drag vertically; hold either Alt/Option key for quarter-speed fine adjustment.
A bipolar range detents at representable zero, double-click restores the
declared reset value, and Arrow plus Home/End keys use the same immediate
`ASTRA_UI_ACTION_VALUE_CHANGED` path. The value or unit label is an ordinary
separate Label so layout, localization, and validation remain composable.

The caller owns the value domain. `minimum`, `maximum`, `value`, `step`, and
`reset_value` are signed 64-bit fixed-point integers sharing
`decimal_places`. For example, `minimum = -3286`,
`maximum = 732145632765400`, and `decimal_places = 2` represent the exact
range -32.86 through 7321456327654.00 without floating-point work on the
MC68040. The indicator's angular position is normalized internally; 0 through
100 is not part of the public value contract. `astra_interface_control_get_value` and every
value-change action return both the exact integer and its scale.

Dials require `interface.library.9` version 9.0.0 or newer. Their indicator
uses Graphics Kit's shared
clipped line primitive, which replays as a hardware line on draw-list surfaces.

## Pointer images

The window server supplies seven native hardware-pointer images:
`ASTRA_POINTER_SHAPE_DEFAULT`, `ASTRA_POINTER_SHAPE_RESIZE_HORIZONTAL`,
`ASTRA_POINTER_SHAPE_RESIZE_VERTICAL`,
`ASTRA_POINTER_SHAPE_RESIZE_NW_SE`, `ASTRA_POINTER_SHAPE_RESIZE_NE_SW`,
`ASTRA_POINTER_SHAPE_TEXT`, and `ASTRA_POINTER_SHAPE_WAIT`. Use
`astra_window_set_pointer_shape` to select one for a window's content. Interface
Kit's `astra_interface_ui_update_pointer` selects the I-beam for fields and the matching resize
pointer for splitters while preserving pointer capture, so the image does not
change in the middle of a drag. Pass
`ASTRA_POINTER_SHAPE_AUTOMATIC` for that behavior, or pass a concrete shape as
a window-wide override.

Use `astra_window_set_pointer_image` for an application-defined pointer. Supply a
one-through-32-pixel RGBA image, byte pitch, and in-bounds hotspot through
{c:type}`AstraHardwarePointerImage`. The call normalizes it to the native 32 by
32 plane and transfers an immutable copy to the window server; callers may
release or reuse their source pixels after it returns. Selecting another
built-in does not discard the copied custom image, so the window can select
`ASTRA_POINTER_SHAPE_CUSTOM` again without another upload.

The wait image means the content below the pointer is temporarily unable to
accept an interaction. Pass `ASTRA_POINTER_SHAPE_WAIT` to
`astra_interface_ui_update_pointer` only for that state, then return to
`ASTRA_POINTER_SHAPE_AUTOMATIC` when the operation becomes available. A custom
image remains selected the same way by passing `ASTRA_POINTER_SHAPE_CUSTOM`.
Pointer pixels, hotspot, position, visibility, and scene state become visible
together at vertical blank; the MC68040 does not paint or composite the
pointer.

Pointer-image APIs and automatic Interface Kit selection require
`interface.library.9` version 9.0.0 or newer.

## Text models and fields

{c:type}`AstraTextModel` is the shared UTF-8 document core used by fields,
Text View and future editors. It is an allocation-free piece table with an indexed line map;
the caller owns its content and four-byte-aligned metadata arenas. Those arenas
are capacity, not document-format limits. When an edit returns
`ASTRA_ERROR_BUFFER_TOO_SMALL`, query `text_model_replace_requirements`, move
the live document to suitably sized arenas with `text_model_move_arenas`, and
replay the unchanged edit. Failed edits do not alter text, selection, or
generation.

Selections are half-open UTF-8 byte ranges whose anchor and focus must be
Unicode-scalar boundaries. Fields render with the system monospace metrics,
track a horizontal viewport, accept Unicode text events, and emit immediate
text/selection plus semantic copy, cut, paste, and activate actions. Use
`astra_interface_field_replace_selection` for programmatic or pasted text so the field's
single-line invariant is checked in one place. Call `astra_interface_field_refresh` after any
other external model mutation.

One document-owning thread may mutate a model. Borrowed spans returned by
`astra_text_model_read` remain valid only until the next mutation or arena move.

On the physical 70.038 MHz MC68040, 4,096 contiguous single-byte appends
measured 7.320 microseconds per edit. A deliberately maximally fragmented
workload that retained 4,095 pieces measured 656.958 microseconds per edit.
The automated target gate uses measured ceilings of 10 and 800 microseconds
respectively.

```{literalinclude} ../../examples/text_field.c
:language: c
:linenos:
```

## Text selection and clipboard

Interface Kit 7.1 adds selectable, read-only multiline Text View. Initialize
`AstraTextViewInfo` with a caller-owned `AstraTextModel`, then call
`astra_interface_text_view_init`. Place the control as a ScrollView's single
child; bind ordinary scrollbars to that view's existing scroll model when
visible bars are needed. Interface Gallery's Text page demonstrates this
composition and native clipboard integration.

Text View uses TextSurface's fixed-grid hardware glyph path. It draws only
visible lines and columns, expands tabs to eight-cell stops, and displays CRLF
as one line break while preserving original bytes for copying. Long lines
scroll horizontally. Pointer drag, arrows, Shift selection, Home/End,
Ctrl+Home/End, Page Up/Down and Meta+A/C use the same TextModel selection as
single-line fields. Editing, cut and paste are rejected. A keyboard focus
border remains visible at the viewport. Copy actions carry the control ID;
applications copy that model's selected byte range through Clipboard Kit.

After an external document change, call `astra_interface_text_view_refresh`
to remeasure content, update scroll extents and repaint. Existing Field APIs
remain single-line. Text View adds no fields to existing public control/context
structures in its original 7.1 release.

Interface Kit 7.11 adds `astra_interface_text_view_set_wrap_width`. Pass the
available viewport width in pixels to wrap visual lines at word boundaries;
zero restores horizontal scrolling. The caller keeps the same TextModel and
selection. Call the setter after viewport resize, before presenting the next
frame. It remeasures content and updates ScrollView extents. Long words split
at character boundaries; tabs keep eight-cell stops within each visual line.
Haiku reference: `src/kits/interface/TextView.cpp` recalculates line breaks on
width changes (`FrameResized`, `SetWordWrap`) and chooses legal word boundaries
(`CanEndLine`). Astra keeps its existing fixed-cell TextSurface and UTF-8
TextModel contracts.

Interface Kit 8.0 accepts an optional `AstraTextViewInfo.style_at` callback.
It receives each visible UTF-8 scalar's document byte offset and returns
logical foreground/background colors and `ASTRA_TEXT_STYLE_*` bits. Text View
applies appearance while retaining wrapping, selection and copy behavior;
TextSurface sends glyph work to graphics hardware. Application owns style
data and refreshes control after changing it. Haiku reference:
`src/kits/interface/TextView.cpp` stores style runs with text and applies them
during draw. Astra asks document owner for style at offset, avoiding a second
mutable style store inside read-only control. `AstraControl` and
`AstraTextViewInfo` layouts changed; consumers require ABI 8 and rebuild.

Interface Kit 7.9 adds read-only Grid View for applications with fixed cell
layouts and per-cell styles or colors. Supply caller-owned `AstraTextCell`
storage through `AstraGridViewModel`, set its visible rows and columns, then
initialize `AstraGridViewInfo`. The control uses the same TextSurface hardware
glyph path and theme metrics as Text View. Call
`astra_interface_grid_view_refresh` after changing cells or visible rows;
the control remeasures, relayouts, and damages itself. Grid View does not
handle editing or selection. Compose it with Container and ScrollView when
those behaviors are needed.

Interface Kit 9.0 adds a selectable Tree control over the shared
`AstraCollectionModel`. The caller supplies parallel preorder `AstraTreeNode`
entries and a visible-row workspace in `AstraTreeModel`. Parent IDs refer to
the collection's stable item IDs; zero denotes a root. Initialize with
`astra_interface_tree_init`, then use `astra_interface_tree_set_expanded` to
change a branch or `astra_interface_tree_set_nodes` to replace items and the
hierarchy together. The library maintains computed depth and visible rows.
Collapsing a branch with a selected descendant selects the nearest visible
ancestor. Control values and activation actions name visible row indices;
`ASTRA_UI_ACTION_EXPANSION_CHANGED` names the full collection item index.
Tree uses the same focus, selection, clipping, and ScrollView behavior as List.

Pagination in Interface Kit 9.0 composes four controls through
`astra_interface_pagination_init`: Container, Previous Button, Label, and Next
Button. Give `AstraPaginationModel` a C1 collection and a nonzero page size.
Use `astra_interface_pagination_range` for the current half-open item range.
When either button activates, call `astra_interface_pagination_set_page` with
the adjacent page index. The helper updates the label and button availability;
the collection's selected item ID remains unchanged. Empty collections expose
one empty page.

{c:type}`AstraTextSurface` owns presentation policy and caller-provided scratch
storage, not the document. Grid cells, selection, and scrollback remain in the
application. Grid selections are normalized half-open ranges. The extraction
call measures or copies UTF-8 atomically and preserves row boundaries while
trimming selected trailing spaces.

Scrollable text uses composition: ScrollView owns clipping, wheel and keyboard
movement, offsets, and scrollbars; the text control owns its document, layout,
selection, caret, and content extent. Terminal uses the same scroll model over
packed styled grid rows because terminal history is not an editable document.

The system clipboard stores immutable documents with one or more named
representations. Use `text/plain;charset=utf-8` for text and add richer types
to the same write when an application has them. A read returns a stable mapped
snapshot that must be closed; a later writer cannot mutate that snapshot.

```{literalinclude} ../../examples/text_clipboard.c
:language: c
:linenos:
```

## Undo and redo

Create one {c:type}`AstraUndoManager` per document and give it a caller-owned,
four-byte-aligned arena. There is no transaction-count or action-count limit.
The arena byte budget is the only retained-history limit, and
`astra_undo_move_arena` moves live history into a larger allocation when needed.

An action contains a nonzero application operation ID and a serializable
payload with enough before/after state for both directions. A group gives one
or more actions a user-facing UTF-8 name. `astra_undo_perform_group` is the safest
entry point: it verifies capacity first, applies the forward transaction, and
records it only after success. If an action fails, completed actions in that
group are compensated. A compensation failure poisons the history rather than
pretending the document state is known; `astra_undo_clear` is the explicit recovery.

Equal nonzero `coalesce_id` values join only consecutive groups with the same
name inside `coalesce_interval_ns`. Coalescing never crosses a save point.
Call `astra_undo_mark_clean` after a successful save and use `astra_undo_get_state` to
drive dirty state plus labels such as “Undo Typing” and “Redo Typing.” A new
edit after undo discards and wipes the redo branch.

One document thread owns a manager. Do not call it concurrently and do not
re-enter it from the apply callback. The callback must make each individual
action atomic; the manager provides transaction-level ordering and
compensation around those calls.

```{literalinclude} ../../examples/undo.c
:language: c
:linenos:
```

On the physical 69.874 MHz MC68040, the retained 10,000-group acceptance
workload measured 7.546 microseconds to apply and record a group, 3.958
microseconds to undo it, and 3.292 microseconds to redo it. The automated gate
uses a measured 12.5-microsecond ceiling for each phase.

## Icon tiles and inline editing

Interface Kit 7.4 provides `AstraIconTileInfo` and
`astra_interface_icon_tile_render` for themed icon-and-label tiles. Desktop
and Atlas supply frames from their existing Container/ScrollView layout;
the tile applies shared icon sizing, UTF-8 label fitting, clipping, colors,
and placement. `astra_interface_icon_tile_hit` tests the same frame against
its viewport clip. Tile style selects the desktop or client role; applications
still own the item model, selection, commands, and navigation.

`AstraInlineEdit` composes the existing Field, TextModel, and UIContext for a
single-line overlay. Call `astra_interface_inline_edit_start` with initial
text, send key and text events through `astra_interface_inline_edit_event`,
render to its hardware surface, and use `astra_interface_inline_edit_text`
on activation. Escape dismisses. The caller validates and saves the result;
`astra_interface_inline_edit_stop` ends editing after commit or cancel.
Desktop and Atlas use this same control for folder names. Both additions are
exported by `interface.library.9`.

## Image View

Interface Kit 7.10 adds `AstraImageViewInfo` and
`astra_interface_image_view_render` for clipped images in application windows.
The caller supplies a frame from its existing layout, a viewport clip, and a
borrowed Graphics Kit draw-source surface. The view appends an alpha-aware
hardware blit; it never copies or rasterizes pixels on the CPU. Decode images
outside Interface Kit, upload them with `astra_surface_write`, and keep the
source surface live until the referencing draw list retires. This composes
with existing Container and ScrollView controls without an image-specific
layout state machine.

For mixed graphics and text, the Graphics Kit draw list is the ordered canvas.
Give a Container a fixed flex size, call `astra_interface_ui_geometry` to get
its frame and inherited clip, and replay the content in order. Use Image View
for picture commands, `astra_draw_ui_text` or `astra_draw_font_text` for text,
and Graphics Kit rectangle and same-surface blit commands for erase and scroll.
Set the viewport clip before each raw Graphics Kit command after an Image View
call. Image View sets the clip for its own blit, so commands retain the order
in which the application appends them. The Container owns layout; the draw
list owns rendering order and hardware submission.

Haiku reference: `headers/os/interface/View.h` and
`src/kits/interface/View.cpp` use `BView::DrawBitmapAsync` to submit a bitmap
reference and source/destination rectangles to the window server. Astra keeps
that server-owned pixel work, using its existing surface and draw-list
contracts instead of adding a second bitmap protocol.
