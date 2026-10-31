# Native Trash lifecycle for Atlas and Desktop

Status: implementation contract, 2026-10-31. Restore and Empty Trash are
not implemented yet.

## Existing substrate

Atlas and Desktop already call one shared folder operation to move files,
directories, and symbolic links into a writable volume's `.Trash` directory.
Filesystem Kit owns atomic rename-without-replacement, cross-volume tree
copy/move, exact mounted-volume views, directory traversal, file sync, and
private document staging. Interface Kit owns File Panel, menu, command, and
dialog controls. Keep those responsibilities where they are.

Haiku read-only references: `src/kits/tracker/FSUtils.cpp` writes
`_trk/original_path` before moving an item, reads it when restoring, and
has a separate Empty Trash operation. `src/kits/tracker/ContainerWindow.cpp`
offers Restore and Empty Trash while viewing Trash. Astra must use a sidecar
record because its native Filesystem Kit currently has no portable per-node
attribute API across supported writable filesystems. This is storage policy,
not an Atlas control.

## Shared storage contract

- Filesystem Kit owns `move_to_trash`, `restore_from_trash`,
  `empty_trash`, and a Trash snapshot API. Desktop and Atlas call those APIs.
  Remove the existing shared folder helper once all callers migrate.
- Each writable volume owns one `.Trash`. Records live in a private
  `.Trash/.origins` directory; payloads stay directly under `.Trash` so
  existing user data remains visible and no bulk migration is required.
- A record names the payload and stores its original path *relative to that
  volume root*. A remounted USB volume may receive another `/usbN` path;
  Restore joins the current exact mount root with the stored relative path.
  No absolute assign from a prior mount is trusted.
- Use an explicit versioned, length-checked record with bounded UTF-8 path,
  canonical components, and payload name. Validate decoded records before
  joining paths. Reject traversal, another volume, root itself, and paths
  that would place an item inside Trash. No destination is overwritten.
- Use Filesystem Kit's existing document-stage helper to write and sync
  original-path record *before* atomic move. If move fails, remove the record.
  A crash before move may leave an orphan record; snapshot ignores it and
  cleanup may reap it. A crash after move leaves both record and payload, so
  Restore remains possible.
- Restore uses atomic rename-without-replacement to the stored path. If parent
  is gone or destination is occupied, retain payload and record and report
  exact path and error. After successful rename, remove the record. A crash
  between rename and record removal leaves only orphan metadata.
- Empty Trash removes payload trees without following symbolic links, using
  Filesystem Kit's existing heap-backed tree removal. Do not add recursive
  traversal in Atlas or Desktop. Remove each matching record only after its
  payload is gone. On failure, report specific remaining item and preserve
  records for retry. Never recurse into `.origins` as a payload.
- A mounted-volume view must remain bound to its exact incarnation throughout
  any operation. Removal or replacement during work returns the provider's
  failure; later media at the same `/usbN` is not treated as continuation.

## Interface contract

File Panel supplies a shared Trash Place or virtual collection adapter, using
its current Grid/List/Columns selection and context-menu semantics. Atlas
contributes Restore, Empty Trash, and Get Information command bindings. Empty
Trash asks for confirmation through the shared dialog control and describes
which volume is affected. Missing original-path metadata is shown as
unrestorable; users may move that item to a chosen folder through existing
Move/Paste. Desktop can expose the same commands without another Trash
implementation.

## Verification gates

Host fault tests cover record write failure, record/payload name collision,
move failure, crash points before and after rename, invalid record paths,
missing parents, occupied restore destinations, symlinks, nested directories,
partial recursive deletion, and exact mount replacement. DE25 tests use RAM0
first, then a user-authorized disposable USB volume. They verify Atlas view,
Restore, Empty Trash, removal/remount, existing file operations, and zero
service faults. No existing production user file is used as trial data.
