# Atlas acceptance status

Status: in progress, 2026-10-31. This records observed behavior, not an
assumption that every browser path is complete. Current implementation is on
`main`; `docs/CURRENT_STATE.md` holds detailed source and hardware checkpoints.

## Shared implementation boundary

Atlas uses Interface Kit's File Panel, Toolbar Actions, Collection, Table,
Outline Table, Icon Grid, Inline Edit, Context Menu, Status Bar, Breadcrumb,
Search Field, and File Inspector. Common directory snapshot code owns browsing
snapshots. Shared `directory_tree`, `directory_query`, and `path_result_table`
code owns result traversal, metadata matching, and Name/Location Table rows.
Filesystem Kit owns saved query records, recent items, copy/move, Trash, volume
identity, and file type resolution. Atlas owns navigation history, user
commands, and file launch policy. Haiku read-only references include
`src/kits/tracker/PoseView.cpp`, `ContainerWindow.cpp`, and `QueryPoseView.cpp`.

## DE25 core-action gate

Evidence: `/mnt/Documents/astra68/atlas-core-actions-20261031/` on Beast.
Selected RFB raw frames matched board and Beast SHA-256; archive contains
verified `SHA256SUMS` and PNG previews.

| Action | Observed result |
| --- | --- |
| Open RAM0, create New drawer | `Folder1` appeared and shared Inline Edit opened. |
| Rename and open folder | `SweepProbe` appeared; Atlas navigated to `/ram0/SweepProbe`. |
| Create nested folder | `Folder1` appeared inside `SweepProbe` and was renamed `Child`. |
| Copy and paste | `Ctrl+C` on nested `Child`, Back, `Ctrl+V` produced `/ram0/Child` alongside `SweepProbe`. |
| Delete through context menu | `Child` left root and appeared in RAM0 Trash. |
| Delete through toolbar | `SweepProbe` left root and appeared in RAM0 Trash. |
| Empty Trash | Confirmation named `/ram0` Trash; after approval, Trash showed zero items. |

RAM0 was empty before test. Only these disposable folders were created and
removed. Astra, HDMI audio host, and remote desktop stayed active with zero
restarts. Board trial captures and helper were removed after archive.

## DE25 shared result-view gate

Evidence: `/mnt/Documents/astra68/atlas-shared-query-20261031/` on Beast.
Board/Beast SHA-256 matched for four selected RGB frames; archive includes
PNG previews and a verified `SHA256SUMS`.

Atlas saved a temporary `fonts` search for `/dh0`, opened it as a Name/Location
Table with `/dh0/fonts` as the match, then opened that result. The resulting
folder showed both `installed` and `system`; the old search text no longer hid
them. Recent Items used the same Table adapter and showed eight entries.
The temporary saved search was removed through its context menu, leaving no
trial query in HOME. Shared tree/query tests cover nested paths, symlink
non-descent, hidden directories, case-insensitive names, modification-time
filtering, cancellation, read failures, and storage cleanup. Host tests and
address/undefined sanitizers passed. The MC68040 Atlas build and targeted
DE25 install passed. Astra, HDMI audio, and remote desktop remained active,
with zero Astra restarts and 8.2 GiB free. Trial captures and helper were
removed from the board.

Recent Items now refreshes Places selection through the same result-view path
as Saved Searches. DE25 RFB showed Recent highlighted and eight Name/Location
rows after a targeted Atlas install. Board/Beast capture hashes matched;
verified evidence: `/mnt/Documents/astra68/atlas-recent-selection-20261031/`.

## Still unproven or incomplete

- Physical USB and optical browsing, hotplug, ejection, and removal behavior
  need attached devices. DE25 currently sees only its USB root hub.
- Saved Searches still persist only a case-insensitive name substring scoped
  to one directory. Shared metadata query code can filter by modification
  time, but persisted predicates and dynamic local-day refresh do not yet
  exist. Desktop design's `Changed today` example is not functional yet.

