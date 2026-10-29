# Shared system panels

Interface Kit owns system panel composition; callers own the window, event loop,
control array, and the selected value. The first implemented panel is Colour.
It uses existing Container, Label, Colour Well, Slider, and Button controls.
The Gallery's Fields-page Colour Well opens the shared Colour panel on a Panels
page and applies the result back to that well. No pixel renderer or app-specific
picker was added.

RGB565 channels use exact ranges 0..31, 0..63, 0..31, so every selection is
representable. XRGB8888 channels use 0..255, with opaque preview. Indexed mode
borrows an opaque palette and selects an actual slot. `packed` is the encoded
result, so callers do not infer it from a rounded preview. Cancel does not
apply it. The caller may render `AstraColourPanel` with its ordinary UI context
and pass each `AstraUIAction` to `astra_interface_colour_panel_action`.

Reference review:

- Haiku `src/kits/tracker/FilePanel.cpp` keeps file selection and window
  behavior in a shared kit panel, not in each application. Astra follows the
  same ownership direction. Its window/message machinery is deliberately
  replaced by Astra's retained controls and ordinary `AstraUIAction` loop.
- Haiku `src/kits/interface/ColorControl.cpp` composes palette and channel
  controls in a reusable kit component. Astra composes existing shared
  controls; exact source format and palette slot remain first-class data.
- Astra design `~/Downloads/Astra Desktop (standalone).html`, Colour panel:
  RGB565/XRGB8888/indexed modes, sliders, preview, Cancel and Use. This slice
  implements those semantic modes and actions. Field, Theme, and Recent tabs
  and the two-swatch wanted/stored presentation remain for follow-up.

Fonts panel still needs implementation. File panels should grow toward the
same navigation and display controls as Atlas. Fonts panel should use Font Kit
catalog and installed face/strike selection, with no app-specific scanner.

The shared Open/Save file panel now consumes `AstraDirectoryEntry` snapshots
from Filesystem Kit, builds a List in a ScrollView, and returns a directory
plus leaf name. Its actions distinguish entering a folder, opening a file,
saving a new name, replacing an existing file, and cancelling. Save requires
an editable `TextModel`; it rejects empty, dot, dot-dot, slash, and control
names. The caller performs the actual open/save operation and handles the
overwrite confirmation. Directory replacement uses a second item buffer so
the old List data remains valid if validation or layout fails. Existing Atlas
and Fonts browser code can adopt this panel without duplicating picker policy.
The sidebar, breadcrumb, search, view modes, and filename filters from the
standalone design remain to implement.
