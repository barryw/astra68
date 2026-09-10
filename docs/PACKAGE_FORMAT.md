# Astra package format

**Status:** LOCKED for version 1.

## 1. Role

`.apkg` is Astra's software transport and installation format. Packages are
the standard way to install kernel/system images, the NDK, Kits, libraries,
services, commands, applications, development files, debug symbols, and
documentation. Application (`.app`) and Kit (`.kit`) bundles remain installed
discovery units; a package may contain any number of them plus a private POSIX
prefix tree.

`COMMANDS:`, `APPS:`, and `LIBS:` are active namespaces, not package storage.
The package service installs immutable payloads and atomically publishes only
the commands, bundles, services, and libraries declared by their manifests.
Private executables and data remain inside the package root.

## 2. Container

An `.apkg` is a reproducible POSIX pax archive compressed with Zstandard. Its
top-level layout is:

```text
manifest
files.sha256
payload/
signatures/ed25519/KEY-ID       optional for local development
```

Archive entries are lexically ordered and use timestamp zero, numeric owner
and group zero, and normalized modes. Only directories, regular files, and
symbolic links are valid. Absolute paths, empty components, `.` or `..`, hard
links, devices, FIFOs, sockets, set-id bits, and links escaping `payload/` are
invalid. Extraction occurs only into a new staging directory using descriptor-
relative operations; validation precedes publication.

`files.sha256` is the canonical, path-sorted ledger of every entry below
`payload/`, including type, mode, byte length, SHA-256 digest, and link target
where applicable. A signature covers the exact bytes of `manifest`, a single
LF, and `files.sha256`. Repository packages require a signature from the
selected trust policy. Unsigned packages require an explicit development
installation capability.

## 3. Manifest

The manifest reuses the bundle manifest's UTF-8, line-oriented token and quote
grammar. Version 1 requires:

```text
astra-package 1
id reverse.dns.identifier
version MAJOR.MINOR.PATCH
architecture m68k-68040 | noarch
```

It may repeat these directives:

```text
requires-package ID MINIMUM.VERSION MAXIMUM.EXCLUSIVE | *
requires-library NAME ABI MINIMUM.VERSION
bundle RELATIVE.PAYLOAD.PATH
command NAME native | posix RELATIVE.EXECUTABLE.PATH
service NAME RELATIVE.BUNDLE.OR.EXECUTABLE.PATH
posix-root RELATIVE.DIRECTORY.PATH
config-default CONFIG:RELATIVE.PATH RELATIVE.PAYLOAD.PATH SCHEMA.VERSION
```

Unknown directives and conflicting exports are errors. Package versions use
`MAJOR.MINOR.PATCH`; compatibility remains a separate kernel ABI, NDK API,
library ABI, service protocol, architecture, or hardware constraint. The
solver records the exact selected package/library versions and content digests
in the installed generation.

A POSIX root may contain its ordinary `bin/`, `lib/`, `share/`, `include/`,
and other package-private directories. It is never unpacked over a global Unix
tree. At launch the POSIX runtime constructs `/bin`, `/usr`, and related views
from the program's resolved package closure. Writable configuration and state
are separate declared capabilities; immutable package defaults never overwrite
locally modified configuration.

## 4. Transactions and generations

The package service resolves and validates the complete closure, stages every
immutable payload, builds the new `COMMANDS:`, `APPS:`, `LIBS:`, and service
views, then selects the generation with one atomic commit. Failure leaves the
previous generation selected. Rollback selects a previous complete generation;
it does not execute downgrade scripts.

Packages never run unrestricted maintainer scripts. Configuration changes and
service activation use versioned, declarative package-service operations.
Payloads are collected only when no installed generation, bundle dependency,
or running process references their exact identities.

Debian and RPM archives may be host-side porting inputs. Their global paths,
dependency identities, ownership assumptions, and maintainer scripts are not
native Astra installation semantics; a porting tool must produce and validate
an `.apkg`.
