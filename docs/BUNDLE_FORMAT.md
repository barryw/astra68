# Astra application, Kit, and icon bundle format

**Status:** LOCKED for version 1 (2026-08-16).

## 1. Filesystem is authority

Applications and Kits are ordinary self-contained directories. There is no
installation registry and no uninstall database. Moving a bundle moves one
directory. Removing one moves that directory to `TRASH:` by default. A cache
may accelerate discovery, but it is disposable and must be reconstructible by
scanning manifests.

Installed applications live in `APPS:`. Shared Kits live in `LIBS:`. An
application may declare private `provides` entries and carry their exact
libraries under `libs/` using the same canonical layout as a Kit.
The loader resolves those declarations before `LIBS:`. Desktop
pins are user preferences under `CONFIG:` and are not installation records;
installing an application does not silently pin it.

```
APPS:Terminal.app/
    manifest
    bin/m68k-68040/Terminal
    resources/Terminal.aicon
    libs/                         optional private libraries
    licenses/                     optional notices

LIBS:Graphics.kit/
    manifest
    libraries/graphics.library/abi-1/1.1.0/m68k-68040/graphics.library
    libraries/font.library/abi-1/1.0.0/m68k-68040/font.library
    headers/...                   optional development payloads
    licenses/
```

The suffix is part of the type: `.app` for applications and `.kit` for Kits.
Names shown to a person come from the manifest, not the directory name.

Each Kit `provides NAME ABI VERSION` payload has one canonical location:

```
libraries/NAME/abi-ABI/VERSION/m68k-68040/NAME
```

The image builder validates providers against their embedded library identity
and derives `LIBS:.providers/NAME.abi-ABI`, a bounded exact-identity record for
the newest compatible payload. The supervisor uses that record while resolving
an executable's eager `DT_NEEDED` closure and gives the process loader only the
validated, exact library identities selected for that launch. There are no
flattened compatibility copies beside the Kit, no current-directory search,
and no installation records outside the filesystem.

## 2. Manifest grammar

The `manifest` file is UTF-8 with LF or CRLF line endings. It is line-oriented.
Tokens are separated by spaces or tabs. `#` begins a
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

An application may declare each content type it can open with a repeatable
`handles MIME-TYPE [EXTENSION]` directive. MIME types use lowercase ASCII
type/subtype identifiers. An optional lowercase extension starts with `.`;
it is a hint for content that File Types cannot identify. For example:

```
handles application/x-zmachine-story .z3
handles application/x-zmachine-story .z4
handles application/x-zmachine-story .z5
handles application/x-zmachine-story .z6
handles application/x-zmachine-story .z8
handles application/x-blorb .blorb
```

The same extension cannot map to two types within one bundle. If installed
bundles disagree, File Types leaves that extension unknown. Content recognition
always takes precedence. Declarations describe capability; installation does
not change an existing user default. Kits cannot declare document types.

Optional repeatable directives:

```
capability NAME
requires library.name ABI MINIMUM.VERSION
provides library.name ABI VERSION
```

Optional application-only singleton directive (system.library 2.7):

```
single-instance
```

One copy of the application runs at a time. Opening it again while it runs
starts nothing: the supervisor asks the display, through the application's
GUI session, to restore and raise its windows, and answers the launch with
the running process. Games that take over the machine use it.

An application may ship first preferences in `resources/defaults/`. When the
supervisor first creates the application's `STORE:`, it copies those files
into it; from then on they are the application's own, and later launches
never overwrite them. Removing the store resets it to the defaults.

`STORE:` is private persistent application data at
`HOME:/Application Data/<bundle id>/`, keyed by the manifest's stable `id`.
Moving, renaming, or uninstalling an application bundle does not move or
delete its data. On first launch after this layout change, the supervisor
moves an existing bundle-name store into that directory without rewriting
its contents. If both locations already contain data, launch fails rather
than choosing one silently. SDL's `SDL_GetPrefPath()` returns `/store/`, so
SDL games use this location without port-specific save-path code. Native apps
use `STORE:` for private data; user documents belong in `HOME:` and should be
chosen through a file dialog when one is available. Programs that write
arbitrary paths cannot be redirected safely by the OS without changing their
code or launch configuration.

Game ports should make their existing save-directory selector use `STORE:`
(or `/store/` in POSIX code). The engine keeps its own file format, slot names,
and save/load logic. This is the one port boundary that may need adaptation;
the OS cannot infer which arbitrary file write is a saved game. Port-specific
subdirectories stay beneath the same application data root, so uninstall and
moving the bundle leave saves in place. A shared Save dialog is for apps that
let users choose document paths, not an alternate game-save backend.

A Kit must provide at least one library. An application may request only
capabilities named by its manifest. Paths are bundle-relative: assigns,
absolute paths, backslashes, and `.` or `..` components are forbidden.
Identifiers contain lowercase ASCII letters, digits, hyphens, and non-empty
dot-separated components. Numeric version fields and ABI values are unsigned
16-bit values; ABI zero is invalid. Version `0.0.0` is reserved and invalid.

The launcher's `APP:` assign is a read-only binding to the launched bundle
root. The executable is resolved under that root. Bundle identity, resources,
and private libraries therefore move together without rewriting paths.
For a private declaration `provides NAME ABI VERSION`, the payload lives at
`libs/NAME/abi-ABI/VERSION/m68k-68040/NAME`. Its embedded library
identity must match. A missing or damaged declared payload fails launch; the
loader does not silently substitute `LIBS:`.

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
that chrome. While an application starts, the display shows a launch panel
with its 64x64 strike and its manifest `name`, from the moment the
supervisor opens its GUI session until the application presents its first
frame or exits. The application does nothing for it.

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
