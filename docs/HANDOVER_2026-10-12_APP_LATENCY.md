# Doom HDMI audio and Interface Gallery latency

Status: audio, Gallery and quit branding deployed and accepted,
2026-10-12. Worktree is `fs-phase1`, following
committed checkpoint `881e91cef8d8aeb21e0955f670ac7ccd58d5b463`.
Evidence lives on Beast under
`/mnt/Documents/astra68/builds/app-latency-20261011/`.

Current selected release:
`36791f1e24e86a94856446a7389392127f6e6db5fe831d5bd8d3cc88cc97f175`.
Its previous selector retains accepted audio release `8dfdbdaf` below.

## Scope and fixes

Interface Gallery ran nine benchmark suites before creating its window. Normal
launch now builds its controls and opens immediately; `--benchmark` runs the
same nine suites with their original thresholds. The desktop gate rejects
benchmark output on ordinary launch and requires readiness within one second.
The benchmark gate opens the application explicitly from Terminal. Physical
baseline readiness was 5,620,322 and 6,076,308 microseconds. Five subsequent
open/close cycles measured 134,018, 86,704, 85,838, 88,808 and 85,143
microseconds on release `6a0dfc85`; see `board-final-1/gallery.json`.

The first baseline Doom launch exposed a kernel lifetime defect: an empty global
futex queue remained linked after its owner pages were reclaimed. Later wake
traversal raised a process-corruption panic. Cold process reap now retires empty
queues through the existing futex release path before owner reclamation, with
interrupts excluded. The owner-death/wake regression fails before the fix; the
full kernel host suite and MC68040 verification pass afterward. No scheduler,
syscall or user-copy hot path changed.

QEMU previously returned application buffers immediately after copying them
into the mailbox. That permitted SDL's three buffers plus two mailbox buffers
to accumulate. Completion now means the mixer consumed the complete buffer;
copied buffers remain part of the application's group. SDL uses two buffers,
preserving the caller's format, rate and period. The daemon wakes QEMU on every
completed buffer, including the last, and refills a stream that was published
after the initial batch pull. Geometry is validated and cached per generation,
as format and rate already were. Native transport tests cover partial/full
completion, queue bounds, counter wrap and stale generations. Geometry mutation
and mid-batch publication have host regressions.

The remaining periodic producer stall matched SDL_mixer's 250 ms MIDI activity
refresh: its synchronous native status request blocked the PCM callback for
35–45 ms. `astra_midi_active()` now submits one request and collects the existing
validated reply without waiting. `ASTRA_ERROR_WOULD_BLOCK` means the request
remains pending; received service `BUSY` completes it. Other synth operations
drain an outstanding query before sending their own request. SDL keeps its
last answer until a reply arrives, and the native example retries pending
queries. The synth layout and activity contract require Audio.kit 3.0.0 /
`pcm.library.3` / `ASTRA_PCM_3.0`; all current consumers and exports move together.
The PCM service wire protocol does not change, and no ABI 2 alias remains.

PCM conversion sanitizer checks also found zero-length copies with null source
or destination pointers. Existing empty-conversion coverage reproduces both;
copies now run only for nonzero lengths, and sanitizer errors halt the gate.

## Acceptance boundary

The accepted app-fix release is
`8dfdbdaf0f12d7630dccdb562e67f4b13adb1032fd4c86beca580739e29eae29`.
`midi-green-3` passes fatal sanitizers and the full clean production publisher.
`guest-midi-1` passes transport, desktop, all nine Gallery suites, bit-exact SDL
playback, Doom MIDI/SFX/normal quit, 111 Terminal commands, six power actions,
filesystem semantics/recovery/provider fsck, unchanged immutable hashes, and
the packaged NDK's documentation and extracted examples.
`deploy-midi-1` preserves three `/home` and `/local` entries, including metadata
and file contents. The previous mutable disk SHA-256 is
`1a6fe7c4b6b223e8ad5bfb272747ff81230c29c0672d9d9c2febe9124b89f0ee`.

`producer-midi-1` measures 40 seconds with MIDI active: median write interval
23.399 ms, p95 24.714 ms, maximum 26.467 ms, zero stalls over 35 ms and zero
audio faults. The preceding `producer-trace-1` measured 47 such stalls, p95
31.492 ms and maximum 45.142 ms. No host CPU improvement is claimed.
`board-midi-3` passes five Gallery open/close cycles and two ten-shot Doom runs,
each with zero new underruns, overflows or software source gaps. Source
generation is 121.655–172.222 ms after input; host ring intake is
143.467–190.755 ms. Intake includes decoder lookahead and is not speaker onset.
Service PIDs/restart counts and the retained old panic hash/mtime stay unchanged.
The user confirms that gunshots now track muzzle flash through monitor/TV
speakers. No microphone/acoustic timing measurement was performed.

Retain `board-midi-1` and `board-midi-2`: the first measurement tried opening
the exclusively owned capture device, and the second extended the observation
window without updating ring accounting. Both exploratory runs logged one
source gap. Final accepted windows above have none; no all-lifetime zero-gap
claim is made. The optical classifier in `board-midi-3` did not detect flash,
so its absent timing fields are not acceptance evidence. A single-buffer
experiment was stopped before target/hardware qualification after the user's
positive listening result; it is not retained in source or deployed.

The requested quit-prompt change replaces only the English “quit to dos”
confirmation with “quit to Astra” in the prepared pinned upstream copy.
Upstream checkout remains clean, and a changed/missing original prompt fails
preparation. `quit-green-1` passes the full clean publisher and native tests.
`guest-quit-1` proves the new target text, absence of the old DOS prompt,
byte-identical QEMU/audio daemon relative to `8dfdbdaf`, and fresh desktop,
bit-exact SDL, Doom MIDI/SFX/normal quit and filesystem/recovery/fsck gates.
The staged ROM/image hashes remain unchanged.
`deploy-quit-1` preserves all three user entries and archives the previous disk
with SHA-256 `0bde7768e1bdfaf701f368a603be3a83ba7a1efa5147abeadcf757e3dc6160d9`.
QEMU's nine threads remain UID/GID 999, empty groups, zero capabilities and
`NoNewPrivs=1`. Runtime/audio/remote PIDs are 2208646/2208644/2208645, active
with zero restarts. `board-quit-2` captures “PRESS Y TO QUIT TO ASTRA” from
the physical HDMI output and verifies actual Doom process exit after Y/Space,
with service and retained panic evidence unchanged. The initial
`board-quit-1` captured the skill menu and did not prove exit; retain it as
an unaccepted navigation attempt. Use the pinned default F10 quit key for the
final confirmation check. The capture is `docs/images/astra-doom-quit.png`.

The selected board release before the MIDI update was
`6a0dfc856a5cb044c2260d2acbabbff1185819d76b17e35b73d95052b269b995`.
It passed Gallery, guest audio/desktop/filesystem gates and unprivileged QEMU
credentials, but does not qualify Doom audio: the user still hears delay, and
two physical repeated-launch runs each found one software source gap. Retain
those failed checkpoints. No new hardware underrun or overflow occurred.

Original input-to-mixer gunshot measurements were 229–261 ms. The intermediate
release measured roughly 154–183 ms. Those measurements do not establish
muzzle-flash-to-speaker latency: unmodified Doom intentionally waits four
35 Hz tics before its pistol fire action. Neither its game code nor its assets
were changed for the audio fix. The later quit-prompt branding is cosmetic.

Deployment must archive the stopped selected mutable disk and prove its
checksum, update managed products through the canonical image installer, and
compare `/home` and `/local` inode metadata and regular-file content before and
after. The immutable release contains no user data. QEMU must remain UID/GID
999, without supplementary groups or capabilities, with `NoNewPrivs=1`.

The pre-existing broad filesystem performance budgets still fail. These app
fixes do not waive or close that acceptance gate. FPGA route, shell and Linux
driver remain unchanged.
