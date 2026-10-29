# Interface display widgets

Level Meter and Sparkline are retained, noninteractive Interface Kit controls.
They use the same `AstraUIContext` layout, clipping, state, damage, and
hardware draw-list pipeline as Progress. Their caller-owned models make data
refresh explicit; `astra_interface_display_refresh` validates changed values
and marks the control's frame damaged. The CPU computes rectangle geometry;
the graphics hardware draws every pixel. A meter channel fills bottom-up and
shows its held peak. A sparkline draws one bar per visible sample, newest at
the right, with values at or above the warning threshold in the theme's fault
color. Dense histories show the most recent samples that fit at two pixels per
sample.

Status Bar is a composition of one themed Container and two Labels. Its
container owns a centered row layout with space between labels; callers can
attach it to any parent using the ordinary flex item API. Text changes use
`astra_interface_control_set_text`, so measurement and damage remain shared.

The design reference is `~/Downloads/Astra Desktop (standalone).html`,
Controls chapter, “LEVEL METER · SPARKLINE”. Haiku was reviewed read-only:
`~/Git/haiku/src/kits/interface/StatusBar.cpp` keeps value, labels, and
invalidations together, while `Slider.cpp` validates a bounded value before
redraw. Astra adapts those responsibilities to borrowed native models and
draw-list commands; the informational status strip composes existing controls.

Interface Gallery exercises two audio channels, a history with a missed-sample
fault bar, and a status strip. Beast host tests cover model validation,
geometry, threshold color, composition, and damage. The cross-built library
and Gallery bundle link. The dedicated model pointer changes `AstraControl`'s
layout and must be released in the unified Interface Kit ABI 9 integration.
