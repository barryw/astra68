# Atlas acceptance status

Status: in progress, 2026-10-31. This records observed behavior, not an
assumption that every browser path is complete. Current implementation is on
`main`; `docs/CURRENT_STATE.md` holds detailed source and hardware checkpoints.

## Shared implementation boundary

Atlas uses Interface Kit's File Panel, Toolbar Actions, Collection, Table,
Outline Table, Icon Grid, Inline Edit, Context Menu, Status Bar, Breadcrumb,
Search Field, and File Inspector. Common directory snapshot code owns browsing
snapshots. Filesystem Kit owns saved query records, recent items, copy/move,
Trash, volume identity, and file type resolution. Atlas owns navigation
history, user commands, and file launch policy. Haiku read-only references include
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

## Still unproven or incomplete

- Physical USB and optical browsing, hotplug, ejection, and removal behavior
  need attached devices. DE25 currently sees only its USB root hub.
- Saved Searches currently store a case-insensitive name substring in one
  directory. Desktop design shows `Changed today` as a query example. A
  reusable metadata query contract and result view are needed before that
  example can work as a real search.

