# Astra OS Interface System

## Direction

Astra OS is the user-facing environment of Astra 68: a fast, elegant,
single-user 68040 workstation whose restraint comes from deliberate hardware
and software boundaries. It should feel precise and immediate rather than
nostalgic or generic. The signature is the **ion signal rail**: cyan for active
and affirmative state, amber for attention, and muted red for faults, set
against deep system chrome and lunar client surfaces.

This system is independent of the host-side AstraVM instrument. Shared product
vocabulary does not imply shared component implementations or token values.

## Source of truth

- Public UI contracts live in the versioned NDK.
- `interface.library` owns retained controls, layout, interaction, damage, and
  rendering behavior. Astra OS applications consume that public ABI.
- The display service owns windows and composition. Applications submit
  validated draw lists; they do not paint private copies of system controls.
- Semantic roles come from `ASTRA_THEME_SYSTEM_INIT`. Applications do not
  embed colors, radii, spacing, or control geometry.

## Color and depth

- Deep navy canvas and near-black system chrome frame lunar client surfaces.
- Graphite raised controls and inset surfaces establish hierarchy through
  small lightness shifts.
- Borders-only depth: no shadows, glow, glass, or decorative gradients.
- Ion cyan communicates focus, selection, primary action, and active state.
- Amber communicates warning or pending attention. Fault red communicates
  destructive action and error.
- Maintain primary, secondary, tertiary, and muted text roles, plus separate
  client-text roles where light surfaces require dark text.
- Use soft, emphasis, client, and focus borders according to boundary
  importance.

## Geometry and scaling

- The spacing base is 4 logical pixels.
- Theme generation 5 records an integer UI scale. Theme geometry is expressed
  in logical pixels at that scale.
- Default control height is 28, horizontal padding 14, control radius 8,
  border width 1, and focus width 2 logical pixels.
- The MC68040 never scales pixels. The FPGA scales the complete logical scene
  to the fixed 1920x1080x60 output; the enabled hardware pointer remains an
  unscaled native-output plane.

## Retained layout pattern

- Applications declare an `AstraFlexLayout` for the root and each nonvisual
  container plus one `AstraFlexItem` per control; ordinary controls never
  receive application-owned absolute coordinates.
- Layout is deterministic integer row/column flex with padding, gaps, wrapping,
  semantic line breaks, justification, alignment, intrinsic or explicit basis,
  min/max constraints, and weighted grow/shrink.
- Parents precede descendants. Reflow is an allocation-free,
  floating-point-free iterative traversal with no arbitrary child or depth
  ceiling. Parent extent changes trigger automatic descendant reflow;
  same-size frame notifications do nothing.
- Descendants inherit ancestor clipping for damage, hit testing, and painting.
  Graphics draw-list ABI 1.1 carries the clip to Astraea's hardware clip
  registers.
- Interaction state and capture survive reflow. A changed extent damages the
  new parent area once; presenting it must not create a frame/damage feedback
  loop.
- Released nested ABI-2 physical measurements: 7.704, 5.091, and 5.783
  microseconds per control across 12, 64, and 256-control workloads. Reject
  regressions above 10 microseconds per control.

## Controls

### Label

Labels use semantic text hierarchy and intrinsic font measurement. Their
baseline and spacing come from the shared font and theme contracts.

### Button

Buttons support standard, primary, warning, and destructive semantic variants;
normal, hover, pressed, focused, disabled, selected, and error states; pointer
capture with release-outside cancellation; and keyboard activation through
Enter, keypad Enter, or Space.

Tab advances focus and either physical Shift key plus Tab reverses it. Input
modifier bits come from the shared public NDK contract, never private service
copies.

## Text surfaces

- `interface.library` owns reusable grid, code, and flow text presentation;
  Terminal is an escape-sequence producer for grid mode, not a painter.
- All modes share UTF-8 validation, font metrics, styled glyph runs, logical
  colors, caret, selection, clipboard, scrolling, undo, and find behavior.
- Designed bold/italic faces are preferred. Synthetic bold and italic reuse the
  installed glyph source; underline and strikeout use AFNT metrics instead of
  duplicate glyph images.
- The current foundation implements grid runs, style resolution, caret, and
  hardware-blit scrolling. Wide cells and the editing/code/flow layers remain
  pending and must extend this component rather than fork it.

## Validation pattern

- Exercise reusable controls in the native `InterfaceGallery.app` through the
  same NDK, Kits, window events, draw lists, and FPGA renderer used by other
  applications.
- Cover layout arithmetic and edge cases with host tests, ASan/UBSan, GCC
  analysis, MC68040 builds, and a target-side benchmark.
- Accept resize only when the physical renderer completes balanced submissions
  and then becomes idle.
