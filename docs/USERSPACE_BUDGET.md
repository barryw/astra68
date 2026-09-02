# Astra userspace memory and performance budget

Status: active measurement policy.

This document turns "small and fast" into values that must be measured on the
QEMU MC68030 and qualified Arty system. A value may change after evidence and
an explicit design decision; it may not drift silently.

`MEMORY_BUDGET.md` remains authoritative for kernel and physical-memory
accounting. Graphics arenas and surfaces are reported separately from general
heaps.

## 1. Machine envelope

- Astra guest RAM: 128 MiB.
- Graphics RAM: 128 MiB, separate from guest RAM.
- Linux and host services: 256 MiB.
- Primary display: 1280x720 at 60 Hz.
- One RGB565 scanout surface: `1280 * 720 * 2 = 1,843,200` bytes.
- CPU: QEMU TCG MC68030 without hardware FPU, approximately 30 MHz equivalent.

## 2. Resource policy

There are no program-class binary-size ceilings or fixed desktop allotments.
Admission is governed by available charged resources and the kernel's protected
recovery reserves. Every retained build records file sections, detached debug
size, resident pages, peak stack and heap, shared mappings, graphics ownership,
and launch time so regressions remain visible.

## 3. Interaction targets

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

## 4. Frame-path rules

- No unbounded allocation, filesystem operation, network operation, or nested
  synchronous IPC in the compositor frame path.
- Frame-critical buffers, command records, and fence slots are preallocated.
- Damage, commands, and events have hard count and byte limits.
- A missed application frame retains its prior surface.
- A missed compositor deadline increments a visible counter and trace event.
- Cursor and close/scene-switch controls retain progress under ordinary CPU
  saturation.
- Graphics commands use bounded bursts and cannot monopolize SDRAM.

## 5. Launch and shell measurements

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

## 6. Stress acceptance

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
