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
multisession and CDDA. This is the portable core for Astra's CDFS adapter.
Astra builds the portable reader privately
into `sw/userspace/services/cdfs`; the upstream sources remain unchanged.

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

## Astra adapter

`sw/userspace/vfs/src/backends/cdfs.c` implements the read-only backend;
`cdfs_media.c` supplies borrowed `AstraBlockDevice` and native `AstraFile`
sources. Sector conversion respects transfer limits. The adapter checks the
source before cached operations and permanently invalidates changed media.
ODFS's directory resume value is the opaque VFS cookie. Metadata dates become
UTC epoch seconds; fields absent from ODFS's public node remain unknown.

The upstream host path helper lacks the required `open_at`, dot and trailing
separator semantics, so the adapter uses the existing ODFS component lookup
and parent resolver. The common Astra namespace layer owns symlink traversal
and authority; Rock Ridge targets remain byte-exact. Astra currently permits
63-byte UTF-8 names, so larger names fail without truncation. ODFS retains its
512-byte node-name representation.

The optional image service publishes `/cd0`, using the normal VFS sessions,
private service configuration and shutdown contract. Native builds remap
ODFS's standard allocation names to the canonical runtime allocator through
an Astra-owned forced-include header; no POSIX library or Amiga handler is
linked. Parser logging and CDDA are disabled. TOC/session/eject transport and
physical optical-drive qualification await the native device layer.
See `sw/userspace/services/cdfs/README.md` for usage and checks.
