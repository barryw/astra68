# Atlas acceptance status

Status: Atlas implementation complete, 2026-10-31; physical optical regression
pending media. This records observed behavior. Current implementation is on
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
Saved Search also remains highlighted after repeated Atlas repaints: shared
File Panel now preserves a virtual Place on a same-directory refresh. DE25
showed the highlighted `fonts` query and `/dh0/fonts` result across two RFB
frames; the temporary query was removed afterward. Beast host tests and
MC68040 builds passed. Verified evidence:
`/mnt/Documents/astra68/atlas-query-place-20261031/`.

## DE25 physical USB gate

Evidence: `/mnt/Documents/astra68/atlas-usb-physical-20261031/` and
`/mnt/Documents/astra68/atlas-usb-terminal-20261031/` on Beast. Selected raw
frames matched board SHA-256; both archives contain verified checksums and PNGs.

The attached SanDisk Cruzer exposes exFAT `CLIEXFAT` at `/usb0` and FAT32
`CLIFAT` at `/usb2`. Both appeared on Desktop and in Atlas Places with their
volume labels. Atlas browsed each root, created and renamed a folder on
exFAT, copied it to FAT32 through Edit > Copy/Paste, moved only trial folders
to each volume's Trash, and emptied those trial items. FAT32's existing
`ChocolateDoom.app` remained. Right-click Unmount removed the exFAT icon and
made its open Atlas window report the unavailable volume; native `mount 11`
restored the icon and `/usb0`. Terminal opened from Atlas in both USB roots:
`pwd` returned the selected path and external `ls` completed. `cd /dh0`,
`pwd`, and `ls` passed as a fixed-volume regression. Board trial captures were
removed; Astra, audio host, and remote desktop remained active with zero Astra
restarts and 8.2 GiB free.

Current-build read-only regression: with the same stick still attached, Atlas
opened FAT32 `/usb2` and displayed `ChocolateDoom.app`, then opened empty exFAT
`/usb0`. Desktop and Places showed both filesystem labels. Native `volumes`
reported both mounted read/write, and `lsof /usb0` and `lsof /usb2` found no
holders. Board/Beast raw-frame hashes matched; verified RGB/PNG evidence is at
`/mnt/Documents/astra68/atlas-usb-current-20261031/`. No user data changed.

## DE25 Changed today gate

Evidence: `/mnt/Documents/astra68/atlas-today-20261031/` on Beast. Selected
board/Beast raw-frame SHA-256 matched; archive contains PNG previews and a
verified `SHA256SUMS`.

The built-in Query searches Home recursively for entries whose native mtime is
within the current Astra calendar day (UTC on the current DE25 provider).
Atlas created `AtlasTodayProbe` in Home through New drawer and the shared inline
editor. The query displayed its exact `/home/AtlasTodayProbe` path among three
Name/Location rows. Atlas moved only that folder to an initially empty Home
Trash, emptied it through the confirmation dialog, and reopened the query;
the result fell to two rows and the probe was absent. Shared directory query
now obtains time-predicate metadata with native `lstat`, since directory-entry
wire records carry no metadata-validity flags. HostFS and Atlas target builds,
Atlas and VFS host suites, and a targeted two-binary DE25 install passed.
Final exact-source Atlas install opened the query's `Desktop` row into
`/home/Desktop` by double-click.
Astra, audio host, and remote desktop stayed active with zero restarts and
8.1 GiB free. Trial board files were removed.

## Still unproven or incomplete

- Physical optical browsing/ejection and cable-level USB hotplug were not
  repeated with this Atlas build. Earlier DE25 gates passed both with a burned
  ISO9660 DVD-R and literal USB cable replug on 2026-10-24; no optical drive is
  attached now. Current Atlas also passed an image-backed ISO9660 `/cd0` gate
  on 2026-10-31 before the final query change.
- Saved user queries persist a case-insensitive name substring scoped to one
  directory. Richer predicates and local time-zone policy remain future scope.

