# Astra userspace memory and performance budget

Status: provisional design gates. No userspace measurements exist yet.

This document turns "small and fast" into values that must be measured. The
first implementation establishes baselines on Musashi, pin-level RTL where
practical, and the qualified ULX3S build. A value may change after evidence and
an explicit design decision; it may not drift silently.

`MEMORY_BUDGET.md` remains authoritative for kernel and physical-memory
accounting. Graphics arenas and surfaces are reported separately from general
heaps.

## 1. Machine envelope

- Physical SDRAM: 32 MiB.
- Primary display: 720x480 at 60 Hz.
- One RGB565 scanout surface: `720 * 480 * 2 = 691,200` bytes.
- Required double scanout: `1,382,400` bytes.
- Any third scanout surface costs another 691,200 bytes and requires measured
  justification.
- CPU: 12.5 MHz MC68030-class core without hardware FPU.

## 2. Warm desktop envelope

The first full workspace plus one idle terminal targets this committed-memory
envelope:

| Category | Initial ceiling |
|---|---:|
| kernel, wired tables/stacks and emergency core | 2 MiB |
| two scanout surfaces | 1,382,400 B exact |
| workspace and terminal off-screen surfaces | 1.5 MiB |
| core userspace private pages | 5 MiB |
| physically shared Kit/code/read-only pages | 2 MiB |
| reclaimable filesystem/font/icon/query caches | 3 MiB warm |
| emergency system reserve | 1 MiB minimum |
| uncommitted memory left for applications | 16 MiB target |

The committed ceilings above total 16,586,752 bytes and leave 16,967,680 bytes
before the 16 MiB application-availability target is applied. The remaining
190,464 bytes are margin, not a new allocation category.

These categories must not double-count shared pages. Process reports show both
private committed bytes and proportional/shared bytes. Graphics surfaces are
charged to their owning service or application and shown separately.

If the initial implementation cannot fit this envelope, report the exact
owners and working sets before changing a ceiling. The system may reclaim warm
caches under pressure; it may not consume the emergency reserve for ordinary
desktop decoration.

## 3. Binary-size targets

With shared Kits available, initial stripped text plus read-only-data targets
are:

| Program class | Target |
|---|---:|
| tiny command-line utility | <= 32 KiB |
| ordinary command-line utility | <= 96 KiB |
| small native GUI executable, excluding resources and shared Kits | <= 128 KiB |
| base service client library veneer | <= 32 KiB per Kit |

These are targets, not excuses to distort a program into unsafe code. A larger
binary requires a section-level report and explanation. zsh, Vim, servers, and
complex media tools receive measured component budgets after their first clean
ports; they do not receive an unlimited exception.

Every retained build records total file size, text, read-only data, writable
data, BSS, relocations, detached debug size, and resident pages.

## 4. Interaction targets

At 60 Hz one frame is approximately 16.67 ms.

| Operation | Initial target |
|---|---:|
| hardware input report to pointer presentation, p99 | <= 1 frame |
| key/button to ordinary visible UI response, p99 | <= 2 frames |
| compositor frame work, p99 | <= 12 ms |
| continuous window move with background CPU load | 60 presented frames/s |
| warm terminal-window creation to usable input | <= 250 ms |
| terminal key to glyph presentation, p99 | <= 2 frames |
| warm default-shell prompt | <= 250 ms |
| cold default-shell prompt from cached storage metadata | <= 1 s |

An operation that legitimately takes longer returns control and progress
asynchronously. It does not extend the input or compositor deadline.

## 5. Frame-path rules

- No unbounded allocation, filesystem operation, network operation, or nested
  synchronous IPC in the compositor frame path.
- Frame-critical buffers, command records, and fence slots are preallocated.
- Damage, commands, and events have hard count and byte limits.
- A missed application frame retains its prior surface.
- A missed compositor deadline increments a visible counter and trace event.
- Cursor and close/scene-switch controls retain progress under ordinary CPU
  saturation.
- Graphics commands use bounded bursts and cannot monopolize SDRAM.

## 6. Launch and shell measurements

For every application and shell build, measure:

- registrar request to first user instruction;
- first instruction to application-ready;
- application-ready to first completed frame or prompt;
- page allocation and mapping counts;
- bytes read from storage;
- relocations and cache-synchronization cycles;
- startup messages and service round trips;
- private/shared resident pages after idle;
- peak stack and heap during startup.

The zsh port separately records `posix_spawn`, process clone, first copy-on-
write fault, simple external command, pipeline, command substitution, and
completion initialization costs.

## 7. Stress acceptance

The desktop and terminal budgets are tested while:

- all ordinary-priority CPU time is consumed;
- storage queues are full or the media disappears;
- network traffic saturates its transport credits;
- one application stops reading events;
- one application repeatedly crashes and restarts;
- graphics jobs time out or are reset;
- memory falls to the emergency threshold.

The expected result is bounded degradation, explicit errors, and retained
input/display/inspector progress. Queue growth, monotonic memory growth, hidden
frame misses, and global stalls are failures.
