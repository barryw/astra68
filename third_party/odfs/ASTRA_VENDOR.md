# ODFileSystem vendor record

## Upstream identity

- Project: [ODFileSystem](https://github.com/reinauer/ODFileSystem)
- Upstream commit: `d4e005c0f047c1d271f556c1f9d93cef307be8ff`
- Upstream commit date: 2026-09-30
- Imported: 2026-10-03
- License: **BSD-2-Clause** (`LICENSE`; SPDX header on every core, backend
  and public header file)

The source was imported without upstream Git metadata. Nothing in the tree
is modified.

## Why

A read-only optical filesystem for Astra's VFS: ISO 9660 (levels 1-3,
multi-extent), High Sierra, Joliet, Rock Ridge, UDF (type-1 partitions:
pressed DVD-era discs, not UDF 2.50+ / Blu-ray), HFS and HFS+ data forks,
multisession and CDDA. `docs/DEVICE_AND_VOLUME_PLAN.md` lists CDFS as a future
VFS adapter; this is its core. **Not wired into any Astra build yet.**

The portable part is `core/`, `backends/` and `include/odfs/`: I/O through
`odfs_media_ops_t` (a sector-read callback), all on-disc fields read with
explicit byte readers (big-endian safe), no globals, state per mount (one
mount is not thread-safe; an adapter holds a lock per mount). Cross-compiled
for the 68040 with `-Os` the core and the ISO/RR/Joliet/UDF backends are
about 28.6 KB of text. It needs `malloc`/`calloc`/`free`, `mem*`, `strcmp`,
`strlen`, `vsnprintf` and libgcc's `__lshrdi3` -- userspace only.

## What was not imported

| Path | Reason |
|---|---|
| `3rdparty/libcodesets` (submodule) | Not BSD-licensed, and not needed: the built-in `core/charset.c` covers UCS-2 and Mac Roman |
| `tests/images/*.iso`, `*.img` | About 10 MB of binary fixtures; `docs/ARTIFACT_POLICY.md` keeps captures out of Git. Fetch them from the upstream commit above into `tests/images/` to run the fixture tests |
| `platform/amiga/**/*.info` | Binary Workbench icons |
| `build/` | Products |

`platform/amiga/` and `tests/amiga/` are kept as text: upstream's host
`make check` builds tests against their headers. Astra does not use the
Amiga handler.

## Verification at import

```sh
grep -rlE "GNU General Public|\bGPL\b|LGPL" third_party/odfs   # no matches
cp -a third_party/odfs /tmp/odfs && make -C /tmp/odfs check   # all suites pass
```

`make check` passed on the Mac (host cc); the fixture tests skip with
"fixture tests/images/... unavailable" because the images are not imported.
Build in a copy: upstream's Makefile writes `build/` inside its own tree.

## Plan for the adapter (not started)

- Media adapter over `AstraBlockDevice`: ISO's 2048-byte sectors on Astra's
  512-byte block transport (LBA x 4), no TOC (session 0, CDDA off), media
  change mapped to `ODFS_ERR_MEDIA_CHANGED`. A second adapter over a VFS
  file handle mounts `.iso` files, the realistic first use.
- A read-only `AstraVfsBackendOps` beside ext4: open through
  `odfs_resolve_path`/`odfs_lookup`, read, stat from node fields, readdir with
  ODFS's resume offset as the cookie, readlink; writes denied.
- Open questions: absolute Rock Ridge symlinks against Astra's `ASSIGN:path`
  form, the per-node size (`ODFS_NAME_MAX` 512 on non-Amiga builds), and
  timestamp conversion to epoch seconds.
