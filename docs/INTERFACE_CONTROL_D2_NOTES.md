# D2 overlay and container implementation notes

Status: Form Row, parent-attached Sheet, Tooltip, and Notification implemented.

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

## Shared lifecycle

GUI protocol 26 carries a Sheet's parent window ID and generation when it
opens. The display validates same-session ownership, keeps the Sheet attached
to its parent's content origin as the parent moves, hides it with a minimized
parent, and tears it down when that parent closes. Sheet modality redirects
input and activation only from its parent; unrelated windows in the session
remain interactive. The new `astra_window_sheet_create` API leaves the
ordinary creation structure and System Kit ABI intact. The same protocol
generation adds `astra_window_menu_open_persistent`: the display owns a flat
menu's checked state, reports each selection with
`ASTRA_WINDOW_EVENT_CONTINUED`, and reports a terminal zero selection when
dismissed. This shared menu lifecycle serves P1's Multi-select Popup.

Tooltip and Notification use the display service's overlay resource and scene
layer rather than application windows. Both copy a bounded single-line UTF-8
message, place it within the work area, and expire on display vblank. Tooltip
waits 500 ms, is placed at a client-local anchor, and closes on pointer motion,
keyboard activity, focus loss, owner close, or timeout. Notification appears
immediately at the work-area edge and closes on a pointer click, owner close,
explicit hide, or timeout. Neither captures input. A menu replaces either
notice; a notice cannot displace an active menu. This shared overlay slot
keeps placement and focus policy in the display service.

Sheet does not reuse the session-wide `ASTRA_WINDOW_MODAL` flag. Its parent
identity has no reparent operation, so stale or cross-session parent identities
are rejected when it opens. A Sheet may be moved while retaining a relative
offset; parent movement, minimize/restore, and close propagate to it.
