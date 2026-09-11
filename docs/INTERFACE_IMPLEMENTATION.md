# Astra interface implementation ledger

Status: active implementation ledger for the design in
`~/Downloads/Astra68 Desktop Design/Astra Desktop.dc.html`.

The normative contracts remain `INTERFACE_SPECIFICATION.md`,
`DESKTOP_AND_UI.md`, and the public NDK headers. This ledger prevents a design
screen from being mistaken for an implementation and records the dependency
order needed to finish the complete design without application-private UI.

## Non-negotiable boundaries

- Public user-facing behavior originates in the NDK and `interface.library`.
- Astra applications and services consume that public ABI; they do not link or
  copy private control painters.
- Ordinary children use retained layout. Absolute coordinates are limited to
  compositor-owned chrome and explicitly anchored overlays.
- Parent extent changes reflow descendants before the frame event returns.
- Layout and interaction paths allocate no memory, use no floating point, and
  have no fixed child-count ceiling.
- The MC68040 submits logical geometry and glyph work. Vega/Astraea perform
  output scaling, composition, glyph expansion, and presentation.
- Beast tests establish correctness. Only the physical DE25 establishes UI
  timing, input latency, rendering throughput, and release acceptance.

## Dependency order

| Layer | Design screens | Required result | State |
|---|---|---|---|
| Theme, surfaces, font strikes | 1a-1d, 3b | semantic generation-5 tokens, integer UI scale, bitmap strikes | partial |
| Retained layout | all | nested row/column/wrap containers, intrinsic measurement, reflow, clipping | complete; physical ABI-2 gate passed |
| Primitive controls | 1d, 4a | label, button, field, check, radio, switch, slider, stepper, popup, combo, segmented, tags, disclosure, progress | label/button/check/radio/switch/slider/progress complete |
| Collection controls | 1d, 2b, 4a | scroll model, scrollbar, splitter, tabs, list, tree, table, grid, columns, toolbar, status, pagination | pending |
| TextSurface | 7a-7b | shared UTF-8 model, grid/code/flow layout, runs, gutters, overlays, caret, selection, undo, find, clipboard, scrollback | grid renderer, selection paint/hit test/extraction, typed clipboard, Terminal copy/paste, and shared undo/redo accepted; find, scrollback, wide cells, and code/flow layout pending |
| Input vocabulary | 6a-6b | keymap-selected Meta labels and immutable system/workspace/app shortcut tiers | input service emits one normalized Meta bit; detection, override, labels, and shortcut tiers pending |
| Command model | 1e, 2a, 6b | stable IDs, typed arguments, state, metadata, asynchronous invocation; shared by menus, palette, toolbar, scripting | pending |
| Menus and palette | 1e, 2a, 3a | persistent application strip, skeleton menus, command palette, system escape shortcuts | pending |
| Telescope | 2a, 3a | Cmd-Space search/launch surface, filesystem change index, ranked results, keyboard selection and open | pending |
| System panels | 4b, 5a-5b | Open, Save, Fonts, and truthful pixel-format-aware Colour services | pending |
| Atlas | 2b | shared sidebar and toolbar with grid, list, and columns views | pending |
| Dock and window policy | 2c | pinned/running/minimized groups, instance state, hold menu, durable ordering | hardware overlap path started; policy UI pending |
| Workspace finish | 1a-1c | volume objects, titlebar treatment B, designed icon strikes, status surfaces | partial |
| Scenes | 3a | discoverable scene ledger and menu switching over fixed 1080p60 timing | hardware contract complete; UI pending |

"Complete" in this table means implemented through the public ABI with unit,
sanitizer, malformed-input, and application-consumer coverage. A layer is not
physically accepted until its representative application is deployed from an
immutable image, visually captured, exercised with real input, and measured on
the DE25 without weakening the existing frame or layout budgets.

## Retained performance gates

- Retained flex reflow remains at or below 10 microseconds per control on the
  physical 70 MHz MC68040. The released nested ABI-2 measurements are 7.704,
  5.091, and 5.783 microseconds per control for 12, 64, and 256 controls.
- Pointer presentation targets one frame; ordinary control, key, menu, and
  window feedback targets two frames.
- Every new nested layout, text-layout, menu-search, and collection-view hot
  path gets a target workload and regression threshold before another layer
  depends on it.
- No benchmark result from the Mac or Beast substitutes for a DE25 result.

## Artifact-specific behavior still to preserve

- A radio group has exactly one selected member; disabled items remain visible
  and leave focus traversal.
- Sliders snap to representable values. RGB565 colour channels expose only
  their real 32/64 stops and show wanted versus stored colour.
- Popup menus open with the current item under the pointer. Multi-select popups
  remain open until explicitly completed.
- Tables right-align machine values; faults color the value, not its row.
- Date, time, and address controls use editable segments rather than reparsing
  free text.
- Unknown progress moves a fixed-width fill; completion greys in place.
- Minimized windows remain addressable from the dock and live Window menu.
- Terminal becomes a TextSurface grid-mode producer without changing its
  visible terminal or POSIX behavior.
- Menus, palette entries, key bindings, automation, and toolbar items invoke
  the same command object.
- Telescope indexes file creation, mutation, rename, and deletion through the
  filesystem notification contract rather than a filesystem-specific watcher.
  Its Linux-side service owns the SQLite index and exposes backend-neutral
  query/result handles to the Astra UI; Cmd-Space opens the centered search
  surface, arrow keys select a result, and Enter invokes the shared open
  command.

`interface.library` 2.5 owns the first TextSurface layer: validated fixed-grid
UTF-8 cells, logical color resolution, background/style runs, synthetic
bold/italic, metric underline/strikeout, blink/hidden/faint/inverse state,
carets, normalized half-open selection painting/hit testing, and hardware-blit
scrolling. Grid selections are normalized and extracted as validated UTF-8
through an explicit source stride, so capacity-padded rows cannot leak into a
visible selection,
with selected row boundaries preserved and trailing grid padding removed;
short destination buffers are left unchanged. Terminal now feeds its parser
cells and pointer coordinates into that public component and contains no
private text painter, hit-test, or selection-extraction math.

The same ABI exposes an immutable typed clipboard document rather than a
Terminal-private text slot. Documents may carry multiple representations,
plain text is validated as UTF-8, writes replace one service-owned generation
atomically, and readers receive reduced read-only area capabilities. Terminal
uses the input service's normalized Meta modifier for Meta-C and Meta-V while
leaving Ctrl-C untouched for zsh. The physical DE25 boot and the full
pointer-selection/Meta-C/Meta-V/zsh execution gate are accepted in release
`07be64339b3bdb58eef906734ed3b857e701856d607f17e6804a4e26a6993ee8`.
Scrollback, find, wide-cell behavior, and
code/flow layout remain unfinished and must land in TextSurface rather than
Terminal.

The same library now owns one reusable undo/redo implementation for every
document-oriented application. A caller supplies an arena and serializable
typed action payloads; there is no independent action, group, or transaction
ceiling. Groups are applied transactionally, partially applied groups are
compensated, a failed compensation poisons the history rather than claiming a
false state, and a larger non-overlapping arena can replace the original
without losing history. Named undo/redo menu state, save-point dirty tracking,
redo-branch invalidation, bounded-time coalescing, payload wiping, and callback
reentrancy rejection are covered by normal, sanitizer, analyzer, and MC68040
build gates. The physical 69.874 MHz MC68040 measured 7.546 microseconds per
apply-and-record group, 3.958 microseconds per undo, and 3.292 microseconds per
redo across 10,000 groups, inside the retained 12.5-microsecond phase gate.

The fixed-grid source cutover is physically accepted at commit `b36a784` in
immutable DE25 release
`5b00754844ed0714b8ecce98fe8042a9d130031ba2014bfd1d49207818fecabb`.
Terminal displayed and executed `echo textsurface-ok`; first output measured
108.041 ms and the frame settled in 205.463 ms. The retained HDMI frame is
`/private/tmp/astra-textsurface-terminal-5b007548.png`, SHA-256
`30f43a3f234208ad570ce0b22cfefc647b293a7f8f85c9a2cfea44d883babf23`.

## Window composition cutover

The MC68040 no longer subtracts opaque windows into a bounded list of visible
rectangles. For each damage rectangle the display service submits the desktop
and every intersecting window bottom-to-top; Astraea's ordered, clipped blits
produce the final overlap and rounded-window result. This removes the former
eight-region policy/fallback from the CPU and keeps z-order policy in the
protected display service while pixel composition remains in hardware.

`interface.library` ABI 2.0 now implements caller-owned nested containers.
Parents precede children; validation builds index links in the caller's control
array, reverse-order intrinsic measurement derives container sizes, and
parent-before-child iterative reflow avoids recursion and depth caps. Ancestor
clips govern damage, hit testing, and rendering. Graphics draw-list ABI 1.1
serializes that clip on every command, and replay validates it before lowering
it into Astraea's clip registers. Host tests cover resize reflow, invalid
hierarchies, clipped input, malformed draw lists, and a 256-level tree.

The remaining cutover is structural rather than cosmetic: replace the fixed
four-entry window table and fixed 4 MiB-per-slot media map with service-owned,
resource-accounted window and surface allocations, then represent the ordered
stack as a validated batched scene description. Admission must fail only when
an accountable kernel, IPC, render-batch, or Media RAM resource is genuinely
exhausted; no small UI-specific window ceiling is permitted.
