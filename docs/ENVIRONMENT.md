# Environment variables

Status: design approved 2026-10-06, not built. Phase 1 below; phase 2
(the Get Info editor) has its own spec when it starts.

## The problem

A program started from the desktop gets an empty environment. The kernel and
the launch API carry an environment vector, but the application launch
protocol reserves its `environment_*` fields as "must be zero"
(`sw/include/astra/application_service.h:66-71`) and the supervisor refuses
anything else (`sw/userspace/supervisor/src/loader.c:2508-2510`). Only
Terminal sets variables, by hardcoding six of them for zsh
(`sw/userspace/services/terminal/console_session.c:431-456`). So a port that
asks `getenv("HOME")` from the desktop gets NULL, and Chocolate Doom cannot
find its WAD there.

## The idea: most variables are answers, not settings

Astra is single-user and names its places with assigns (`HOME:`, `TMP:`,
`COMMANDS:`, `STORE:`, `CONFIG:`). Native code uses the assigns directly. Only
POSIX ports ask for HOME, TMPDIR, PATH or USER, and they ask through libc. So
libc answers from the system instead of the system storing copies.

### Synthesized by libc

`getenv(name)` returns an explicit `environ` entry when there is one (a
program's own `setenv`, a test, a zsh `export`). Otherwise, for these names, it
answers from the system:

| Name | Answer | Source |
|---|---|---|
| `HOME` | `/home` | the `HOME:` assign's POSIX path |
| `TMPDIR`, `TMP`, `TEMP` | `/tmp` | `TMP:` |
| `PATH` | `/local/commands:/commands` | the members of `COMMANDS:`, in order |
| `USER`, `LOGNAME` | the user's name | the single user |
| `SHELL` | `/commands/zsh` | the system shell |
| `XDG_CONFIG_HOME` | `/config` | this program's private `CONFIG:` |
| `XDG_DATA_HOME` | `/store` | this program's private `STORE:` |
| `XDG_CACHE_HOME` | `/tmp/<program>` | `TMP:` plus the program's name |

`getpwuid`, `getpwnam` and `getlogin` return the one user, consistent with
`USER` and `HOME`. (Check `sw/userspace/posix/src/identity.c`, where uid
faking already lives.)

What this gives:

- **Nothing to lock.** These names are never stored anywhere a setting could
  override them, so the configuration layers simply refuse them.
- **Always right.** Rebind `HOME:` and `$HOME` follows; nothing to keep in
  sync.
- **Nothing to pass at launch.** A desktop app with an empty `environ` gets
  correct answers. Doom's `$HOME` lookup works from the desktop with no
  supervisor change.
- **Ports land in private storage.** SDL, ScummVM and DevilutionX honour XDG,
  so their config, saves and caches go to each app's own `CONFIG:`, `STORE:`
  and `TMP:` with no per-port glue.

A synthesized name is absent only to code that walks `environ` itself. A child
started with the parent's `environ` synthesizes the same answers in its own
libc, so values still arrive; only listings miss them. The `env` command lists
synthesized entries, marked as such.

Terminal keeps setting `TERM`, which belongs to the terminal. Its other five
hardcoded variables go.

## Real environment: per-app knobs only

What remains as stored variables are genuine per-program settings, such as
`SDL_AUDIODRIVER`, `DOOMWADDIR` or a debug flag. Two layers, later wins:

1. **Developer defaults**: `environment NAME VALUE` lines in the bundle
   manifest (`docs/BUNDLE_FORMAT.md`), like macOS's `LSEnvironment`.
2. **User overrides**: `CONFIG:applications/<Bundle>.app/environment`, in the
   existing `astra-config` key/value format, written atomically by the config
   library (`sw/userspace/config/src/config_library.c`). A line may set a name
   or unset one the manifest set.

There is no system-wide defaults layer. Locale and time zone are synthesized
from the settings Astra already has when a port needs them, and a system
layer can be added when something real needs one.

Names follow the existing grammar (`docs/TEXT_AND_UNICODE.md:13`). Setting a
synthesized name in either layer is refused when it is written and ignored at
launch.

## Launch

- **Desktop, `open`, services**: the supervisor merges the manifest and the
  override file and fills the launch protocol's `environment_*` fields (making
  them live). The launcher does not supply the environment, so an app behaves
  the same wherever it is opened from. `open --env NAME=VALUE` adds per-launch
  values on top.
- **Terminal**: zsh starts with `TERM` only; everything else is synthesized.
  Commands inherit the shell's environment through fork and exec, so `export`
  works as on any Unix.
- **Size**: the environment, arguments and capabilities share the 4 KiB
  startup page. The supervisor refuses an oversized environment with a clear
  error. Growing the page is a later ABI change, made only if real setups need
  it.

## Commands

- `env`: as today, plus synthesized entries marked.
- `env -a <App>`: the merged environment an app will get, with each value's
  source (manifest, override, synthesized).
- `env -a <App> set NAME=VALUE`, `env -a <App> unset NAME`: edit the override
  file.

## Proof

A QEMU gate:

- Chocolate Doom launched from the desktop finds its WAD through `$HOME` or a
  manifest `DOOMWADDIR`.
- A per-app override appears in the program's `environ`.
- Setting `HOME` in the override file is refused.
- `getenv("XDG_DATA_HOME")` in a bundle is `/store`.
- zsh's `echo $HOME $PATH` prints the synthesized values, and `export HOME=/x`
  wins inside that shell.

## Phase 2

- A reusable list/table widget in the toolkit.
- A desktop context menu with a Get Info panel and an Environment tab that
  edits the override file.

The editor writes the same file the `env` command does.

## How others do it

- **Haiku**: a system `SetupEnvironment` script plus a per-user
  `UserSetupEnvironment` hook, inherited by everything the desktop launches.
  No per-app settings (`data/system/boot/SetupEnvironment`,
  `src/kits/app/Roster.cpp:1964`).
- **Windows**: system and user variables, no per-app settings.
- **macOS**: per-app developer defaults in `Info.plist` (`LSEnvironment`), no
  user override.

Astra takes macOS's developer layer, adds a user override per app, and
replaces both the system and user layers with synthesis, because on a
single-user system with assigns those variables carry no information the
system doesn't already have.
