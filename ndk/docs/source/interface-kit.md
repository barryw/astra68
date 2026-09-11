# Interface Kit

`interface.library` is the shared implementation for native window controls,
integer flex layout, TextSurface presentation, the typed system clipboard, and
per-document undo histories. Applications call the exported table from
{c:type}`AstraInterfaceLibraryV2`; they do not compile private Interface Kit
sources or speak directly to the display and clipboard services.

## Opening a compatible library

The library follows semantic versioning. The ABI major is the breaking-change
boundary, a minor adds fields to the end of the export table, and a patch fixes
behavior without changing the table. A consumer records the oldest minor and
table extent it actually uses:

```c
AstraLibraryHandle *handle = OpenLibrary(
    ASTRA_INTERFACE_LIBRARY_NAME, ASTRA_INTERFACE_LIBRARY_VERSION);
const AstraInterfaceLibraryV2 *interface =
    handle == NULL ? NULL : handle->exports;

if (!astra_interface_library_supports(
        interface, 5u, ASTRA_INTERFACE_LIBRARY_2_5_SIZE)) {
    if (handle != NULL) CloseLibrary(handle);
    return ASTRA_ERROR_NOT_PRESENT;
}
```

Do not compare `structure_size` with `sizeof(AstraInterfaceLibraryV2)` unless
the program truly uses every API in the newest header. An application built
against a newer NDK can continue to run with an older compatible 2.x library
when it uses only that older extent. Its bundle manifest must declare the same
minimum semantic version.

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

Labels, buttons, checkboxes, radios, switches, sliders, progress indicators,
and containers share this lifecycle. Text spans are borrowed, not copied; keep
their UTF-8 bytes alive until the control is replaced or `control_set_text`
installs another span.

```{literalinclude} ../../examples/interface_controls.c
:language: c
:linenos:
```

## Text selection and clipboard

{c:type}`AstraTextSurface` owns presentation policy and caller-provided scratch
storage, not the document. Grid cells, selection, and scrollback remain in the
application. Grid selections are normalized half-open ranges. The extraction
call measures or copies UTF-8 atomically and preserves row boundaries while
trimming selected trailing spaces.

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
