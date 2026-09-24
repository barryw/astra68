# Interface Kit

`interface.library.5` is the shared implementation for native window controls,
integer flex layout, TextSurface presentation, the typed system clipboard, and
per-document undo histories. Applications call its versioned ELF symbols
directly; they do not compile private Interface Kit sources or speak directly
to the display and clipboard services.

## Opening a compatible library

The library follows semantic versioning. The ABI major is the breaking-change
boundary, a minor adds backward-compatible functions or behavior, and a patch
fixes behavior without breaking the ABI. A consumer links directly and records
the oldest compatible version in its bundle manifest:

```c
requires interface.library 5 5.0.0
```

The linker records the ABI-major SONAME and required symbol versions. The
process loader rejects a missing or incompatible dependency before application
code runs, so applications need no second discovery path or runtime API table.

## Controls and responsive layout

Controls and {c:type}`AstraUIContext` live in the application. Initialize every
object with its `ASTRA_*_INIT` initializer, create controls through the library
table, assign {c:type}`AstraFlexItem` constraints, then initialize and lay out
the complete array. IDs are nonzero and unique. A child names its container by
`parent_id`; parents must precede descendants in the array.

The flex engine uses deterministic integer arithmetic and has no child-count or
nesting-depth cap of its own. The caller's control array and memory are the
resource limit. A window frame event passed to `ui_handle_event` recomputes the
entire descendant layout from the new parent extent. Pointer and keyboard
events use the same function and return semantic {c:type}`AstraUIAction`
records. Render only after handling input, then use `ui_damage` and
`window_present_region` to publish the changed rectangle.

Animated controls own their animation state and active-list link; the shared
display-refresh epoch keeps them synchronized. Include
`ASTRA_WINDOW_SUBSCRIBE_VBLANK` in the event mask, retrieve the window-owned
borrowed handle with `window_vblank_wait_handle`, include it in the UI thread's
multi-object wait, and call `ui_vblank` once after each coalescing pulse.
Interface Kit visits only active controls; indeterminate progress indicators
and focused carets create neither timer threads nor application polling.
Dispatch the returned semantic action exactly like one returned by
`ui_handle_event`; this is how pointer-held steppers report repeat changes
without a second callback path. Stop
subscribing when `ui_animations_active` becomes false. `window_close` closes
the vblank handle with the rest of the window state.

Labels, buttons, checkboxes, radios, switches, sliders, dials, numeric steppers,
segmented selectors, tab strips, disclosures, progress indicators, fields,
and containers share this lifecycle. Label, button, toggle, disclosure, and
choice-item text spans are borrowed, not copied; keep
their UTF-8 bytes alive for the control lifetime. Segmented selectors divide
their frame evenly, change value on pointer release, and support
Left/Up/Right/Down plus Home/End. Steppers accept signed decimal input,
Up/Right/Down/Left plus Home/End, immediate pointer changes, and pointer hold
repeat after 400 ms; unavailable arrows remain visible but muted at a bound.
All value changes return the same `ASTRA_UI_ACTION_VALUE_CHANGED` action.
Numeric actions carry a signed 64-bit value; `decimal_places` is zero for
integer controls and gives the Dial's fixed-point scale. The controls in the
following example require `interface.library.5` version 5.0.0 or newer.

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
each subsystem. `ui_set_state` reflows after a collapse change and clears any
interaction owned by a descendant that left the active page. Tabs and
collapsed containers require `interface.library.5` version 5.0.0 or newer.

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
or after UI initialization. Disclosures require `interface.library.5` version
5.2.0 or newer.

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

Wheel and keyboard input move in model line units. Nested views consume a
gesture only while they can move in that direction, then chain to the parent.
The 10-pixel track owns a six-pixel thumb with a 24-pixel minimum. Track clicks
page; Option-click jumps; Shift-drag maps one pointer pixel to one content
pixel. Overlay bars fade after 800 milliseconds on the shared vblank clock.

On draw-list surfaces, an isolated scroll transaction reuses retained pixels
with one overlap-safe hardware copy and clips repaint to the newly exposed
strip. Submit `ui_render` to a newly initialized draw list: commands queued
before it select the safe normal repaint because their effect on retained
source pixels is unknown. Any unrelated damage or multiple view mutation in
the same transaction also selects the normal path. These are correctness
fallbacks, not different application APIs.

Scroll models, their bound views, and scrollbars require
`interface.library.5` version 5.0.0 or newer.

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
`control_set_value` restores a saved first-pane extent.

Splitters require `interface.library.5` version 5.0.0 or newer.

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
100 is not part of the public value contract. `control_get_value` and every
value-change action return both the exact integer and its scale.

Dials require `interface.library.5` version 5.0.0 or newer. Their indicator
uses Graphics Kit's shared
clipped line primitive, which replays as a hardware line on draw-list surfaces.

## Pointer images

The window server supplies seven native hardware-pointer images:
`ASTRA_POINTER_SHAPE_DEFAULT`, `ASTRA_POINTER_SHAPE_RESIZE_HORIZONTAL`,
`ASTRA_POINTER_SHAPE_RESIZE_VERTICAL`,
`ASTRA_POINTER_SHAPE_RESIZE_NW_SE`, `ASTRA_POINTER_SHAPE_RESIZE_NE_SW`,
`ASTRA_POINTER_SHAPE_TEXT`, and `ASTRA_POINTER_SHAPE_WAIT`. Use
`window_set_pointer_shape` to select one for a window's content. Interface
Kit's `ui_update_pointer` selects the I-beam for fields and the matching resize
pointer for splitters while preserving pointer capture, so the image does not
change in the middle of a drag. Pass
`ASTRA_POINTER_SHAPE_AUTOMATIC` for that behavior, or pass a concrete shape as
a window-wide override.

Use `window_set_pointer_image` for an application-defined pointer. Supply a
one-through-32-pixel RGBA image, byte pitch, and in-bounds hotspot through
{c:type}`AstraHardwarePointerImage`. The call normalizes it to the native 32 by
32 plane and transfers an immutable copy to the window server; callers may
release or reuse their source pixels after it returns. Selecting another
built-in does not discard the copied custom image, so the window can select
`ASTRA_POINTER_SHAPE_CUSTOM` again without another upload.

The wait image means the content below the pointer is temporarily unable to
accept an interaction. Pass `ASTRA_POINTER_SHAPE_WAIT` to
`ui_update_pointer` only for that state, then return to
`ASTRA_POINTER_SHAPE_AUTOMATIC` when the operation becomes available. A custom
image remains selected the same way by passing `ASTRA_POINTER_SHAPE_CUSTOM`.
Pointer pixels, hotspot, position, visibility, and scene state become visible
together at vertical blank; the MC68040 does not paint or composite the
pointer.

Pointer-image APIs and automatic Interface Kit selection require
`interface.library.5` version 5.0.0 or newer.

## Text models and fields

{c:type}`AstraTextModel` is the shared UTF-8 document core used by fields and
future editors. It is an allocation-free piece table with an indexed line map;
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
`field_replace_selection` for programmatic or pasted text so the field's
single-line invariant is checked in one place. Call `field_refresh` after any
other external model mutation.

One document-owning thread may mutate a model. Borrowed spans returned by
`text_model_read` remain valid only until the next mutation or arena move.

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
`undo_move_arena` moves live history into a larger allocation when needed.

An action contains a nonzero application operation ID and a serializable
payload with enough before/after state for both directions. A group gives one
or more actions a user-facing UTF-8 name. `undo_perform_group` is the safest
entry point: it verifies capacity first, applies the forward transaction, and
records it only after success. If an action fails, completed actions in that
group are compensated. A compensation failure poisons the history rather than
pretending the document state is known; `undo_clear` is the explicit recovery.

Equal nonzero `coalesce_id` values join only consecutive groups with the same
name inside `coalesce_interval_ns`. Coalescing never crosses a save point.
Call `undo_mark_clean` after a successful save and use `undo_get_state` to
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
