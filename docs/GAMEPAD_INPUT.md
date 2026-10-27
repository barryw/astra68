# Native gamepad input

Status: Beast targeted builds and DE25 physical controls and hotplug passed in
a guarded trial. Normal release gates remain.

The Linux host discovers evdev joystick nodes under `/dev/input/by-id`, with
`by-path` fallback. QMP attaches up to four `astra-evdev-gamepad` objects to
the running QEMU machine. Slots remain stable for devices that stay attached.
QEMU reads evdev identity and control capabilities, scales axes, and emits
version 1.2 Astra input records. It never exposes evdev to guest applications.
InputService owns connection, state, removal, generation reset, and subscriber
snapshots. `system.library` exposes `astra_gamepad_*` observers. SDL's Astra
joystick driver adapts that native API to SDL joystick events and GUIDs, then
uses SDL's existing controller mapping database. Unknown devices remain
available through SDL_Joystick even without a GameController mapping.

Haiku reference reviewed before this mechanism:
`~/Git/haiku/src/add-ons/kernel/drivers/input/hid_shared/JoystickProtocolHandler.cpp`
discovers axes, hats, and buttons from HID capabilities;
`~/Git/haiku/src/kits/device/Joystick.cpp` exposes device state to clients;
`~/Git/haiku/src/servers/input/InputServer.cpp` explicitly unregisters and
registers devices across removal and addition.
Astra adapts capability-based discovery to its host-provider boundary and
central InputService, preserving one native input implementation. The host
tracks each evdev node's sysfs device incarnation so unplug/replug of the
same stable path still causes an explicit detach and attach.

Compatibility: InputService remains protocol 3. Gamepad uses a new event class
and an unused subscription bit; the record layout and existing seat bit mask
are unchanged. Installed protocol 3 Desktop and Display clients work with
the new service. `system.library` stays ABI 4 and adds gamepad symbols in
version 4.3.0; SDL2 stays ABI 3 and advances its compatible release version.
The probe carries those two newer libraries privately, leaving installed
applications on their existing providers. Incompatible record or library
contracts require a new major version and migration of actual consumers.

Physical device observed on DE25, 2026-10-26: Logitech F310 in DirectInput
mode, USB `046d:c216`, evdev name `Logitech Logitech Dual Action`. Linux
reports X/Y/Z/RZ axes, one hat, and twelve buttons. SDL already carries its
`Logitech F310 Gamepad (DInput)` mapping; raw evdev capability order and USB
GUID are passed through so its right stick, shoulders, triggers, and face
buttons use that mapping. Linux initially reported axis value zero for all
axes; QEMU waits for a real axis event before seeding that axis, avoiding a
false full-stick deflection on attach.

DE25 trial, 2026-10-26: copied production storage, replaced only InputService,
and launched a private SDL probe bundle with newer `system.library` and SDL2.
Production storage hash and selector were preserved on every rollback. QEMU
initially registered the gamepad QOM type without adding it to the typed QMP
`object-add` schema; the new schema patch fixes attachment. SDL initially
generated a GUID checksum from a synthetic device label and omitted its
Linux-compatible mapping database. Both are fixed. Physical F310 now reaches
SDL as `046d:c216`, GUID `030000006d04000016c2000011010000`, mapped to
`Logitech Dual Action`; SDL video probe starts. First physical control trial
recorded 76 button-down and 2096 axis-motion events, including face buttons,
D-pad, and both sticks, with no panic. During a later live hotplug trial, host
logged detach and reattach of the same F310, guest SDL reported remove, add,
and controller reopen in that order, then 1 button-down and 277 axis-motion
events. The probe initially stopped listening after removal because its loop
limit depended on the now-closed controller pointer; it now snapshots the
duration before polling. A live QMP remove/add cycle and physical cable cycle
both confirmed reopen. The guarded trial copied production storage; rollback
restored its hash, release selector, and services. Evidence resides on Beast
under `/var/tmp/astra-gamepad-evidence-20261026/`; DE25 trial scratch was removed.

Gates before release: exact DE25 release build, full guest/input/SDL regression
checks, and normal preserved-state deployment. Avoid changing the full image
during iteration; use targeted artifacts and a copied production storage image
for guarded trials.
