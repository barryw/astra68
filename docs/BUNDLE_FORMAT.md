# Astra application, Kit, and icon bundle format

**Status:** LOCKED for version 1 (2026-08-16).

## 1. Filesystem is authority

Applications and Kits are ordinary self-contained directories. There is no
installation registry and no uninstall database. Moving a bundle moves one
directory. Removing one moves that directory to `TRASH:` by default. A cache
may accelerate discovery, but it is disposable and must be reconstructible by
scanning manifests.

Installed applications live in `APPS:`. Shared Kits live in `LIBS:`. An
application may carry private libraries in its own `libs/` directory. Desktop
pins are user preferences under `CONFIG:` and are not installation records;
installing an application does not silently pin it.

```
APPS:Terminal.app/
    manifest
    bin/m68k-68030/Terminal
    resources/Terminal.aicon
    libs/                         optional private libraries
    licenses/                     optional notices

LIBS:Graphics.kit/
    manifest
    libraries/graphics.library/abi-1/1.1.0/m68k-68030/graphics.library
    libraries/font.library/abi-1/1.0.0/m68k-68030/font.library
    headers/...                   optional development payloads
    licenses/
```

The suffix is part of the type: `.app` for applications and `.kit` for Kits.
Names shown to a person come from the manifest, not the directory name.

Every `provides NAME ABI VERSION` payload has one canonical location:

```
libraries/NAME/abi-ABI/VERSION/m68k-68030/NAME
```

The image builder validates providers against their embedded library identity
and derives `LIBS:.providers/NAME.abi-ABI`, a bounded exact-identity record for
the newest compatible payload. `OpenLibrary()` reads that record and opens the
canonical bundle-relative payload; older images fall back to scanning the same
authoritative manifests. There are no flattened compatibility copies beside
the Kit and no installation records outside the filesystem.

## 2. Manifest grammar

The `manifest` file is UTF-8, at most 4096 bytes, with LF or CRLF line endings.
It is line-oriented. Tokens are separated by spaces or tabs. `#` begins a
comment outside a quoted token. A quoted token may contain spaces and uses
`\\`, `\"`, `\n`, `\r`, and `\t` escapes. Unknown directives, malformed
lines, duplicate singleton directives, and missing required directives are
errors. Version 1 does not silently accept future syntax.

Common required directives:

```
astra-bundle 1
kind application | kit
id reverse.dns.identifier
name "Human-readable name"
version MAJOR.MINOR.PATCH
```

Application-only required directives:

```
executable relative/path/inside/bundle
icon relative/path/to/icon.aicon
```

Optional repeatable directives:

```
capability NAME
requires library.name ABI MINIMUM.VERSION
provides library.name ABI VERSION
```

A Kit must provide at least one library. An application may request only
capabilities named by its manifest. Paths are bundle-relative: assigns,
absolute paths, backslashes, and `.` or `..` components are forbidden.
Identifiers contain lowercase ASCII letters, digits, hyphens, and non-empty
dot-separated components. Numeric version fields and ABI values are unsigned
16-bit values; ABI zero is invalid. Version `0.0.0` is reserved and invalid.

The launcher's `APP:` assign is a read-only binding to the launched bundle
root. The executable is resolved under that root. Bundle identity, resources,
and private libraries therefore move together without rewriting paths.

## 3. `.aicon` version 1

Application icons use a compact, big-endian, indexed format with one shared
RGBA8 palette. Palette index zero is transparent. Every application icon has
three separately designed square strikes: 16×16 for titlebars, 32×32 for
compact lists, and 64×64 for the desktop. Runtime scaling is not a substitute
for a required strike.

All integers are big-endian. Offsets are from the beginning of the file.

### Header (32 bytes)

| Offset | Type | Meaning |
| ---: | --- | --- |
| 0 | `u32` | magic `AICO` (`0x4149434f`) |
| 4 | `u16` | version `1` |
| 6 | `u16` | header size `32` |
| 8 | `u32` | exact total file size |
| 12 | `u16` | strike count `3` |
| 14 | `u16` | palette entries, 1–256 |
| 16 | `u32` | palette offset |
| 20 | `u32` | strike-table offset |
| 24 | `u32` | first pixel-data offset |
| 28 | `u32` | flags, zero in version 1 |

The palette is `count` consecutive `R,G,B,A` byte records. The strike table
contains three 16-byte records:

| Offset | Type | Meaning |
| ---: | --- | --- |
| 0 | `u16` | width |
| 2 | `u16` | height |
| 4 | `u32` | pixel-data offset |
| 8 | `u32` | pixel-data length, exactly width × height |
| 12 | `u32` | reserved, zero |

Pixels are row-major palette indices. The required strikes may appear in any
order but each size must appear exactly once. A loader validates every offset
and length before exposing pixel data.

## 4. Desktop policy

The system owns the square icon container, grid, selection treatment, and
label metrics. Artwork is expressive inside that container. Version 1 uses
crisp, pixel-aligned rounded corners and the Astra semantic palette rather
than accepting arbitrary icon extents. Terminal is the first factory pin; it
does not make all installed applications appear automatically. The desktop
uses its 64x64 strike and launches the bundle on a primary-button
double-click. A titled application window uses the bundle's 16x16 strike at
the left of the title text; the display service, not the application, renders
that chrome.

## 5. Copy, move, Trash, and dependencies

Bundle tools operate on the directory as one object and validate it before a
mutation. Same-volume movement and Trash use atomic rename. Cross-volume move
is copy, validate the destination, then remove the source. Copy never rewrites
the manifest or bundle contents.

Before permanently deleting a Kit, the tool scans application and Kit
manifests in the selected roots. Deletion is refused when a remaining bundle's
`requires` entry can only be satisfied by that Kit. Moving a Kit within a
searched `LIBS:` root does not change dependency identity. Trash is reversible
and therefore allowed with a dependency warning; permanent emptying requires
the dependency check. Concurrent duplicate destination names are refused,
never overwritten.

The host/installation utility exposes the same rules directly:

```
astra-bundle check BUNDLE
astra-bundle copy SOURCE DESTINATION
astra-bundle move SOURCE DESTINATION
astra-bundle trash BUNDLE TRASH-DIRECTORY [SEARCH-ROOT ...]
astra-bundle delete BUNDLE SEARCH-ROOT [SEARCH-ROOT ...]
```

There is no registration step. Copying or moving a valid bundle into `APPS:`
or `LIBS:` installs it; moving it out or sending it to `TRASH:` removes it
from discovery. Any future discovery index is disposable derived data.
