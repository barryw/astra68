# Astra settings ownership

Config Kit is Astra's settings substrate. It stores small, versioned, human-readable
`astra-config` documents on the writable system volume. There is no central
registry database or global key namespace. A setting belongs to one component;
that component validates its values and defines defaults.

## Storage and authority

| Owner | Directory | Access |
| --- | --- | --- |
| System component | `/config/system/<service>/settings.conf` | Its resident service's `CONFIG_SYSTEM` grant |
| Service implementation | `/config/services/<service>/settings.conf` | Its private `CONFIG` grant |
| Application | `/config/applications/<app>/settings.conf` | Its private `CONFIG` grant |
| Command | `/config/commands/<command>/settings.conf` | Its scoped command grant |

Supervisor derives roots from verified service or bundle identity, creates owner
directories, and grants only requested rights. Ordinary apps cannot request
`CONFIG_SYSTEM`; system services cannot delegate that grant. Config Kit opens
the selected grant, checks read/write rights, validates the document's format
and schema version, and writes a temporary file, syncs it, then renames it over
the old document. Installers preserve `/config` rather than replacing user data.

System components own system policy; apps own app preferences. A settings UI
asks the owning service to change system policy through a typed, validated
operation. It does not receive a raw writable `/config/system` path. Applications
use their own Config Kit handle directly for private preferences.

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
  the last committed document or built-in defaults. Cross-process notification
  and transactional multi-key updates are future work, not implied by Config
  Kit's current per-key atomic write.

## Font preferences

Font service will own `/config/system/fonts/settings.conf`. Its system-font
setting will identify one installed face, including designed weight, through a
single catalog identity. One Config Kit write changes that choice atomically.
The service validates the face and all required strikes before activating it.
Deleting that font, a damaged preference, or an unavailable generated strike
selects built-in UI face; recovery text always uses its built-in rescue face.
Fonts UI will import/manage faces and request the system-font change through
font service. It will not contain its own font renderer.

Theme and common controls own semantic pixel sizes: menu bar, window title,
body/control text, desktop labels, and other roles may differ. The system font
choice controls face/weight across those roles; each role requests its own
strike. Terminal's font choice belongs to Terminal's application settings and
does not change system font. Terminal may own its size choice too. Target
rendering sends validated AFNT strikes through Astraea's glyph engine;
host-side outline conversion produces strikes, not an application drawing path.
Today draw-list text reaches Astraea, but direct RGB565 surface text still has
CPU pixel loops in `sw/userspace/graphics/src/surface.c`. Production Desktop's
scrollbar now uses the shared Interface Kit draw-list renderer through System
Kit's checked list append, not a CPU-painted pixel buffer. Remaining direct
RGB565 rendering APIs must be retired from production UI use before
system-wide installed-font selection can be called complete.

Current implementation provides scoped Config Kit grants and document storage.
The resident font service catalogs AFNT and persists validated choice. The
native Fonts UI imports an outline font and saves that imported face as system
choice in a disposable QEMU gate. It offers a confirmation flow for removing
imported faces after changing away from the active choice. Display reads the
choice at startup; live updates and client-side metrics remain open. The
selected DE25 release does not yet include this font work.
Config Kit currently assumes one writer per settings document; multiple app
instances need an owner-mediated write path before concurrent editing is safe.

## Haiku reference

Reviewed `src/servers/app/DesktopSettings.cpp` for server-owned desktop font
defaults and per-user settings, `src/preferences/appearance/FontView.cpp` for a
font-selection UI that talks to shared system font policy, and
`src/apps/terminal/PrefHandler.cpp` for Terminal-owned preferences. Astra adapts
their ownership split to scoped Config Kit grants and native AFNT/graphics
contracts. Haiku checkout remains read-only.
