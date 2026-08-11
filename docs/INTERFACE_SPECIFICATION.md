# Astra interface specification

Status: normative visual language, window-management, and pointer-routing
contract for the Astra OS guest.

This document specifies Astra's native interface. It does not describe the
AstraVM host application, whose visual system is separate. The public code
counterpart is `ndk/include/astra/theme.h`, `ndk/include/astra/window.h`, and
`ndk/include/astra/pointer.h`.

## 1. Design intent

Astra is a high-resolution 68030 workstation. It should feel like a precise
instrument: immediate, legible, compact, and deliberately constructed. Its
retro character comes from visible structure and crisp bitmap-aligned detail,
not from imitating the low resolution or heavy bevels of older systems. Its
modern character comes from spacing, typography, restrained color, complete
interaction states, and subtly rounded geometry.

Reference domains are workstations, observatory controls, star charts, logic
analyzers, terminals, physical instrument panels, and modular workbenches.
Where this specification does not decide an interaction, AmigaOS and BeOS are
the behavioral precedents: capability-owned message ports and explicit event
masks from AmigaOS, with BeOS-style separation between physical input, the
window server, and application window messages.

The recurring signature is a thin ion-cyan **signal rail**. It identifies the
active window and later repeats in focused controls, selected tabs, menu
selection, progress, and other active system state. It is an accent, not a
large saturated surface.

## 2. System theme

All interface components consume semantic roles from one versioned
`AstraTheme`. Applications must not embed private copies of system chrome
colors, radii, spacing, or gadget geometry. The initial system has exactly one
immutable theme and no theme editor. It is compiled into both the NDK and
window server from `ASTRA_THEME_SYSTEM_INIT`; dynamic distribution is deferred
until runtime theme selection exists.

The color world is:

| Role | Character |
|---|---|
| canvas | deep-space navy |
| system bar | near-black graphite |
| frame | black |
| active title | lifted graphite |
| inactive title | recessed graphite |
| client | cool lunar gray |
| primary text | starlight white |
| muted text | cool gray |
| accent | ion cyan |
| warning | restrained amber |
| fault/close | muted red |

Semantic color always communicates state. Close, minimize, and maximize do not
remain permanently red, amber, and green; their semantic color appears on
hover, press, or focus. Window content uses the same roles as chrome.

Geometry uses a 4-pixel spacing unit:

| Token | Value |
|---|---:|
| window outer radius | 12 px |
| window frame | 2 px |
| standard/dialog titlebar | 26 px |
| utility titlebar | 22 px |
| active signal rail | 2 px |
| gadget hit area | 20 x 20 px |
| gadget glyph | 10 px |
| control/card radius | 8 px |
| resize hit zone | 6 px |

Rounded rectangles are used for windows, buttons, fields, cards, and compact
controls. Pills are reserved for genuinely compact labels or binary state and
must not become the default shape. There are no window shadows in the initial
theme; the continuous black frame is the complete depth treatment.

## 3. Window anatomy

A normal window is composed in this order:

1. a continuous 2-pixel black rounded outer frame;
2. a dark titlebar whose upper corners follow the outer radius;
3. a flat, square seam between titlebar and client content;
4. a 2-pixel ion-cyan rail below the active titlebar;
5. client content whose bottom corners follow the frame;
6. a left-aligned title and right-aligned gadgets.

The title must remain the dominant titlebar label. Gadget order from left to
right is minimize, maximize, close. Their 20-pixel hit areas are larger than
their 10-pixel glyphs but remain compact. Window-specific color or radius
overrides are not part of the NDK contract.

## 4. Window types

| Type | Chrome and purpose |
|---|---|
| Standard | 26-pixel titlebar; close, minimize, maximize; normally resizable |
| Utility | compact 22-pixel titlebar; close; supporting tools and inspectors |
| Dialog | 26-pixel titlebar; optional close; no minimize/maximize; may be modal |
| Popover | black border only; no titlebar or gadgets; contextual transient UI |
| Fullscreen | system chrome hidden; content occupies its assigned work area |

Window type selects a system recipe. It is not an invitation for an
application to restyle the frame.

## 5. Gadget states

Every gadget has normal, hover, pressed, focused, and disabled appearances:

- normal uses neutral graphite and a starlight glyph;
- hover fills with the gadget's semantic color;
- pressed uses a recessed graphite fill and semantic glyph;
- focused receives a thin ion-cyan ring;
- disabled uses inactive chrome and muted text.

The style gallery may request these states at creation so all appearances can
be inspected before input routing exists. This is a prototype affordance.
Once pointer and keyboard routing are active, the window server owns hover,
pressed, and focus transitions; applications retain only semantic enablement.

## 6. Window behavior

- Dragging unused titlebar space moves a normal window.
- Double-clicking the titlebar toggles maximization when permitted.
- Resizable windows expose a 6-pixel invisible edge/corner hit zone.
- Maximized windows become square and flush with the usable work area.
- Minimize will place a window in the system shelf when shelf policy exists.
- `astra_window_close` destroys the server object immediately. A separate
  application-veto close-request event may be added with pointer routing; it
  must not change the explicit destroy operation.
- Moving, exposing, minimizing, or closing chrome must never wait for the
  application process.
- Only one window is active in the normal workspace; inactive chrome uses the
  inactive title role and has no signal rail.
- Windows may overlap. The compositor paints them in back-to-front workspace
  order, and activating a window raises it above ordinary peers.
- The active titlebar uses the brighter title role; brightness and the signal
  rail communicate focus without changing application-owned content.

## 7. Control language

Every future control inherits the same palette, 4-pixel grid, 8-pixel control
radius, typography roles, and state model. Components define semantic variants
such as primary, warning, or destructive; they do not define arbitrary colors.

The first complete control set should include buttons, text fields, checkboxes,
radio controls, sliders, scrollbars, tabs, menus, list/table rows, progress,
toolbars, splitters, and status bars. Each must specify normal, hover, pressed,
focused, disabled, selected, and error states where applicable before it is
accepted into the NDK.

## 8. Typography and icons

Astra Sans is the normal UI face; Astra Mono is for terminals, code, and dense
tables. Titles use the primary text role and are vertically centered. The
current gallery exercises the proportional AROS ISOHelvetica-derived AFNT
face; the 8x8 ROM font remains the recovery-console face.

Icons use crisp designed sizes and the semantic palette. System gadgets use
simple line glyphs rather than text labels. Applications provide content icons
but never replace system window gadgets.

## 9. NDK contract and acceptance

`AstraWindowCreateInfo` supplies frame position, draw-list content geometry,
window type, behavior flags, gadget presence, title, and preview gadget states.
The shared command area is transferred by capability; no application pointer
crosses into the display service. A successful create returns an opaque
`AstraWindow` containing a private control capability. The NDK uses that
capability for query, set-frame, move, resize, raise, lower, activate,
deactivate, minimize, maximize, restore, title, and close operations. The
server validates every request and resolves theme roles into hardware commands.

The initial visual acceptance gallery contains four real service windows:

1. active Standard with normal, hover, and pressed gadgets;
2. inactive Utility with a focused close gadget;
3. modal Dialog with a disabled close gadget;
4. border-only Popover without titlebar or gadgets.

This set must boot through the ordinary service manifest, exercise the complete
scripted management sequence, present 17 consumed display fences, remain within
the existing four mapped surfaces per process, and render from the NDK theme
and window attributes without per-window chrome constants in the compositor.

## 10. Pointer routing and application events

The protected input service is the only consumer of physical keyboard and
pointer records. The display service connects as the single seat owner and is
therefore the only process that owns window focus, hit testing, pointer capture,
titlebar dragging, gadget state, and z-order activation. Applications never
receive raw physical records and cannot claim the seat-owner role.

Each window owns a bounded event port and an explicit event mask. Motion,
button, and wheel messages include both screen coordinates and coordinates
relative to the window's client origin. Pressing the left button captures the
target until release; capture survives movement outside the rounded window,
and is revoked when the window or owner dies. Rounded corners participate in
hit testing, so transparent corner pixels do not activate a window behind the
pointer's actual target.

A process without a window may use `astra_pointer_observer_open` only when its
launcher explicitly delegates `INPUT_SERVICE`. It selects motion, button, and
wheel classes and receives screen coordinates only. This observer API cannot
subscribe to keys, text, focus, capture, or window-relative coordinates.
Closing its receive endpoint revokes the subscription through ordinary port
lifetime; no invisible window or global process identifier is involved.

Sprite 0 is the desktop pointer. The MC68030 submits only clipped position and
visibility; the Arty display owner updates the Astraea sprite descriptor and
commits the scene. Pointer movement never redraws the framebuffer merely to
move the pointer. Hover or pressed chrome damage remains an independent native
render batch.
