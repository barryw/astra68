# Astra desktop and user-interface model

Status: architecture direction with a protected window-service prototype.

This document defines the intended feel and behavioral model of Astra's native
graphical environment. `GRAPHICS_ARCHITECTURE.md` defines the active Arty
target. `PRESENTATION.md`, `VEGA.md`, `ASTRAEA.md`, and `FONTS.md` retain the
implemented ULX3S behavior and contracts carried forward by the new design.
`INTERFACE_SPECIFICATION.md` is the normative visual language and NDK window
style contract.

## 1. Experience target

**LOCKED:** Astra should feel as immediate, legible, coherent, and personal as
a well-designed Amiga, without cloning Workbench or inheriting AmigaOS's unsafe
execution model.

Adopt the principles, not the artifacts:

| Preserve | Replace |
|---|---|
| compact, direct interaction | flat shared address space |
| visible volumes and understandable objects | raw pointers and public system structures |
| consistent devices, libraries, and messages | subsystem-specific ownership accidents |
| hardware-aware graphics and media | unrestricted application MMIO |
| menus and controls that react immediately | cooperative application dependence |
| fullscreen/public-screen spirit | applications owning physical scanout |
| small tools that compose well | frozen historical ABI baggage |

The visual design should be recognizably Astra: crisp, compact, readable, and
confident at 1920x1080. It should not depend on nostalgia, fake scanlines, giant
touch-sized controls, excessive padding, translucent decoration, or animation
that delays an action.

## 2. Desktop composition

**DIRECTION:** The display service owns windows, composition, scenes, graphics
queues, and presentation. The workspace owns desktop policy, launcher behavior,
mounted-volume presentation, and the user's arrangement.

The normal desktop uses:

- two wrapped RGB565 or XRGB8888 scanout surfaces for tear-free presentation;
- application-owned off-screen surfaces with explicit damage;
- Astraea blits and drawing commands to compose the back surface;
- fenced vblank presentation through Vega;
- hardware glyph runs for normal UI and terminal text;
- sprite descriptor 0 for the pointer while the desktop is active, leaving 63
  of the 64 hardware descriptors available to the active desktop Scene.

The remaining hardware sprites are not general window objects. A protected
fullscreen game or scene may receive a validated sprite set. Exclusive
fullscreen can hide the system pointer and temporarily release its sprite, but
returning to the workspace restores the reservation and input ownership.

Ordinary UI work never polls the beam. Copper remains an explicitly validated
raster facility, not a way for widgets to bypass scene ownership.

## 3. Windows

An application window is a service object containing at least:

- identity, owner, title and application relationship;
- frame, minimum/maximum size, visibility and stacking state;
- current completed content surface and damage generation;
- one event endpoint and one command set;
- focus, pointer capture, and close state;
- explicit resource and queue limits.

The application renders into a non-visible surface and submits a completed
generation. The display service retains the last completed generation until a
new one is ready. Therefore:

- moving, raising, exposing, minimizing, or closing a window does not wait for
  its application;
- a hung application keeps its last valid contents rather than tearing or
  exposing random memory;
- resize negotiation has a deadline and a fallback presentation policy;
- the compositor may label or offer termination for an unresponsive window;
- no global busy cursor exists.

Window events are queued, coalesced where semantics permit, and bounded.
Pointer motion may coalesce; button, key, command, close, and ownership events
may not disappear silently.

## 4. Workspace and visual structure

**DIRECTION:** The initial workspace combines proven desktop ideas without
copying Workbench pixel for pixel:

- a persistent top strip for the active application's menus plus concise
  machine state;
- mounted volumes and important locations represented as first-class desktop
  objects;
- compact windows with obvious focus, depth, resize, and close controls;
- a launcher and command palette that do not require navigating the filesystem;
- direct keyboard access to a terminal from anywhere;
- context menus and drag and drop using the common command/resource model;
- deterministic placement and restoration rather than windows jumping as
  asynchronous metadata arrives.

The desktop is not merely the filesystem. Applications, documents, volumes,
services, running jobs, and queries are distinct typed objects even when they
have icons or can be reached by paths.

Window chrome, its semantic palette, and its geometry are specified in
`INTERFACE_SPECIFICATION.md`. Icon language, menu activation behavior, and the
workspace name remain **OPEN** and require real hardware prototypes.

## 5. Scenes

**DIRECTION:** A Scene is a protected top-level presentation environment in the
spirit of an Amiga public or custom screen, but owned and mediated by the
display service.

Examples include the normal workspace, a fullscreen game, a graphics tool, a
presentation, or a recovery environment. A scene owns a set of windows or one
exclusive surface, display policy, input focus, and optional validated sprite
or raster resources.

Scene switching is instant and tear-free through framebuffer page selection,
Vega viewport scrolling, or both. Applications never receive the active
framebuffer address. A scene that stalls cannot prevent switching back to the
workspace. Whether multiple scenes can be partially revealed or dragged like
classic Amiga screens is **OPEN** and must be justified by usability and
bandwidth measurements.

## 6. Scrolling and animation

The Arty Vega contract provides pixel-granular X/Y framebuffer viewport
scrolling and independent wrap. The UI and game kits expose that capability as
a surface or scene operation, not MMIO.

- Scrolling uses a larger or ring-shaped framebuffer where that avoids copies.
- The copper may animate permitted viewport/register values during scanout.
- Ordinary window contents still use fenced surfaces and damage.
- Animation advances from frame callbacks or media time, not collections of
  unsynchronized wall-clock timers.
- A missed frame retains the previous completed frame; it never tears.

The two hardware tile layers are first-class game-scene resources. They support
pixel-granular X/Y scroll, independent wrap, foreground overlays, and
copper-controlled scroll bands. Desktop content normally uses fenced chunky
surfaces; tile layers are available when a workspace effect can justify their
resource and bandwidth cost rather than being exposed as a second widget API.

## 7. Commands, menus, and automation

**DIRECTION:** User-visible actions are command objects rather than duplicated
menu callback code. A command has a stable application-local identifier,
versioned argument description, label/icon metadata, enabled and checked state,
shortcut suggestions, and an asynchronous invocation endpoint.

Menus, key bindings, toolbar controls, the command palette, scripting, and the
system inspector can all discover or invoke the same command. Applications
remain responsible for their command semantics; the workspace does not infer
actions by scraping UI text.

Commands that can block return a request/fence and complete asynchronously.
Closing a window or application cancels or resolves every outstanding command
according to its protocol.

## 8. Input and focus

- The protected input service normalizes hardware events. The display service
  is the sole seat owner and routes subscribed window events through bounded
  per-window message ports.
- Window pointer events carry both screen and client-relative coordinates.
  Explicitly authorized windowless observers receive screen coordinates only.
- Pointer location and system shortcuts remain responsive at ordinary CPU
  saturation.
- Focus changes are explicit ordered events.
- Pointer capture is a revocable lease and is released on process death.
- Global shortcuts are owned by workspace policy, not intercepted by arbitrary
  applications.
- Fullscreen games receive bounded low-latency event rings without bypassing
  process isolation.
- Text input is UTF-8 and separate from physical key events. Games and editors
  may request either or both.

## 9. Typography and icons

Normal UI text uses Astra Sans; terminals, code, and dense tables use Astra
Mono. The rescue font is never the default desktop face. Hardware expands
validated glyph runs; applications do not draw glyphs with CPU pixel loops.

Icons and other application resources live in bundles or system resources,
not in executable C structures. The first icon format should support compact
indexed artwork and multiple designed pixel sizes. Runtime vector rasterization
is not a prerequisite for the first desktop.

## 10. Responsiveness contract

The UI treats responsiveness as correctness:

- pointer updates target one display frame end to end;
- ordinary key/menu/window feedback targets two frames or less;
- window movement targets 60 presentations per second;
- the compositor performs no unbounded allocation or service call in a frame;
- input, close, switch-scene, and terminate remain available under CPU,
  storage, network, and application failure;
- queue depth, frame misses, input latency, composition time, damage, and
  graphics-engine resets are observable.

Initial numeric gates and memory envelopes are in `USERSPACE_BUDGET.md`. They
are provisional until measured on the qualified ULX3S build, but regressions
may not be hidden by silently relaxing them.

## 11. First desktop milestone

The first desktop is intentionally small but real:

1. workspace background and top strip;
2. mounted-volume or no-media state;
3. launcher/command palette;
4. one draggable, resizable terminal window;
5. hardware pointer and keyboard focus;
6. Astra Mono text rendered through the normal font/draw path;
7. tear-free composition and presentation;
8. process-hang and process-crash containment;
9. visible performance and resource counters.

It is not a mockup and does not use the POST text plane as the normal desktop.
