# File types and document opening

Status: native content identification and application-declared extension hints
implemented. Filesystem Kit identifies bounded Z-machine and Blorb headers,
uses `.apkg` as a built-in hint, then consults installed application bundle
`handles MIME-TYPE .extension` declarations if content remains unknown. Open
With discovers live bundles, offers a shared chooser, and stores type defaults
with Config Kit. Atlas sends file double-clicks to Open With. VFS typed
attributes, general format sniffer registration, and per-file preferred
applications remain design contracts below; content identity is derived on
every open.

## Identity and ownership

A file's content type and its preferred application are separate facts.
Content types use stable MIME-style identifiers. `application/x-zmachine-story`
identifies Z-machine versions 1–8; story version comes from the header, not
from the identifier or filename. A Blorb containing Z-code has a distinct
container type and may be opened by the same application. A file-specific
preferred application does not change the content type or global default.

Filesystem Kit provides one content-type query for every mounted backend.
Atlas and other clients never examine bytes or filename suffixes to identify
content. Current resolution order is bounded content recognition, built-in
`.apkg` hint, application-declared extension hint, then unknown. Once typed
attributes exist, explicit validated file attributes will precede content
recognition. A hint is not silently written as a trusted attribute. A
recognizer consumes one open file handle and has fixed read/CPU limits.

Future writable backends with typed attributes persist `system.content_type` on the
file object. The attribute is copied with the file where supported. Read-only
media and backends without attributes expose the same query using derived
metadata; no attempt is made to modify the volume. Unsupported is an explicit
backend capability, not a silent sidecar database. If the contents change, a
derived result is invalidated by node identity, size, and modification time;
an explicit type remains under user control until Identify is requested.

## Application discovery and preferences

An application bundle declares stable types it can open and optional extension
hints. A future File Types revision may add declarative, bounded recognition
rules. Copying a bundle into `APPS:` makes declarations discoverable; removal
removes them. An APKG install uses the same discovery path. No installation
script writes a global handler table and no application may seize a user's
default. A disposable index may accelerate discovery; bundles remain source
of truth. Launching a bundle on another mounted volume remains valid.

The preferred application for a type is a user setting managed by Config Kit.
The setting records a stable bundle ID; resolution checks the currently
installed bundle and its declared support each time. An absent or unsupported
preference opens the chooser. A single eligible handler may be selected
automatically only when no user preference exists. Multiple handlers require
an explicit choice unless a default was already set.

The shared Open With dialog lists eligible applications and offers Open and
Cancel. For a known type, `Always open files of this type with this app` sets
the type default. For an unknown type, the dialog offers Open once; it does
not assign one application to every unknown file. Per-file preference, when
available, is a separate explicit action. Atlas calls the shared resolver and
dialog; it contains no format-specific rules.

When AstraZ is built, its application bundle should declare
`handles application/x-zmachine-story` for `.z3`, `.z4`, `.z5`, `.z6`, and
`.z8`, plus `handles application/x-blorb .blorb`. Filesystem Kit identifies
valid story headers even after renaming; extension declarations let unknown
bytes reach AstraZ for its own validation. The bundle ID becomes the stable
target of a user default. See `docs/BUNDLE_FORMAT.md` for manifest syntax.

## Gates

- Rename a valid V3 story to a name without suffix: Identify still reports
  Z-machine story and launches its preferred application.
- Give arbitrary bytes a `.z3` suffix: they are not marked as a validated
  Z-machine story, and the chosen application must still reject invalid data.
- Install, copy, move, and remove a bundle: eligible handlers update without
  stale registration. Existing user defaults are never overwritten.
- Set a default, reboot, reopen another file of the same type, then remove
  the preferred app: chooser appears and the setting is preserved for when
  that app returns.
- Copy across ext4, FAT, and optical volumes; identify the same content and
  open it without writing to read-only media.
- Open unknown content: chooser opens once; no global unknown association.
- Repeat the complete path on DE25, including Atlas double-click and native
  app launch, then check for regressions in bundle and filesystem tests.

Haiku reference: `src/kits/storage/MimeType.cpp` separates guessing, sniffer
rules, and preferred application; `src/kits/storage/AppFileInfo.cpp` records
supported types on applications. Astra keeps this separation while using
its own Filesystem Kit, Config Kit, and bundle discovery contracts.

First-slice verification, 2026-10-27: Beast host bundle, application catalog,
file type, development installer tests, and NDK `dist-check` passed. On DE25,
Atlas opened unknown `/dh0/astra_events.cat` in Open With. No application was
preselected, Open was disabled, and Cancel returned cleanly to Atlas. Initial
hardware run exposed a missing service-ready signal: Atlas showed
`ASTRA_ERROR_PEER_DEAD` after the chooser closed. Adding the handshake fixed
that error. Selected RFB evidence is archived on Beast at
`/var/tmp/astra-file-types-evidence-20261027/`. Known-type automatic launch,
saved-default persistence, removable-volume opening, and the gates above remain
to be certified on DE25.

Extension-hint gate, 2026-10-27: synthetic AstraZ manifests declare `.z3`,
`.z4`, `.z5`, `.z6`, `.z8`, and `.blorb`. Beast tests verify case-insensitive
filename matching, content over hint, bundle removal, conflicting bundle
declarations, and rejection of conflicting declarations in one manifest.
This allows AstraZ to register its file types when its bundle exists; AstraZ
is not installed yet. DE25 opened a real valid Z3 story from temporary WORK
through native Open With. It showed `No installed application handles this
file type.` with no file-identification or launch error. Trial exposed a
missing `WORK:r` grant in both bundle and supervisor launch ceiling; both are
fixed. Selected screenshot is archived on Beast at
`/var/tmp/astra-file-types-extension-evidence-20261027/de25-z3-openwith.png`.
Board trial story and RFB captures were removed after archiving; all three
Astra services remained active with 8.3 GiB free. AstraZ launch and its saved
default remain untested until AstraZ exists.
