# Astra interface implementation ledger

Status: active implementation ledger for the design in
`~/Downloads/Astra68 Desktop Design/Astra Desktop.dc.html`.

The current source package has SHA-256
`1005463fc51826aac33def242c70d9f4122a616352680e93ae44e58f10960603`.
Its written control list is authoritative by name; the adjacent total is not,
because Scrollbar is specified in the ScrollView section but omitted from that
numbered list. Implementation status in the design file is illustrative only;
this ledger and the public ABI are authoritative for shipped state.

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
| Primitive controls | 1d, 4a | label, button, field, check, radio, switch, slider, dial, stepper, popup, combo, segmented, tags, disclosure, progress | label/button/check/radio/switch/slider/progress/field and tabs accepted; segmented, stepper, dial, and disclosure host-certified in current source |
| Collection controls | 1d, 2b, 4a | scroll model, ScrollView, scrollbar, splitter, list, tree, table, grid, columns, toolbar, status, pagination | scroll model/view/bar and splitter accepted; list, tree, table, grid, columns, toolbar, status, and pagination pending |
| TextSurface | 7a-7b | shared UTF-8 model, grid/code/flow layout, runs, gutters, overlays, caret, selection, undo, find, clipboard, scrollback | grid renderer, selection paint/hit test/extraction, typed clipboard, Terminal copy/paste, shared undo/redo, piece-table model, and field accepted; find, scrollback, wide cells, and code/flow layout pending |
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
- Segmented selection and numeric stepping share a 12.5-microsecond semantic
  action gate. The physical 69.416 MHz baselines are 7.003 and 8.926
  microseconds per action respectively.
- Pointer presentation targets one frame; ordinary control, key, menu, and
  window feedback targets two frames.
- Every new nested layout, text-layout, menu-search, and collection-view hot
  path gets a target workload and regression threshold before another layer
  depends on it.
- No benchmark result from the Mac or Beast substitutes for a DE25 result.

## Image presentation boundary

IconView and ImageView depend on one missing Graphics Kit foundation: draw
lists must reference immutable protected image resources. The existing desktop
AICON painter expands indexed pixels into coalesced rectangle fills, consumes
the fixed draw-command budget, and is not a reusable control boundary. It must
not be promoted into Interface Kit.

Once the resource reference exists, IconView owns compact icon presentation
(AICON strike selection, intrinsic size, tint/state, baseline, and adjacent
text composition) while ImageView owns content presentation (fit, fill, crop,
and integer zoom). Buttons, menu items, tabs, fields, and toolbars use the same
icon descriptor and painter as IconView; no control gets a private icon path.

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

The ABI 3.0 release retained the 2.6
allocation-free UTF-8 piece table and single-line field and adds vblank-driven,
control-owned animation. Each animated control owns its state and active-list
link while sharing the display-refresh epoch. A coalescing per-window event
wakes the UI thread at most once per refresh, and the Interface Kit visits only
active controls; applications do not create animation timers or mutate
controls from worker threads. Normal,
sanitizer, analyzer, documentation, and MC68040 build gates pass for the 2.6
text slice. The model stores immutable inserted chunks plus a reverse-grown
line index in caller-owned, replaceable arenas; it has no independent document,
piece, line, or edit-count ceiling. Replacement preflight reports exact content
and metadata requirements, capacity failure is atomic, arena moves compact live
text and wipe occupied old storage, and every selection boundary is validated
as a Unicode-scalar boundary. ABI 3.0 deliberately enlarges the caller-owned
`AstraControl` and `AstraWindow`: controls directly retain animation membership
and phase, and windows directly own their coalescing vblank wait handle. The
obsolete timer tick and separate vblank-window constructor do not exist in the
3.0 table. GUI protocol version 8 returns both window handles atomically.

The append-only ABI 3.1 release added the segmented selector. Its borrowed UTF-8
item model has no widget-specific count ceiling; the available width is divided
equally with integer geometry. Pointer selection commits on release inside,
release outside cancels, and Arrow, Home, and End keys update selection through
the same value-change action. Interface Gallery consumes only the public table
and carries a 10,000-transition target benchmark. Normal, sanitizer, analyzer,
MC68040, library-contract, example, header-contract, and documentation gates
pass; physical DE25 acceptance remains pending.

Current source deliberately advances `interface.library` to ABI 4.0 for the
numeric stepper. Its signed 32-bit range has no UI-specific digit ceiling:
the eleven-byte maximum follows from the value type. Values share the slider's
snapping and semantic value-change machinery. Pointer increment/decrement is
immediate, stays visibly muted at a bound, and repeats after 400 milliseconds;
keyboard Arrow/Home/End and signed decimal entry use the same action. The UI
context owns only the active draft and pointer parts, so controls do not grow a
private text model. `ui_vblank` now returns `AstraUIAction`, allowing repeat to
flow through the sole event/action path instead of polling or a second callback
API. That signature and caller-owned context change require the ABI-major bump.
Normal, sanitizer, analyzer, MC68040, shared-library, and SONAME gates pass.
Diagnostic immutable DE25 release
`1543f41e976943999bfc5447e26a9aa7f62a8f49e7966acaa6540515c44b4bff`
reached stage 8 at 69.416 MHz with zero restarts, rendered the stepper through
the production draw-list and FPGA path, and measured 10,000 public action
transitions at 8.926 microseconds each. Its retained Cam Link specimen is
`/private/tmp/astra-interface-v4-gallery.png`, SHA-256
`2da4732dc0c5b42ed5763ab8e11b8f9d8df0b361199b3002e8325595c1a438d0`.
Physical pointer and hold-repeat acceptance remains pending.

That specimen also proves the current Field painter's embedded mono strike
maps `世界` to replacement glyphs. UTF-8 decoding is intact; Interface Kit must
consume `font.library`'s resolved layout and fallback chain instead of owning
a second embedded-font path. The Gallery retains `世界` as the fallback
regression specimen.

Current source advances the append-only ABI to 4.1 with a shared tab strip and
the generic `ASTRA_CONTROL_COLLAPSED` state. Tabs reuse the segmented
selector's borrowed choice model, selection value, keyboard navigation,
pointer commit, and semantic action path while retaining the design system's
distinct natural-width underline treatment. A collapsed control and its
descendants leave layout, rendering, hit testing, focus, and animation through
the one retained-state transition. Interface Gallery now uses six real tabbed
pages—Input, Choice, Value, Progress, Text, and Layout—instead of a flat widget
pile or a private navigation implementation.

Current source advances the append-only ABI through 4.3 with one caller-owned
scroll model shared by ScrollView and Scrollbar controls. The model has no
control-specific item ceiling and owns extents, integer offsets, line steps,
and borrowed semantic marks. ScrollView clips and translates one direct child;
nested wheel input chains only at an edge. Scrollbars implement proportional
thumbs, paging, Option-jump, Shift fine dragging, marks, and an 800-millisecond
vblank-owned overlay fade. An isolated draw-list scroll uses the existing
overlap-safe hardware copy and repaints only exposed strips. Unrelated or
ambiguous concurrent damage takes the normal repaint path rather than risking
retained-pixel corruption. Host, sanitizer, analyzer, NDK header/example, and
MC68040 shared-library gates pass; the physical acceptance record follows.

Physical DE25 release
`92aa26670bd04ed73c22dad072fb082d08fffccf5bfd8a185fd07df4aff33e43`
accepts the retained path. One wheel notch in the Gallery ScrollView produced
one completed presentation with 53 render commands, down from the 153-command
normal repaint. Render command zero is the overlap-safe framebuffer copy from
`(24,159)` to `(24,143)`, `260x104`; the remaining 16-pixel strip and the
bound scrollbar were repainted. The physical thumb advanced six pixels with
no visible corruption. The Cam Link specimen is
`/private/tmp/astra-gallery-retained-scroll.png`, SHA-256
`313ffb1e26c45b11c97b83c75ff3216da540e3a927f27899aad2d48af48787c7`;
the captured render mailbox is
`/private/tmp/astra-scroll-retained-mailbox.bin`, SHA-256
`56600bba31a6910b4ad78be24b79cdf0f26c0d59e20722fb0539d05a76efeafa`.

Current source advances Interface Kit to ABI 4.4 with one splitter primitive.
A split view is composition, not a second layout system: an existing flex pane,
the splitter, and another flex pane are direct siblings in a non-wrapping row
or column. Existing flex minimum and maximum extents are the only movement
bounds. Pointer capture provides live drag updates outside the divider;
arrows, Shift-arrows, Home, and End provide the same operation from the
keyboard; every retained change emits the shared value-changed action.

The shared layout walk validates and synchronizes a splitter when it reaches
the following sibling. UIs without splitters pay no additional control-array
pass. Host, sanitizer, analyzer, MC68040, NDK header/example, and parser gates
pass. Physical DE25 release
`b83a032916fe6e0bf9fe6704fbc3855923f97e65fb12cb2143ef28e011456cce`
measured 10,000 complete pane/divider/pane reflows in 269.528 milliseconds,
or 26.953 microseconds per action, below the derived 30-microsecond gate. A
20-step physical drag moved the divider and both flex panes without corruption;
the retained Cam Link specimen is
`/private/tmp/astra-splitter-final-dragged.png`, SHA-256
`31ce41e37184a26c46090fd8b70706cf8c94b8803117cfa907a7fe9d9646eeb3`.
The retained trace is `/tmp/astra-splitter-final.flSMgn/ring.bin` on Beast,
SHA-256
`34bf6843d15a8f470f93dd66e65eabc832fcf5e4b28e399597ee3cb3c55e6e0d`.

Current source uses Interface Kit ABI 4.5 and GUI protocol 12 with one
canonical pointer-image path. Window clients select the server-supplied arrow,
horizontal resize, vertical resize, either diagonal resize, I-beam, or wait
image, or transfer a copied RGBA image and hotspot. Interface Kit derives field
and splitter images from its existing hover/capture state and suppresses
duplicate commands. The display service owns custom-image storage and commits
image, hotspot, pointer state, and scene at vblank through the hardware pointer
plane. Window-frame drags update compositor geometry live but defer the client
state and resize events until button release.

Host contract tests cover built-in selection, capture stability, command
coalescing, malformed image rejection, immutable two-handle transfer, and
transactional replacement. The Linux renderer self-test covers every built-in
and a custom image payload. Physical DE25 release
`ab8de7c48ef6059ec25f89711cbfb6e5e42ef5e3acff4fe5556a174d2cc30f02`
reached stage 8 with zero service restarts and rendered the I-beam, both resize
directions, and the default arrow through the hardware plane. Retained Cam Link
frames are `/private/tmp/astra-pointer-desktop.png`,
`/private/tmp/astra-pointer-ibeam.png`,
`/private/tmp/astra-pointer-splitter.png`, and
`/private/tmp/astra-pointer-resize-vertical.png`.

Immutable physical DE25 release
`29b7d36f7687b4fdf577cc6e13a8fdf3064be2947905a190155edfef11d313ca`
boots Interface Kit ABI 3.0 and GUI protocol 8, reaches stage 8 at 69.865 MHz
effective, and remains active with zero service restarts. The published ext4
volume passes read-only `e2fsck` and contains only the 3.0 Interface Kit
provider. Twelve independent Cam Link samples of the indeterminate progress
region produced ten distinct frames, proving control-owned phase advances on
the physical vblank path. The retained specimen is
`/private/tmp/astra-interface-v3.png`, SHA-256
`e738c85fcd3f281b319f7e11daab32161dd0a928c9917b18683d60e691e24371`.

The preceding ABI 2.6 physical release
`ee84a8a336d68cc6469252690e3ac778790225bceda79ea7e7ecd1d425944ebc`
booted the Gallery, rendered editable/error/read-only specimens without
clipping, and remains active with zero restarts. On its 70.038 MHz MC68040,
4,096 contiguous appends measured 7.320 microseconds each and the deliberately
maximally fragmented workload measured 656.958 microseconds per insertion.
The automated target gate rejects regressions above 10 and 800 microseconds
respectively.

The field consumes that public model, fixed AFNT metrics, retained layout,
damage, focus, pointer capture, and normalized Meta events. It implements
scalar-aware caret movement, drag and Shift selection, Home/End,
Backspace/Delete, UTF-8 insertion, horizontal viewport tracking, read-only and
fault states, selection tint, a one-hertz caret, and semantic copy/cut/paste
actions. Programmatic and clipboard replacement use the shared
`field_replace_selection` invariant boundary, so applications do not reparse
single-line text. Interface Gallery is the first NDK consumer and includes
editable, invalid, and read-only specimens.

The shared AFNT import contract also rejects an invisible replacement glyph.
This prevents an unsupported scalar from disappearing even before the planned
Atkinson Hyperlegible Next, JetBrains Mono, and Noto system fallback stack is
installed through the font service.

Current source advances Interface Kit to ABI 5.1 with a reusable Disclosure
control. A disclosure owns only its UTF-8 header and the ID of one sibling
container; that target container's existing collapsed bit remains the sole
expanded-state authority. Pointer activation, Enter/Space, and Left/Right
keyboard navigation use the shared event/action path, atomically reflow the
target descendants, and emit the standard value-change action. Invalid,
non-container, and non-sibling targets are rejected during UI initialization.
Interface Gallery consumes the public 5.1 table and contains no private
disclosure state or painter. Immutable physical DE25 release
`321a882c34ac27f7bf287bc3129eaf1c1fae59aa0384e9c5a8fa8aafde84ae60`
verifies that collapsed points right and expanded points down. Retained Cam
Link captures are `/private/tmp/astra-v52-disclosure-collapsed-clear.png`
(SHA-256
`0631be6fb91731933d4c9dfede60c83c9c491535f953821b7eed4169bc90e362`)
and `/private/tmp/astra-v52-disclosure-expanded.png` (SHA-256
`f5ab52090861331a89ea178af6970a6d5f2a2b8dd3ebdb92681afcc26cf83dc2`).

The same release keeps input delivery nonblocking when a client queue fills.
The input core's existing state-reset and latest-motion recovery is retried by
writable client handles in the service wait set; a slow display client cannot
stall the seat. Gallery consumes all currently queued events before one damage
render, preserving button, key, text, and action order. A 60-sample physical
splitter drag produced nine batches during input and one catch-up batch,
compared with three catch-up batches before this change. Its nine-frame/second
steady-state ceiling and the large-window drag's 15-frame/second ceiling remain
compositor work, not Interface control work.

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

The fixed four-entry table and fixed 4 MiB-per-window media map are gone.
Window metadata grows in committed kernel pages, while exact 64-byte-aligned
content and chrome extents are allocated from the 16..512 MiB Media RAM arena
and reused after close or resize. Tests cover growth beyond one metadata page,
five-window composition, pairwise extent isolation, freed-range reuse, and
physical-arena exhaustion. Admission now fails only when an accountable
kernel, IPC, render-batch, or Media RAM resource is genuinely exhausted.

The remaining compositor cutover is to represent the ordered stack as a
validated batched hardware scene description.
