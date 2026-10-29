# Interface Kit visual alignment

Reference: `~/Downloads/Astra Desktop (standalone).html`, Controls chapter,
reviewed 2026-10-29. Its implementation-status prose is historical; current
native controls are listed in `ndk/include/astra/control.h` (Interface ABI 10).

The shared theme and Interface Kit own control appearance. Applications should
compose these controls and use theme roles instead of drawing local variants.
The 2026-10-29 pass compared the design's control gallery with Interface
Gallery on DE25 at 1920×1080. Captures are archived on Beast at
`/mnt/Documents/astra68/ui-design-audit-20261029/`.

| Design surface | Native owner | Alignment checked |
| --- | --- | --- |
| Lunar client, graphite chrome, ion accent | `ndk/include/astra/theme.h` | Neutral client and raised-surface grays now quantize without a blue cast in RGB565. |
| Buttons, toolbar buttons, label | `interface/src/control.c`, `interface/src/toolbar.c` | Theme generation 7 uses 40 px standard controls and 32 px toolbar actions in a 52 px row. Icons and text share Button painting and focus states. |
| Field, Text View | `interface/src/control.c` | Raised field fill, 10 px text inset, focus/error border, native text selection and scroll behavior. |
| Checkbox, radio, switch | `interface/src/control.c` | 16 px indicators, fine check, selected radio ring/dot and 34×18 switch; shared disabled/focus states. |
| Slider, dial, stepper, progress | `interface/src/control.c` | 4 px slider track, round 16 px thumb, neutral progress track, shared value controls. |
| Segmented, tabs, disclosure, list | `interface/src/control.c` | Inset selected segments, tab hover/selection rail, existing list and disclosure states. |
| Scroll View, scrollbar, splitter, grid | `interface/src/control.c` | Neutral scrollbar track, clipped scrolling, shared splitter and grid drawing. |
| Pull-down and context menus | `interface/src/menu.c` | Compact 28 px rows, 4 px frame inset, rounded selection and shared submenu/check marks. |

The design catalogs 50 control and panel concepts. The original source audit
and implementation order are in the
[control workplan](INTERFACE_CONTROL_WORKPLAN.md). Its initial 20/6/24 counts
are a dated baseline: C1, C2, P1, F1, A1, D1, D2, S1, and X1 now have shared
implementations. Several concepts compose existing kinds or use window and
drawing APIs rather than a separate `AstraControlKind`. Real application use,
precise visual comparison, and DE25 trials remain separate acceptance gates.

Haiku read-only references: `src/kits/interface/Button.cpp`, `CheckBox.cpp`,
`RadioButton.cpp`, `Slider.cpp`, `ScrollBar.cpp`, `TextControl.cpp`,
`TabView.cpp`, `StatusBar.cpp`, and `Menu.cpp`. Astra follows their shared-look
and state-driven rendering pattern, adapted to retained hardware draw lists
and the native theme. No Haiku source was copied or modified.
