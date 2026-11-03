# Astra settings ownership

Config Kit stores application and service settings. It stores small, versioned, human-readable
`astra-config` documents in the private host state behind the `CONFIG` export. There is no central
registry database or global key namespace. A setting belongs to one component;
that component validates its values and defines defaults.

## Storage and authority

| Owner | Directory | Access |
| --- | --- | --- |
| System component | `/config/system/<service>/settings.conf` | Its resident service's `CONFIG_SYSTEM` grant |
| Service implementation | `/config/services/<service>/settings.conf` | Its private `CONFIG` grant |
| Application | `/config/applications/<bundle id>/settings.conf` | Its private `CONFIG` grant |

Command-line tools use ordinary configuration files under
`HOME:/.config/<command>/`; Terminal sets `XDG_CONFIG_HOME=/home/.config`
for compatible ports. Existing shell dotfiles in `HOME:` remain usable, but
new command defaults belong in the command directory. Commands do not receive
Config Kit grants. A command that changes system policy calls the owning
service's validated API;
that service persists the change through Config Kit. Config Kit does not
materialize command files or watch them for edits.

Haiku's `B_USER_SETTINGS_DIRECTORY` pattern informed the dedicated user
settings location (`src/apps/workspaces/Workspaces.cpp`); Astra adapts it to
ordinary CLI files under `HOME:/.config/`, while native apps and services keep
private Config Kit roots.

Supervisor derives roots from verified service or bundle identity, creates owner
directories, and grants only requested rights. Missing Config Kit providers
fail launch instead of silently omitting a requested or automatic grant.
Ordinary apps cannot request
`CONFIG_SYSTEM`; system services cannot delegate that grant. Config Kit opens
the selected grant, checks read/write rights, validates the document's format
and schema version, and writes a temporary file, syncs it, then renames it over
the old document. The root storage provider migrates the former system-image
`/config` tree into root-owned host state before publishing volumes or starting
QEMU. It keeps owner paths and metadata, then removes the readable system-image
copy. The host state must be backed up and restored with the system image and
the host-held secret key. Installers must preserve that state across releases.

System components own system policy; apps own app preferences. A settings UI
asks the owning service to change system policy through a typed, validated
operation. It does not receive a raw writable `/config/system` path. Applications
use their own Config Kit handle directly for private preferences.

## Visibility contract

Each application and service owns a private Config Kit document. Ordinary
owners cannot inspect another owner's document. A system management
tool may receive an explicit, narrow grant for migration or administration;
its role alone does not grant access to every owner's settings. Private Config
grants are marked `CLOSE_ON_EXEC`: native exec omits them from the new startup
table and closes their live handles, so a command cannot inherit an
application's or service's private settings grant.

Applications receive a read-only `CONFIG_PUBLIC` grant rooted at
`/config/public`. A resident service may explicitly request
`CONFIG_PUBLISH:rw`, rooted at `/config/public/<service>`, and write a public
document with `astra_config_open_publish`. Applications read one named owner
with `astra_config_open_public`. Public fields are canonical in that document;
private fields stay in the owner's private document, so publishing never
copies or exposes the private file. A setting being unencrypted does not make
it public. Applications cannot write the public view.

Config Kit 2.0 requires an owner schema listing every key. A field marked
`ASTRA_CONFIG_FIELD_SENSITIVE` uses the same typed get/set calls; Config Kit
seals and opens its value and fails closed on authentication failure. Unknown
keys are refused. No global credential document or separate credential API
remains. Public documents must contain only non-sensitive fields. Callers
choose public/private ownership at document open, not by inspecting ciphertext
or key names.

Application settings use the bundle manifest's stable `id`, like `STORE:` data,
so moving a bundle into a subfolder or another volume keeps its preferences.
On first launch, supervisor atomically moves an older leaf-name settings
directory to the ID directory when the destination is absent. When both
exist, it leaves both intact and uses the ID directory.

## Failure rules

- Missing, malformed, unsupported, or semantically invalid optional preferences
  must leave the owning component on known-good built-in defaults. They must not
  block boot, desktop, or recovery UI. Report the bad owner/key to the user
  without deleting or rewriting the original document.
- Validate choices before committing: referenced resources must exist, be
  readable, and satisfy the component's contract. A font face must be installed,
  AFNT-validated, and renderable at required UI role sizes.
- Settings are data, never executable code or arbitrary device/namespace paths.
  Apply them only through the owning service's limited capability.
- One service owns and writes each system document. Readers consume an immutable
  validated snapshot. If that service restarts, it rebuilds the snapshot from
  the last committed document or built-in defaults. The owning service publishes
  validated changes through its native service API; Config Kit does not broadcast
  raw document changes to other owners.

## Settings application

`Settings.app` is a native Interface Kit client. Its own Config Kit document
stores only the selected pane. The shared Settings Sidebar composes a Search
Field, Scroll View, List, and Container; other applications can use the same
component. Search updates the List through its live `set_items` API, keeping
keyboard focus and collection metadata coherent.

The Dock pane queries ordered pins from Display and asks Display to move or
remove one pin with a generation check. Display remains the sole writer of
`/config/system/display/settings.conf` through Config Kit. Settings never
edits that document directly. The Fonts pane launches the existing Fonts app,
which uses the font service for installation and system-face changes. About
is read-only. No duplicate font or Dock setting is stored in Settings' private
document. The Dock pane refreshes its list when selected or reactivated; an
empty Dock shows the existing Keep in Dock path.

## Font preferences

Font service owns `/config/system/fonts/settings.conf`. Its system-font
setting identifies one installed face, including designed weight, through a
single catalog identity. One Config Kit write changes that choice atomically.
This document stores preferences and inventory identities, not font files.
All installed AFNT files live under `FONTS:`: protected shipped faces in
`FONTS:system/` and service-managed imports in `FONTS:installed/`.
The service validates the face and all required strikes before activating it.
Deleting that font, a damaged preference, or an unavailable generated strike
selects built-in UI face; recovery text always uses its built-in rescue face.
Fonts UI imports/manages faces and requests system-font changes through the
font service. It contains no separate font renderer.

Theme and common controls own semantic pixel sizes: menu bar, window title,
body/control text, desktop labels, and other roles may differ. The system font
choice controls face/weight across those roles; each role requests its own
strike. Terminal's font choice belongs to Terminal's application settings and
does not change system font. Terminal may own its size choice too. Target
rendering sends validated AFNT strikes through Astraea's glyph engine;
host-side outline conversion produces strikes, not an application drawing path.
Native production UI submits draw lists to Astraea. Direct RGB565 helpers
remain for software surfaces and host oracles; they do not define the native
UI rendering path. Desktop's scrollbar uses the shared Interface Kit renderer.

Current implementation provides scoped Config Kit grants and document storage.
The resident font service catalogs AFNT and persists validated choice. The
native Fonts UI imports an outline font and saves that imported face as system
choice in a disposable QEMU gate. It offers a confirmation flow for removing
imported faces after changing away from the active choice. Font Kit 5 and GUI
protocol 24 bind client metrics and rendering to the same immutable snapshot.
The service prepares that snapshot before the settings write, then signals
Display through a native change event. Window-specific notices drive live
context refresh and common-control reflow, retaining previous snapshots across
failures. Focused host/sanitizer/native checks pass; coherent native GUI and
physical release qualification remain open. The selected DE25 release does not
yet include this font work.

Interface Kit 7.2 supplies the shared settings pane composition used by Fonts:
Label title/description, a growing Container body and a Container of stacked
actions. Its common layout owns window insets and spacing. Application controls
and ordinary Interface Kit focus, layout and drawing remain shared; the shell
adds no settings backend. See the NDK's `settings_pane.c` example.
Config Kit serializes each set, append, and remove for one owner document with
an exclusive file lock. A writer reloads current settings after acquiring the
lock, commits through a synced temporary file and atomic rename, then closes
the lock handle. The host releases the lock if a writer dies. Concurrent
instances can write different keys or append without losing another write;
contention lasting five seconds returns `ASTRA_CONFIG_BUSY`. Reads use the
handle's last snapshot until `astra_config_reload` is called. Conditional set
and append compare that snapshot with the current document under the same
lock; a changed document refreshes the handle and returns
`ASTRA_CONFIG_CONFLICT` without writing. Callers can recompute their choice
and retry. Open With uses this path to avoid duplicate file-type preferences.
A sequence of separate API calls is not one atomic multi-key transaction.
`astra_config_apply` commits one to 64 ordered set, append, or remove changes
as one document rename, including sensitive fields. A failed edit leaves the
committed document intact. `astra_config_apply_if_unchanged` also checks the
snapshot before applying edits.

## Haiku reference

Reviewed `src/system/kernel/fs/vfs.cpp` file-lock lifecycle: locks belong to
open descriptors and are released when the owner closes or dies. Astra adapts
that pattern to HostFS advisory locks on one per-owner Config Kit lock file.
Reviewed `src/kits/shared/SettingsMessage.cpp`: it uses `BAutolock` for one
object's load/save and notifies local listeners after a value changes. Astra
retains owner-scoped settings and adds cross-process snapshot comparison.

Reviewed `src/servers/app/DesktopSettings.cpp` for server-owned desktop font
defaults and per-user settings, `src/preferences/appearance/FontView.cpp` for a
font-selection UI that talks to shared system font policy, and
`src/apps/terminal/PrefHandler.cpp` for Terminal-owned preferences. Astra adapts
their ownership split to scoped Config Kit grants and native AFNT/graphics
contracts. The native Settings shell also reviewed
`src/preferences/appearance/AppearanceWindow.cpp` and
`src/apps/deskbar/PreferencesWindow.cpp`: grouped navigation and owner-specific
panes informed the shared sidebar and typed Dock API. Haiku checkout remains
read-only.
The private-store and grant-crypto changes also reviewed
`src/system/kernel/fs/vfs.cpp` for server-enforced path authority and
`src/servers/keystore/KeyStoreServer.cpp` plus `Keyring.cpp` for service-owned
secrets and caller checks. Astra's HostFS crypt operation binds authenticated
data to its own canonical grant root and setting key; the caller cannot
substitute a root. The storage provider moves settings out of broadly
readable DH0 before boot.
