# Interface Kit visual alignment

Reference: `~/Downloads/Astra Desktop (standalone).html`, Controls chapter,
reviewed 2026-10-29. Its implementation-status prose is historical; current
native controls are listed in `ndk/include/astra/control.h` (ABI 8).

The shared theme and Interface Kit own control appearance. Applications should
compose these controls and use theme roles instead of drawing local variants.
The 2026-10-29 pass compared the design's control gallery with Interface
Gallery on DE25 at 1920×1080. Captures are archived on Beast at
`/mnt/Documents/astra68/ui-design-audit-20261029/`.

| Design surface | Native owner | Alignment checked |
| --- | --- | --- |
| Lunar client, graphite chrome, ion accent | `ndk/include/astra/theme.h` | Neutral client and raised-surface grays now quantize without a blue cast in RGB565. |
| Buttons, toolbar buttons, label | `interface/src/control.c`, `interface/src/toolbar.c` | 28 px buttons, 36 px toolbar, shared states, text and icon geometry match the design's control language. |
| Field, Text View | `interface/src/control.c` | Raised field fill, 10 px text inset, focus/error border, native text selection and scroll behavior. |
| Checkbox, radio, switch | `interface/src/control.c` | 16 px indicators, fine check, selected radio ring/dot and 34×18 switch; shared disabled/focus states. |
| Slider, dial, stepper, progress | `interface/src/control.c` | 4 px slider track, round 16 px thumb, neutral progress track, shared value controls. |
| Segmented, tabs, disclosure, list | `interface/src/control.c` | Inset selected segments, tab hover/selection rail, existing list and disclosure states. |
| Scroll View, scrollbar, splitter, grid | `interface/src/control.c` | Neutral scrollbar track, clipped scrolling, shared splitter and grid drawing. |
| Pull-down and context menus | `interface/src/menu.c` | Compact 28 px rows, 4 px frame inset, rounded selection and shared submenu/check marks. |

The design catalogs 50 control and panel concepts. Current `AstraControlKind`
has 20 native kinds; design-only concepts such as multi-select popup, colour
well, table, tree and standard Open/Save panels are not implemented by this
visual pass. The design's toolbar overflow and per-application presentation
preferences are separate behavior work. Existing controls should stay in
Interface Kit as those features arrive.

Haiku read-only references: `src/kits/interface/Button.cpp`, `CheckBox.cpp`,
`RadioButton.cpp`, `Slider.cpp`, `ScrollBar.cpp`, `TextControl.cpp`,
`TabView.cpp`, `StatusBar.cpp`, and `Menu.cpp`. Astra follows their shared-look
and state-driven rendering pattern, adapted to retained hardware draw lists
and the native theme. No Haiku source was copied or modified.
