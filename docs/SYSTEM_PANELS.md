# Shared system panels

Interface Kit owns system panel composition; callers own the window, event loop,
control array, and the selected value. The first implemented panel is Colour.
It uses existing Container, Label, Colour Well, Slider, Field, Segmented,
List, and Button controls.
The Gallery's Fields-page Colour Well opens the shared Colour panel on a Panels
page and applies the result back to that well. No pixel renderer or app-specific
picker was added.

RGB565 channels use exact ranges 0..31, 0..63, 0..31, so every selection is
representable. XRGB8888 channels use 0..255, with opaque preview. Indexed mode
borrows an opaque palette and selects an actual slot. `packed` is the encoded
result, so callers do not infer it from a rounded preview. Cancel does not
apply it. The caller may render `AstraColourPanel` with its ordinary UI context
and pass each `AstraUIAction` to `astra_interface_colour_panel_action`.
The four tabs are Sliders, Field, Theme, and Recent. Field accepts `#RRGGBB`
and displays an error while incomplete; Use requires a valid value. Theme
offers system accent, warning, fault, and canvas roles. Recent is supplied by
the caller through `astra_interface_colour_panel_set_recent`, rather than
maintaining a second palette database. Wanted and Stored wells show quantization
explicitly, with the largest channel difference in RGB units. An indexed
choice uses the nearest palette color with the lowest slot winning ties.

Reference review:

- Haiku `src/kits/tracker/FilePanel.cpp` keeps file selection and window
  behavior in a shared kit panel, not in each application. Astra follows the
  same ownership direction. Its window/message machinery is deliberately
  replaced by Astra's retained controls and ordinary `AstraUIAction` loop.
- Haiku `src/kits/interface/ColorControl.cpp` composes palette and channel
  controls in a reusable kit component. Astra composes existing shared
  controls; exact source format and palette slot remain first-class data.
- Haiku `src/apps/icon-o-matic/generic/gui/panel/color_picker/ColorPickerView.cpp`
  separates color entry modes while keeping one selected color. Astra keeps
  one encoded selection and composes each mode from existing Interface Kit
  controls. The hardware format remains visible because conversion can round.
- Astra design `~/Downloads/Astra Desktop (standalone).html`, Colour panel:
  exact RGB565 channels, Field, Theme, Recent, dual swatches, Cancel and Use.

File panels should grow toward the same navigation and display controls as
Atlas.

The shared Open/Save file panel now consumes `AstraDirectoryEntry` snapshots
from Filesystem Kit, hides dot entries and invalid or unsupported nodes,
builds a List in a ScrollView, and returns a directory
plus leaf name. Its actions distinguish entering a folder, opening a file,
saving a new name, replacing an existing file, and cancelling. Save requires
an editable `TextModel`; it rejects empty, dot, dot-dot, slash, and control
names. The caller performs the actual open/save operation and handles the
overwrite confirmation. Directory replacement uses a second item buffer so
the old List data remains valid if validation or layout fails. Existing Atlas
and Fonts browser code can adopt this panel without duplicating picker policy.
The sidebar, breadcrumb, search, view modes, and file type filters from the
standalone design remain to implement.

The shared Font panel reads an open Font Kit catalog, lists installed
family/style faces and exact AFNT bitmap strikes, previews through the common
Label's installed-font path, and returns the chosen face/strike. In system
selection mode, faces without the catalog's SELECTABLE flag remain visible
with a clear explanation while Use is disabled. It does not scan font files or
change the system font itself; the caller owns that policy and capability.
Two caller-owned strike buffers let a face change validate and replace the
List without invalidating its previous borrowed rows.

Haiku `src/preferences/appearance/FontSelectionView.cpp` informed the
face/size/preview composition. Astra uses exact installed strikes rather than
Haiku's freely adjustable point-size spinner because its glyph hardware
renders installed AFNT strike sizes. Haiku's Icon-O-Matic
`src/apps/icon-o-matic/generic/gui/panel/color_picker/ColorPickerPanel.cpp`
keeps Cancel/Use outcome delivery in a shared panel; Astra returns semantic
actions to the caller's ordinary retained UI loop.
