# D2 overlay and container implementation notes

Status: Form Row implemented; Sheet, Tooltip, and Notification remain open.

## Haiku read-only review

- `~/Git/haiku/src/kits/interface/ToolTip.cpp` and
  `src/kits/interface/ToolTipManager.cpp`: a manager owns the tooltip lifecycle,
  including replacement, delayed display, and hiding. Astra should put tooltip
  ownership in the display service's existing overlay lifecycle, rather than
  create a floating window for each application control.
- `~/Git/haiku/src/kits/interface/Window.cpp` (`AddToSubset`,
  `RemoveFromSubset`, `SetFeel`): parent relations and modality are registered
  with the window server. Astra's Sheet needs a server-validated parent
  identity and parent-only input blocking; the existing session-wide MODAL
  flag is insufficient.
- `~/Git/haiku/src/kits/tracker/FilePanel.cpp` (`BFilePanel::Show`): the
  shared panel owns positioning and its window, while callers own the result.
  Shared Astra panels should follow the same ownership split.

## Form Row

`AstraFormRowInfo` composes two Containers and two Labels plus a nested
value Container. The caller places its own control under `first_id +
ASTRA_FORM_ROW_VALUE`; its help or fault text sits beneath that value in a
shared-width column. The form row uses the same retained layout, theme roles,
damage, and focus handling as other Interface Kit controls. The first version
uses the caller's `label_width` so rows in one form can share a column width.

The Installer and Interface Gallery now use this composition. Beast host layout
tests verify the aligned message and nested value; m68k Interface Kit,
Installer, and Gallery builds pass. Label click-to-focus, right-aligned label
text, and narrow-window stacking are still design gaps; they should be solved
in shared Label/flex behavior rather than an application-specific painter.

## Remaining lifecycle work

Tooltip and Notification should share display-owned placement, timeout,
dismissal, and focus policy with the existing overlay machinery. The menu
overlay currently assumes every overlay is a menu, and its `menu_end` closes
global state. A generic overlay owner/renderer must be introduced before either
new surface is accepted. P1's persistent multi-selection menu work must use
the same lifecycle.

Sheet needs a parent window identity on the GUI open contract, validation of
same-session ownership, parent-relative placement and movement, parent-only
modality, and teardown when the parent closes. It must not reuse the current
session-wide `ASTRA_WINDOW_MODAL` behavior.
