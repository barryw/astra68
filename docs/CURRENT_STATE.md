# Astra 68 current engineering state

Status: active continuation map, 2026-10-28

AstraZ transcript checkpoint, 2026-10-28: the app now appends output to its
existing TextModel and rebuilds only after erasure, transcript rollover, or
prompt-prefix edits. Its existing 33 ms flush gate still bounds display posts;
full-window redraw at each post has not been performance-qualified. Haiku
reference: `src/apps/terminal/TermView.cpp` uses `_InvalidateTextRange` for
targeted updates; Astra currently retains shared Text View layout and draw-list
rendering while reducing repeated transcript copies. The host transcript test
was red before the sync function and passes on Mac and Beast. A focused AstraZ
install on DE25 opened Zork I from WORK, displayed the complete opening, and
advanced `open mailbox` to Moves 1 with the leaflet visible. Three board
services are active with zero restarts; panic log timestamp did not change.
Evidence: Beast `/home/barry/astra68/build/audit-de25-20261028/astraz-incremental-open.png`
and `astraz-incremental-move.png`. Beast's ad hoc QEMU transcript baseline could
not boot: unmodified prepared gate image also stopped at
`launch-failed /services/posixd status=11`. That gate needs a coherent fixture
before transcript performance qualification. Focused app update has not yet
been captured in a clean selected release image.

Selected release checkpoint, 2026-10-28: PR #7 merged to `main` at
`3cea11f87ce62fdaf658b94b1043d9fba109af75`. Beast clean publisher staged
content identity `68b963418420771d2315ff93db998812d7880241382bd041621148f06a724363`
from source commit `d0dadd513257dea3e267389c34b27acec1046228`, using the clean pinned
FluidSynth checkout at `/home/barry/Git/fluidsynth`. Candidate ROM and image
passed the full 117-command Terminal gate under native QEMU. Guarded DE25
release deployment migrated release-owned files into the persistent image,
verified the installed release, confirmed the unchanged packaged kernel, and
passed runtime/audio/remote-desktop gates. The board selector now points to
that identity; all three services are active with zero restarts, RFB listens
only on 127.0.0.1:5900, root has 8.3 GiB free, and the panic log timestamp
remains 2026-10-28 09:38 +0800. AstraZ empty-state GUI screenshot from the
selected release matches the focused-install capture byte-for-byte. Evidence:
Beast `/home/barry/astra68/build/audit-de25-20261028/` and
`/var/tmp/astra-audit-release-20261028/`. The first clean publisher attempt
stopped before release packaging because `FLUIDSYNTH_ROOT` was omitted; the
retry passed with the pinned checkout and explicit ROM/kernel source identity.

Audit correctness checkpoint, 2026-10-28: nine Ponytail findings were fixed
at existing native seams. Beast host tests, NDK `dist-check`, and the full
native QEMU Terminal gate (`ASTRA TERMINAL PASS 117 commands`) passed. DE25
focused install passed with Interface Kit 7.9, AstraZ, Package/Filesystem/
Runtime Kits, and the hardened remote desktop. An initial hardware trial
found Grid View measured zero height for an empty upper window; a shared
one-pixel minimum and a zero-row regression test fixed AstraZ startup. DE25
then showed Zork I with full title, inverted status line, and inline caret,
plus Beyond Zork Z5's colored Character Setup upper window. RFB binds only
127.0.0.1:5900. Astra, audio, and remote desktop services are active with
zero restarts and 8.3 GiB free; board trial files were removed. Evidence:
Beast `/home/barry/astra68/build/audit-de25-20261028/`. Dev installer now
holds a separate transaction lock through boot verification, and carries
changed systemd units with matching host binaries. It validates unit content
before changing the active release link. Performance qualification of AstraZ
transcript redraw remains separate from this correctness gate.

Interface Kit 7.8 cursor/status checkpoint, 2026-10-27: shared TextSurface
defines underline as `ASTRA_TEXT_CARET_STANDARD`. Common Field and Terminal
call that renderer; AstraZ's inline prompt inherits it. Shared Text View
inversion renders AstraZ's Z1–3 status line with light text on dark ground.
Haiku references: `src/kits/interface/TextControl.cpp` for common editing and
`src/apps/terminal/TermView.cpp` for caret presentation. Beast Interface Kit
tests, Terminal tests, AstraZ tests, and NDK `dist-check` passed. Selective
DE25 update installed Interface Kit, Terminal, and AstraZ together. Physical
RFB screen showed an underline caret after `>`, inverted West of House / Score
/ Moves status line, and `open mailbox` changed Moves 0→1 while revealing the
leaflet. Evidence: Beast
`/home/barry/astra-evidence/astraz-platform-caret-20261027/`.
Follow-up DE25 correction, 2026-10-28: shared TextSurface now centers glyphs
within their line height and offsets the standard underline caret by the same
amount. Inline Field and Text View use matching centered origins; this fixes
status text pinned toward the top of its dark bar while retaining alignment
of the caret with `>`. Earlier row-top alignment was a temporary checkpoint.
Frotz's private `os_display_string` and `os_string_width` now consume a zero
style/font argument instead of mistaking it for string end. DE25 native CLI
probe and GUI both show full `ZORK I` title; GUI `open mailbox` advances Moves
0→1. Beast Interface Kit and AstraZ tests/builds passed. Probe removed with
digest guard; final GUI remains open. Board services active, zero restarts,
8.3 GiB free, one development release. Evidence: Beast
`/home/barry/astra-evidence/astraz-platform-caret-20261027/`.

AstraZ GUI checkpoint, 2026-10-27: Interface Kit 7.7 adds a borderless inline
Field using the existing Field editor and caret. AstraZ composes it with
TextView, Container, ScrollView, and Toolbar, so input follows `>` in the
scrolling transcript. Frotz window 7 drives a fixed Z1–3 status line. DE25
ran real Zork I: opening room, `open mailbox`, leaflet output, and Moves 0→1;
Deadline displayed the room/Time variant. Launch through Terminal's `open`
returns focus to Terminal; clicking AstraZ then restores prompt focus on its
active window state. Selective guest installs passed, and the previous
display panic log timestamp did not advance. Evidence: Beast
`/home/barry/astra-evidence/astraz-inline-status-20261027/`. V4+ upper-window
layouts, Glulx, and Z6 picture/sound remain open; see `docs/ASTRAZ.md`.

AstraZ private interface, 2026-10-27: `astraz_engine` now exposes only an
app-private probe/run callback interface. It covers Frotz Z1–8 input,
screen/window events, files, pictures, sound, mouse, and cancellation, with
host capability checks. Frotz's unimplemented optional `make_menu` stays
disabled. Native DE25 session client played Mini Zork Z3, saved at move 1,
advanced to move 2, then restored move 1. Native DE25 contract test passed
synthetic bare Z1–8 and Z-code/Glulx Blorb probes plus malformed cases.
Native DE25 session client also played a Z5 Adventure story through a `look`
turn after its title pagination. Test client now marks `More` pauses explicitly.
Evidence: Beast `/var/tmp/astra-astraz-private-interface-evidence-20261027/`.
Both test-only binaries and board scratch were removed with guarded removal;
all three services active, board root 8.3 GiB free. At that checkpoint, real
story execution for Z1–2, Z4, and Z6–8, V6 graphics/sound, Glulx backend,
and native GUI remained open.

AstraZ first checkpoint, 2026-10-27: Filesystem Kit 15.5 identifies bare
Z-machine, Glulx, and Blorb content for shared file associations. Interpreter
dispatch and Blorb executable parsing belong to AstraZ, with no story engine
NDK API. Frotz 2.56pre source is pinned under `third_party/frotz`; common and
Blorb code build as one AstraZ-private MC68040 static archive through NDK
CMake/Ninja. Test-only upstream
CLI on DE25 ran Mini Zork Z3, displayed West of House, and executed `open
mailbox` with expected leaflet output. Trial evidence is at Beast
`/var/tmp/astra-astraz-evidence-20261027/de25-z3-first-move.png` and
`de25-z3-private-engine.png`; board
scratch files and the test-only guest CLI were removed with the checksum-guarded
development installer. Final Filesystem Kit 15.5 was installed through
the development installer after Beast file-type, library-contract, and NDK
`dist-check` passed. Astra, audio host, and remote desktop services remain
active with 8.3 GiB free. At that checkpoint, no native AstraZ GUI, session
adapter, or Glulx backend was installed. See `docs/ASTRAZ.md` for current gates.
The corrected app-private archive and private File Types boundary passed a
second DE25 Z3 input trial. NDK distribution contains no story header and
Filesystem Kit exports no story inspector. Trial CLI was removed again.

File types, 2026-10-27: Haiku-style native slice is installed on DE25.
Filesystem Kit 15.4 identifies bounded Z-machine and Blorb content, uses an
`.apkg` built-in hint, then resolves unknown content using current bundle
`handles MIME-TYPE .extension` declarations. System library 4.4 parses these
declarations. Open With resolves installed handlers and Config Kit defaults;
Atlas routes file double-clicks to it. Beast bundle, catalog, file-type,
development-installer, and NDK dist checks passed. DE25 opened unknown
`/dh0/astra_events.cat` in shared chooser; Cancel returned cleanly. A valid
Z3 story in WORK opened Open With and showed no installed handler, as expected
before AstraZ exists. Trial found that Open With needed `WORK:r` in both its
bundle and supervisor ceiling; both are updated. Board services remain active,
root has 8.3 GiB free, and trial files were removed after evidence archival
at `/var/tmp/astra-file-types-evidence-20261027/` and
`/var/tmp/astra-file-types-extension-evidence-20261027/` on Beast. VFS typed
attributes, generic content-rule registration, and AstraZ launch/default
tests remain open. See `docs/FILE_TYPES.md`.

Development deployment, 2026-10-27: DE25 is an active development machine;
the selected release below is a checkpoint, not a production constraint.
`tools/de25-dev.py` now mirrors source, builds a selected target on Beast, and
installs only named guest or host artifacts on DE25. Guest files update the
persistent image after the storage provider stops; host files use a hard-linked
development release. The installer keeps changed files installed and verifies
a new QEMU process plus HOST0 boot. Desktop guest and Codec worker host probes
completed in about 31 and 39 seconds respectively, with all three services
active. See `docs/DE25_DEVELOPMENT_LOOP.md`. Complete images are checkpoints
after fixes pass hardware trials.
Checksum-guarded removal of one test-only `/commands` artifact passed on DE25
without touching user paths; the new boot and all three services passed.

Board release checkpoint, 2026-10-27: PR #4 merged the certified Codec Kit,
SDL_mixer Ogg preroll, and Chocolate Quake support into `main` at
`d7cdc9164085a1a3aaa857e8dae0adf4ab628b18`. Beast rebuilt the full DE25
software release from that merged tree; content identity
`b46aaf2de9af55540162e42ac694cd04e96b48527f4d471201046c509bf98270`
is now selected on DE25. Guarded deployment verified the running kernel
package, migrated release-owned system files into the persistent volume while
retaining user-owned paths, and passed QEMU, HOST0, audio host, remote desktop,
and kernel confirmation gates. All three Astra services remained active after
deployment; a 15-second production `astra-top` sample reported zero audio
underruns or software gaps. Previous release
`8b9c771f5bb9ee071306270565f40c007b5909bbd1fd9b19bf58b230f745d04e`
remains selected as `previous`; board root has 8.4 GiB free. Beast evidence:
`/var/tmp/astra-main-production-build.log` and
`/var/tmp/astra-main-production-deploy.log`. Asset-backed Quake trial used the
same software tree before promotion; commercial assets were not copied into
production storage.

Codec audio follow-up, 2026-10-27: SDL_mixer now requests HPS Ogg decode from
a separate thread and feeds its existing audio stream, so its callback never
waits for Codec Kit. A separate PCM voice trial was rejected: 349 software gaps in one
15-second gameplay window and 1,708 over the trial. Revised adapter passed
the QEMU SDL_mixer probe and guarded DE25 Quake trials. Initial 128 KiB queue
had 59 music callback underflows despite two host-gap-free gameplay windows.
With a 512 KiB queue, only one startup burst of two underflows remained;
two later 15-second windows had zero host source gaps and zero HDMI underruns.
SDL_mixer calls `Seek(0)` after `Play`; the adapter's first preroll was cleared
by that seek. The adapter now waits for 128 KiB of fresh PCM after seek, with
a 3-second failure limit. Exact release
`036227968411287ff384cce78b66c0a24f5d520698dbdf7ca25ad814709885aa`
passed QEMU SDL_mixer (with Beast's x86 worker) and native Codec Kit WAV/video
gates. Guarded DE25 trials with that release passed native WAV bytes, YUV4MPEG2
frame bytes/stride/timestamp, seek, and close, then 180 seconds of asset-backed
Quake. Quake closed its Ogg track with zero guest music underflows and 58 ms
preroll wait. A 20-second HDMI capture had zero underruns, overflows, or gaps,
and no silence of 5 ms at -90 dB; mean level was -12.8 dB. Two 15-second
gameplay windows had zero host gaps, 16.39 and 19.25 presents/s, 81.4% and
80.6% Quake guest CPU, and 18.6 MiB resident memory. User heard continuous
music and effects through HDMI speakers. An earlier candidate had one host
gap in a startup window, so that transition is not yet established gap-free.
Both exact-release trials restored then-selected production release
`8b9c771f5bb9ee071306270565f40c007b5909bbd1fd9b19bf58b230f745d04e`
and all three services; production storage was untouched. Evidence: Beast
`/var/tmp/astra-codec-voice-evidence-20261027/`. The candidate was later
rebuilt from merged `main` and promoted as recorded above.

Codec Kit checkpoint, 2026-10-26: native `codec.library.1` and `CODEC`
service route bounded source reads through existing HOST0 transport to a
seccomp-isolated AArch64 worker. Content-selected WAV PCM, Ogg Vorbis, and
YUV4MPEG2 video providers share open/read/seek/close; SDL_mixer's Ogg adapter
uses Codec Kit. Beast host, AArch64, QEMU native, SDL_mixer, and NDK gates
passed. Three initial guarded DE25 private-image trials passed: native certifier
decoded WAV and video with frame metadata and seek; SDL_mixer played Ogg,
MP3, and FLAC; asset-backed Quake ran with its music track found. Quake's two
15-second windows measured 21.91 and 15.66 presents/s, 20 and 0 software
audio gaps, and zero hardware underruns. Guest Quake CPU was 76.5% and 82.9%;
resident memory was 17.0 and 18.2 MiB. Final clean staged release identity
`0170a92de17493885576ca6a79e34703f49cf47b7c2e506fc958c0a69f1a8be2`
contains the tested worker. The exact release passed a further guarded Quake
trial and native WAV/video certifier trial. Quake's final two 15-second windows
measured 22.80 and 18.25 presents/s, 20 and 18 software audio gaps, and zero
hardware underruns. All 107 host source-gap reports in the final music trial
were for the 44.1 kHz stream. This still fails smooth-music qualification.
The final native certifier logged `ASTRA CODEC LIBRARY PASS` for WAV bytes,
video frame bytes/stride/timestamp, seek, and close. Its one-shot exit races
the supervisor startup handshake, which also logs `launch-failed status=16`;
the certifier process exits successfully. No trial panic was logged. At this
earlier checkpoint, candidate release was staged privately; selected production
release was
`8b9c771f5bb9ee071306270565f40c007b5909bbd1fd9b19bf58b230f745d04e`
and production services recovered. The 2026-10-27 qualification and promotion
above supersede these open gates.
Final board trial files were removed after evidence capture; `/var/tmp` has
8.3 GiB free. The worker now enforces declared source length for every
provider, and host sessions can reopen after close. Their regression tests
passed on Beast, including AArch64 provider and sandbox checks.
The video provider does not yet present to a Display-owned surface. Providers
are compiled into the isolated worker; installable provider registration is
still design work. Evidence: Beast `/var/tmp/astra-codec-board-evidence/`.
See `docs/CODEC_ARCHITECTURE.md`.

Chocolate Quake 2.1.0 checkpoint, 2026-10-26: pinned upstream builds through
NDK CMake/Ninja into checked MC68040 executable and `ChocolateQuake.app`.
Standard SDL Kit supplies MP3, Ogg Vorbis, FLAC and WAV through SDL2_mixer;
NDK `dist-check` and DE25 CMake codec probe passed. Quake searches bundled
`resources/id1` and writable `/store/id1`. A user-supplied registered
`pak0.pak` plus `track02.ogg` were installed in a private copied-image trial,
never in source or production storage. DE25 reached `Host_Init`, initialized
effects and Ogg music, and showed changing Quake gameplay in two FPGA HDMI
captures. Those early trials did not establish sustained music playback:
Quake paused its mixer about 0.3 seconds after track start. No panic occurred.
Quake's fullscreen request needed SDL2 2.32.11;
its bundle now requires that version. SDL Kit also requires system.library
4.3.0 for gamepad symbols, so both Kits must be released together.
Startup services now complete before boot applications regardless of manifest
line order. Desktop reports ready after its first frame and startup sound
drain. DE25 trial deliberately listed Quake before Desktop yet logged sound
completion before Quake launch. Production release and image hash remained
unchanged; all three board services returned active. First 60-second telemetry
run measured about 7-14 presents/s after initial startup, Quake 63-86% guest
CPU in most windows, and 49 software audio source gaps despite zero guest
underruns. User observed several multi-second pauses. Two follow-up trials
sampled 90 warm and 60 cold one-second windows with temporary generic SDL
frame timing.
Neither logged a post-startup gap or present call over 500 ms; one warm
zero-present window occurred during initial map loading before first frame.
The cold run averaged 13 presents/s and recorded 42 software audio gaps.
An A/B trial on the same private image and production ROM changed Quake's
effects mix default from 44,100 to 11,025 Hz, leaving music at 44,100 Hz.
Across 12 five-second gameplay windows each, mean presents rose from 10.44
to 19.58/s (+88%); software audio gaps fell from 31 to 13, with zero guest
underruns in both runs. These are effects-only measurements because music was
paused. User confirmed effects sounded good at 11,025 Hz. Source change is in
Quake's port adapter, not SDL or FPGA. Scene progression differed because the
faster build advanced farther through the demo.
Music root cause was Quake's volume adapter rewriting the already-valid 1.0
cvar during first update, then taking a zero-volume pause branch despite
mix volume 128. Adapter now writes the cvar only when clamping changed it and
uses the calculated integer mixer level to decide pause/resume. A DE25 trial
kept music unpaused; decoder position advanced 3.44 seconds in five seconds
with SDL_mixer's STB Vorbis, but audio host reported 242 software gaps in 15
seconds. A fixed-point Tremor experiment advanced 4.22 seconds in five and
reduced gaps to 15, still short of clean playback. Neither decoder change is
retained in the standard SDL Kit. At that checkpoint, music decode needed
offload or a measured equivalent; Codec Kit now supplies it. All trials used
private images and restored
production services; production storage and release were unchanged.
Demo transitions load new maps and may explain perceived pauses, but that
remains an inference until user observation identifies loading versus frozen
gameplay. Smooth playback and acoustic music were open at that checkpoint;
the Codec Kit follow-up above qualified both on a staged release. Evidence: Beast
`/var/tmp/astra-quake-board-evidence-20261026/` and
`/var/tmp/astra-quake-perf-20261026/` and durable
`/mnt/Documents/astra68/quake-audio-20261026/`. See
`docs/CHOCOLATE_QUAKE_PORT.md`.

NDK CMake standard checkpoint, 2026-10-26: extracted NDK archive now ships
`cmake/m68k-astra-toolchain.cmake`, `Platform/Astra.cmake`, and versioned
`find_package` configs for Astra, SDL2, SDL2_net, SDL2_image, SDL2_mixer,
SDL2_ttf, and `sdl2-image`. `Astra::Native`, `astra_posix_program`, and
Kit targets share existing exact-SONAME dynamic link policy. Native targets
compile freestanding; POSIX C/C++ targets remain hosted. CMake plus Ninja is
standard for new C/C++ application ports; existing Make builds remain valid.
Beast `make -C ndk dist-check` passed archive inventory, documentation,
native/POSIX/SDL/C++23 builds, and dynamic-executable contracts. A guarded
DE25 trial launched all four CMake-built binaries with status zero and saw
their native log and POSIX/C++23/SDL output. No trial panic report appeared; copied
production storage and selected release stayed unchanged; all three production
services returned active. Trial image was removed from board. Final evidence
is on Beast under `/var/tmp/astra-ndk-cmake-board-evidence-20261026/final/`.
The Beast QEMU run using an older board release image failed before NDK
commands at `posixd` startup and is not runtime evidence. Quake's build and
no-asset launch are recorded above; gameplay and DevilutionX remain port gates.

Gamepad checkpoint, 2026-10-26: compatible InputService gamepad additions
retain protocol 3, so installed Desktop/Display clients need no update.
`system.library` remains ABI 4 and SDL2 remains ABI 3; new releases can live
inside an application bundle. Beast targeted builds passed. A guarded DE25
trial copied production storage and changed only InputService plus a private
SDL probe bundle, with candidate QEMU/ROM as host overrides. Physical Logitech
F310 attached and reached SDL with built-in controller mapping; probe reported
`SDL_VIDEO_READY`. Physical buttons, D-pad, and both sticks delivered events.
Cable unplug/replug produced host detach/attach, guest SDL remove/add/reopen,
then new button and axis events. A probe loop bug that ended polling after
removal was fixed by holding its duration constant. The host now tracks sysfs
device incarnation so reattachment at the same evdev path is detected.
QEMU QMP needed an ObjectType schema entry, SDL needed its Linux-compatible
mapping database and a zero name CRC; all three source fixes are present.
Trial rollback preserved production storage hash and release selector and
restored services. Exact release build, full regression checks, and normal
release deployment remain gates.
See `docs/GAMEPAD_INPUT.md`.

Beast FPGA build incident, 2026-10-25 (recovery in progress): an uncapped
Quartus audio-fix route exhausted Beast's 61 GiB RAM and 8 GiB swap during
Analysis & Synthesis. Quartus inherited SSH service `oom_score_adj=-900`;
global OOM killed Longhorn and other Kubernetes services instead. Longhorn
iSCSI volumes then reported I/O and ext4 journal errors. Targeted SIGTERM
ended the Quartus process family. Beast SSH recovered; Plane and basic-memory
pods are running, but the 500 GiB `immich-data` Longhorn volume remains
degraded. Beast's SSH priority drop-in now sets `OOMScoreAdjust=0`,
`MemoryMin=64M` and `OOMPolicy=continue`; the parent `system.slice` also
allocates `MemoryMin=64M`. Socket activation and `Restart=on-failure` remain.
Fresh SSH confirmed score 0, active socket/daemon and both effective cgroup
reservations. No new
FPGA build may start until storage is healthy and pressure has settled. Both
DE25 Quartus entry points now require a Beast systemd scope with 10 GiB memory
high, 12 GiB hard limit, zero swap, eight CPU equivalents, 256 tasks and
`oom_score_adj=+1000`; startup refuses unenforced limits. Beast smoke checks
reached platform validation inside each scoped entry point. This prevents an
oversize build from consuming the host's memory, but 12 GiB may be insufficient
for a full route. Failed build/log are retained on Beast; no new RBF exists.
See `fpga/de25/TIMING_CLOSURE.md` for source identity and disposition.

DE25 HDMI audio acoustic regression, 2026-10-25 (open): user reported all
audio silent through unchanged HDMI monitor speakers, including Doom and
startup sound, on production release
`8b9c771f5bb9ee071306270565f40c007b5909bbd1fd9b19bf58b230f745d04e`.
While Doom ran, the audio daemon's monitor produced nonzero stereo PCM
(20-second capture: mean -24.6 dB, peak -4.7 dB); short live counter windows
showed no FIFO underruns or software gaps. A full orderly DE25 reboot restored
audible startup sound and Doom music/effects. One subsequent warm restart of
`astra.service` and its audio/remote units reached stage 8, and a direct HDMI
tone was audible. That Python tone generator produced 119 software gaps and
cannot certify continuous playback. This matches the older boot-dependent
HDMI sink behavior recorded in `docs/HANDOVER_2026-10-03_BOARD.md`. The exact
ADV7513/link failure was not captured; acoustic audio remains an open release
gate despite software counters passing. Production release, FPGA RBF and user
data were unchanged. Temporary audio captures were removed from DE25.
The candidate ADV7513 configuration adds mandatory post-power-up writes
`E0=D0` and `F9=00`; its Python fixture passes, but the FPGA route and DE25
acoustic gate have not run. Exact cause of the boot-dependent silence remains
unproven.

Bundle provider and removable-launch ownership checkpoint, 2026-10-25
(production DE25 passed): runtime and bundle tooling share private-provider
selection. Kit deletion counts only Kit alternatives; another application's
private provider cannot satisfy a dependent bundle. Supervisor owns one
explicit launch-volume lease and passes the bundle source through the loader
call chain. Focused provider and lease tests, including failure rollback and
ASan/UBSan, plus the full Beast userspace build/tests passed. Clean release
`8b9c771f5bb9ee071306270565f40c007b5909bbd1fd9b19bf58b230f745d04e`
passed package verification and guarded DE25 deployment. On production DE25,
Desktop showed `/usb2`, Atlas enumerated `/usb2/ChocolateDoom.app`, and
double-click launched the USB bundle with status zero. FPGA-backed RFB capture
`/private/tmp/astra-final-doom-key.png` showed Doom gameplay after a fresh key
event; the initial no-input capture had retained an earlier frame. `astra-top`
measured 38.6 presents/s with zero audio underruns or software gaps. All three
production services were active with zero restarts and no recent error journal;
retained panic report remained dated 2026-10-24. Board root had 8.4 GiB free.
The QEMU desktop app gate could not observe HostFS syslog or icon positions:
its export reported zero syslog lines on both this candidate and the prior
known-good release, so that gate remains an environment/harness limitation.

USB application launch qualification, 2026-10-25 (production DE25 passed):
native `cp -R /apps/ChocolateDoom.app /usb0/ChocolateDoom.app` copied the
bundle to physical FAT32 `CLIFAT`, including `manifest`, `bin`, and
`resources`. The first guarded release could not launch it: supervisor's
static assigns could not resolve `/usb0` (`application launch:2: failed`).
The follow-up source adds privileged HostFS `VOLUME_DELEGATE`. Supervisor
finds the exact public mount incarnation from the coherent catalog, binds it
while resolving/loading the bundle, derives scoped APP/CWD grants, and retains
the parent grant session until the app exits. HostFS retires descendants when
that session closes. Public VOLUME_READ/WRITE brokers remain nondelegable;
the default app ceiling does not grant VOLUME_DELEGATE. Beast full userspace
build/tests passed before the parent-lifetime correction; supervisor image/tests
and volume-port test passed after. Exact clean release
`362ed77b38730ae0a423ad738ea22f5d97a9139a9c180a70561065afcc7fcb9d`
passed publication verification and separate-image DE25 trial, then normal
preserved-state deployment reported `ASTRA_DE25_RELEASE PASS`. It is selected
on production DE25. On the live release, Desktop showed CLIFAT as `/usb2`
(its persistent slot), double-click opened Atlas, and Atlas enumerated
`/usb2/ChocolateDoom.app`. Double-clicking that bundle launched with status
zero; Chocolate Doom loaded `doom1.wad`, posted frames, locked the pointer,
and displayed live gameplay in the FPGA-backed remote-desktop capture
`/private/tmp/astra-usb-doom-game.png`. `astra-top` listed
`/usb2/ChocolateDoom.app` with three threads and about 35 presents/s;
audio reported zero underruns and software gaps. All three production services
are active with zero restarts; no service errors appeared in the final hour,
and the retained panic report is still dated 2026-10-24. Board root has 8.5
GiB free. The physical USB copy remains. Separate-image trial evidence is on
Beast under `/var/tmp/astra-usb-app-evidence-20261025/`; release remains staged
at `/var/tmp/astra-usb-launch-release-20261025/`. Do not hash the live
production image.

Private application-library lookup work, 2026-10-25 (source checkpoint,
not released): application `provides` declarations select canonical payloads
under `APP:libs/NAME/abi-ABI/VERSION/m68k-68040/NAME` before shared `LIBS:`
providers. Declared payloads
must match their embedded name, ABI, and version; a missing or mismatched
private payload fails rather than falling through. The loader resolves exact
shared provider references before cache attachment; name-only resident
attachment was removed because it could return an unrelated private copy.
Private bytes pass through kernel content comparison before cache reuse.
`astra-bundle check` validates private and Kit payload identities, and bundle
dependency coverage scopes app providers to their own app. SDL Kit was already
in the standard Kit inventory and default image. Haiku loader references and
the adapted contract are in `docs/APPLICATION_AND_KIT_MODEL.md`.
Beast `tools test`, loader MC68040 contract, userspace boundary check, and
full userspace `all test` passed during this work; focused bundle-path,
package-identity, and scoped-dependency tests passed after consolidation.
Disposable beast QEMU image `/var/tmp/astra-private-atlas.img` carried an Atlas
private `graphics.library.3`; manual Applications-icon double-click launched
Atlas with status zero. Its clone
`/var/tmp/astra-private-atlas-missing.img` had that declared private payload
removed; the same click failed with status 34 instead of substituting the
installed shared library. Both gates used exact-reference loader code before
the final path-builder consolidation. The private payload path was shortened
from `libs/libraries/` to `libs/`; focused host tests and MC68040 loader link
passed with that change. The first QEMU retry used stale ROM/QEMU products and
stopped before Desktop at supervisor status `0x53568000` during `/dh0` open.
Fresh publication resolved that fixture mismatch. Source HEAD
`6cd588e389f05fa55c511ae9c5d06a98a65e5615` plus source manifest SHA-256
`1a94e06c0d21a7783e672ed07db64e964d9ba33a765d21bc649018a262362cf6`
produced staged release
`0d02b81e143dcd2582a415cd1c335a048b75f0b4903ca6419b4424984616b0a5`
and ROM `9f1b806b843df5dc45e2052eb1b8e380db0ef15f91d8fe499897d23756db09c7`.
Beast full userspace tests and the five-app Desktop QEMU gate passed. With
the exact ROM and rebuilt host QEMU, disposable short-path Atlas launched
with status zero; removing its declared private payload yielded status 34,
without shared-library substitution.

DE25 installed that release without selecting it, then ran its ROM/QEMU
against a separate 256 MiB trial image. Desktop reached stage 8; Atlas
launched from `/apps` with status zero and enumerated `/apps` (16 entries)
and `/dh0` (14 entries), both status zero. No candidate panic appeared.
Guard verified the stopped production image and selector unchanged, then
restored all three production services. A later observer SHA-256 read of the
live production image broke the existing storage provider's exclusive lease,
causing one `ENODEV` provider exit and automatic service restart. This was a
test-observer error, not a candidate or kernel panic; production reached stage
8 again, all services are active, and retained panic report is still dated
2026-10-24. Do not read the live image; hash only while service is stopped.
Production selector remains release `1488fb36d28a081714c2a5366dea4c27e10d5b79959163c2de52b2c227a7c27e`.
The unselected candidate and all trial files were removed from DE25 after
archiving exact evidence on Beast under
`/var/tmp/astra-private-lib-board-evidence-20261025/`; free board space
returned to 1.7 GiB. The verified release remains staged on Beast at
`/var/tmp/astra-private-lib-release-20261025/`. This is a private physical
qualification, not a production activation.

DE25 disk cleanup, 2026-10-25: 21 older, unmounted private trial trees
totaling 7,549,935,844 bytes were copied to Beast under
`/var/tmp/astra-board-cleanup-archive-20261025/`. Both rsync checksum dry runs
reported zero differences across 17,122 source entries. Only those trial
copies were then removed from the board; current/previous release selectors
and production state were untouched. Board free space rose from 1.7 to
8.6 GiB (69% used), and all three production services remained active. Trial
cleanup and the live-image lease rule are now in `AGENTS.md`.

Interface Kit 7.4 UI reuse release, 2026-10-25: Desktop and Atlas now use
one native icon tile painter/hit contract and one Field/TextModel inline editor
owned by Interface Kit. Their application-specific folder commands and layout
remain in their owners. Haiku Tracker comparison and control boundaries are
recorded in `docs/DESKTOP_FOLDERS.md`. Beast full userspace build/test,
Interface Kit sanitizer/analyzer, NDK header/documentation checks, native QEMU
Desktop folder create/rename/nested open/delete/reboot gate, and Atlas DH0/RAM0,
overflow, 257-entry, UTF-8 and process-retirement gate passed. Evidence:
Beast `/var/tmp/astra-ui-reuse-userspace.log`,
`/var/tmp/astra-ui-reuse-sanitize.log`, `/var/tmp/astra-ui-reuse-ndk.log`,
`/var/tmp/astra-ui-reuse-folders.log`, and `/var/tmp/astra-ui-reuse-atlas.log`.
DE25 release `1488fb36d28a081714c2a5366dea4c27e10d5b79959163c2de52b2c227a7c27e`
passed package verification and normal preserved-state deployment, retaining
qualified kernel identity
`cbb18f706daf0daeb4c74260a321a2cb9d63c3d758eb655b21bba77c7f4bbdb6`.
Physical RFB gate showed selected `Folder1` editor, committed `UIReuseGate`,
renamed it `UIReuseFinal`, and opened its empty root in Atlas. Delete moved
it to `HOME:.Trash`; native Terminal listed `UIReuseFinal-1`. That exact empty
test directory was removed through the provider's mounted volume, and the
Desktop returned to its original icon set. Installed release verified again.
`astra.service`, audio host, and remote desktop all remain active with zero
restarts; retained `panic-latest.log` is dated 2026-10-24, before this release.
Evidence: Beast `/var/tmp/astra-ui-reuse-publish.log` and
`/var/tmp/astra-ui-reuse-deploy.log`; local
`/private/tmp/astra-ui-reuse-new.png`,
`/private/tmp/astra-ui-reuse-after-rename.png`,
`/private/tmp/astra-ui-reuse-open.png`, and
`/private/tmp/astra-ui-reuse-final.png`.

Current DE25 Desktop folder release, 2026-10-24: release
`44c0675b54a2506a3a6f236938bbf9a6d5cf602195a9f55a9fb2adc7bf2cdd33`
passed package verification and normal deployment (`ASTRA_DE25_RELEASE PASS`)
with unchanged qualified kernel identity
`cbb18f706daf0daeb4c74260a321a2cb9d63c3d758eb655b21bba77c7f4bbdb6`.
Desktop user folders persist under `HOME:Desktop` on DH0; mounted volumes
remain virtual icons. Installed apps stay in `APPS:` and are reached through
the Applications drawer. Shared Interface Kit menus now support hover child
menus, used by Desktop's `New > Folder`; shared inline name editing serves
Desktop and Atlas. Folder create, rename, nested browsing, and move-to-Trash
delete passed the native QEMU reboot-persistence gate. Atlas, context-menu,
and app-drawer QEMU gates passed; Atlas overflow also passed with 160 Desktop
folders, scrolling, exact DH0 launch, 257 entries, and process retirement.
Beast userspace build and tests passed.
Physical DE25 remote-desktop capture confirmed the `New > Folder` hover
submenu; production services remain active with `Result=success NRestarts=0`.
Physical folder qualification on 2026-10-25 passed: Desktop created `Folder1`
with inline editor, Enter committed `DE25VerifyStart`, Rename committed
`DE25VerifyFinal`, Atlas opened that folder, created and opened nested
`InsideDE25`, and both names survived a clean Astra service restart. Atlas
and Desktop context menus deleted the child and parent to Trash; both icons
disappeared. Native Terminal listed exactly those two Trash entries. Their
empty directories were removed through the provider's existing mount; native
Terminal then showed empty Trash and Desktop matched its pretest frame. The
release manifest verified again, `astra.service` is active with
`Result=success NRestarts=0`, and `panic-latest.log` remains unchanged.
During the restart, the temporary RFB probe's Python import created one
`__pycache__` file inside the release, so startup integrity checks refused
several automatic starts. The service was stopped, that generated file alone
was removed, and the temporary probe was changed to suppress bytecode writes.
Release verification and restart then passed; no production user file was
removed. Physical captures: `/private/tmp/astra-de25-folder-new-crop.png`,
`/private/tmp/astra-de25-folder-persisted-child.png`, and
`/private/tmp/astra-de25-folder-final-desktop.png`.
Evidence: Beast `/var/tmp/astra-desktop-final-userspace.log`,
`/var/tmp/astra-desktop-folders-publish.log`, and
`/var/tmp/astra-desktop-folders-deploy.log`; local capture
`/private/tmp/astra-desktop-menu-crop.png`. Desktop folder contract and Haiku
comparison: `docs/DESKTOP_FOLDERS.md`.

Current DE25 filesystem release, 2026-10-24: release
`cc88bb3df774196a55886324514453a1be19200558e3ab955d7ff35c02b3bf7d`
passed package verification and normal deployment (`ASTRA_DE25_RELEASE PASS`)
with the unchanged qualified kernel identity
`cbb18f706daf0daeb4c74260a321a2cb9d63c3d758eb655b21bba77c7f4bbdb6`.
The native `disk` CLI was exercised on the physical DE25 with the explicitly
disposable 57.7 GiB SanDisk Cruzer, serial `FC006A81F5493213`. Its prior UDF
partition was erased under user authorization. The initial physical campaign
on release `16e82ed0` ran `disk init` (MBR and GPT),
`disk create` (FAT32 and exFAT), `disk format` (both), and `disk delete`
completed. Exact stale references and mounted-disk edits were refused; clean
unmounts reported zero retained resources. Both filesystems mounted writable,
passed native create/read/remove, and retained data across unmount/remount.
The unchanged Pioneer optical drive remained `/cd0` read-only; native `ls`
listed its ISO9660 root. Current disposable USB layout is GPT partition 1,
512 MiB exFAT `CLIEXFAT` at `/usb0`, and partition 2, 512 MiB FAT32 `CLIFAT`
at `/usb2`, both mounted writable. Linux `lsblk` agrees. All three production
services are active with `Result=success NRestarts=0`; retained panic log
timestamp predates this deployment.

The first physical create at a reused USB extent exposed old FAT32 contents
as a new writable exFAT partition before formatting. Provider partition-create
now clears detected filesystem and nested partition signatures using its held
exclusive disk descriptor before rediscovery. Host and AArch64 loop tests for
MBR/GPT reused FAT16 signatures pass; provider unit and sanitizer gates pass.
On current release `cc88bb3d`, delete/recreate of the physical FAT32 partition
reported `NDOS`, remained unmounted, and Linux `blkid -p` reported no TYPE;
native FAT32 format then mounted it writable. The final mounted-disk edit
refusal left partition layout intact. Evidence: Beast
`/var/tmp/astra-disk-cli-physical-publish.log`,
`/var/tmp/astra-disk-cli-physical-deploy.log`,
`/var/tmp/astra-disk-cli-final-list.log`; board AArch64
`/var/tmp/test_de25_partition_worker.sh` gate and native Terminal/QMP
physical command output.

Previous DE25 filesystem release, 2026-10-24: release
`16e82ed063b34a0b6c73b2383d1645b6b736bbcc1257e4e3d7e7754756d5a403`
passed package verification and normal deployment (`ASTRA_DE25_RELEASE PASS`)
with the unchanged qualified kernel identity
`cbb18f706daf0daeb4c74260a321a2cb9d63c3d758eb655b21bba77c7f4bbdb6`.
`dos.library.1` is now packaged in Filesystem Kit and its public NDK. It
combines one coherent Volume catalog and usage snapshot, applies the same
disk-edit selection policy for Disk Setup and the native `disk` command, and
dispatches existing exact-reference format/partition operations from
`filesystem.library.15`. The latter remains the sole owner of volume
operations; the shared-symbol ownership and export gates pass with no
duplicate definitions. `disk` supports list, info, MBR/GPT initialization,
FAT32/exFAT partition creation and formatting, and partition deletion. Edits
require an exact epoch:ID:generation and typed `ERASE`. Terminal's trusted
launch ceiling and Desktop delegation now admit `DISK_ADMIN` for this command.
Beast gates passed: DOS host policy test, ASan/UBSan and analyzer, Filesystem
Kit manifest, NDK check and distribution, full userspace build/test, image
policy, shared-library ownership, native QEMU `disk list` and stale-edit
refusal. A native DE25 Terminal `disk list` shows FAT32 `ASTRA_FAT` mounted
read/write at `/usb0`, exFAT `ASTRA_EXFAT` read/write at `/usb2`, UDF
`ASTRA_UDF` read-only at `/usb1`, and ISO9660 `ASTRA_CD` read-only at `/cd0`.
Native `disk info 14` on the prior candidate reported one mount and zero
retained resources. No destructive CLI operation was run on physical media;
Disk Setup and the command share the tested DOS dispatch path. All three
services are active with `Result=success NRestarts=0`. The retained
`panic-latest.log` is dated before this deployment; no new panic was observed.
Evidence: Beast `/var/tmp/astra-dos-gate.log`,
`/var/tmp/astra-dos-ndk-gate.log`, `/var/tmp/astra-dos-userspace-gate.log`,
`/var/tmp/astra-dos-image-policy-test.log`,
`/var/tmp/astra-dos-qemu-reject.log`, `/var/tmp/astra-dos-qemu-final.log`,
`/var/tmp/astra-dos-publish2.log`, `/var/tmp/astra-dos-deploy2.log`, and
`/var/tmp/astra-dos-de25-list.log`. Haiku comparison and adapted boundaries
are recorded in `docs/DEVICE_AND_VOLUME_PLAN.md`.

Previous DE25 filesystem release, 2026-10-24: coherent release
`c041b26486b4d609cfecc526505595930710be0eb37e7b31ee565aa721fc2c65`
passed package verification and normal deployment (`ASTRA_DE25_RELEASE PASS`),
with kernel identity `cbb18f706daf0daeb4c74260a321a2cb9d63c3d758eb655b21bba77c7f4bbdb6`.
All three services are active; `astra.service Result=success NRestarts=0`.
The provider now persists USB `/usbN` reservations in root-owned, checksummed
state by USB serial plus partition number, falling back to partition UUID.
Before and after an explicit clean `systemctl stop`/`start`, native
`volumes --all` showed FAT32 `ASTRA_FAT` read/write at `/usb0`, UDF
`ASTRA_UDF` read-only at `/usb1`, exFAT `ASTRA_EXFAT` read/write at `/usb2`,
and ISO9660 `ASTRA_CD` read-only at `/cd0`. Stop returned `Result=success`,
`ExecMainStatus=0` and no QEMU process; restart kept all four paths. Native
FAT32 and exFAT write/read/remove passed after restart; UDF
`Folder/README.TXT` read succeeded; ISO9660 `README.TXT` matched source
SHA-256 `566efe82ff7efb0f1fcb4f892b4dced089c4b710c17535d494882928bbb0ae80`.
Desktop showed every volume icon and Atlas opened `/cd0` with three entries,
one folder and status zero. Beast gates passed: storage name persistence,
catalog, discovery, provider, launcher, runtime launcher and service contract.
Native `unmount --eject /cd0` then ejected this ISO9660 DVD-R: Terminal
reported `ejected`, Linux removed its media identity, native inventory dropped
`/cd0` while preserving `/usb0` through `/usb2`, and the Desktop icon vanished
in a captured remote-desktop frame. Astra stayed active with zero restarts.
This drive cannot close its tray by command. After manual reinsertion, Linux
reported `ID_FS_TYPE=iso9660` and `ID_FS_LABEL=ASTRA_CD`; Astra discovered new
optical device/medium/source/mount IDs 22–25 and automatically remounted `/cd0`
read-only. Native `README.TXT` readback matched the source SHA-256 above;
Desktop restored `ASTRA_CD`, and Atlas opened its root with three entries,
one folder and status zero. The service remained active with zero restarts.
Native safe removal also refused `unmount /usb1` while Terminal held
`/usb1/Folder`, naming PID 17's directory handle and the exact path. After
Terminal left that directory, unmount removed `/usb1` and its Desktop icon
while preserving `/usb0`, `/usb2` and `/cd0`. Native `mount --read-only 16`
restored the same UDF partition at `/usb1` (new mount ID 26); Desktop restored
its icon, and `Folder/README.TXT` read back
`Astra USB UDF gate 2026-10-24`. The service remained active with
`Result=success NRestarts=0`.
USB reattachment without service restart also passed through a targeted Linux
`usb-storage` interface unbind/rebind for disposable SanDisk serial
`FC006A81F5493213`. Native `lsof` showed no holders before all three
partitions were safely unmounted. Unbind removed only that USB device's
catalog graph and Desktop icons; `/cd0` remained mounted. Rebind published
fresh device/medium/source IDs 27–31 and mounts 32–34, preserving FAT32
`/usb0` read/write, UDF `/usb1` read-only, and exFAT `/usb2` read/write.
Native FAT32 and exFAT create/read/remove, UDF read, and unaffected `/cd0`
read all passed. Desktop syslog shows the three USB icons return at
18:41:53 UTC; Astra remained active with `Result=success NRestarts=0`.
This checks Linux block-interface removal and reappearance; a literal cable
unplug/replug then passed separately: after native safe unmount of all three
partitions, the user unplugged and replugged the same SanDisk on the hub.
Fresh device/medium/source IDs 43–47 and mount IDs 48–50 restored UDF
`/usb1` read-only, exFAT `/usb2` read/write and FAT32 `/usb0` read/write.
Native FAT32/exFAT create/read/remove and UDF read passed; Desktop restored
all three icons at 18:47:31 UTC, while `/cd0` remained mounted. Astra stayed
active with `Result=success NRestarts=0`.
An additional surprise-removal probe found that a Terminal holding
`/usb1/Folder` could not fork after interface withdrawal. The old release
reported `unknown error`. Two private DE25 trials exposed the native failure:
CWD sharing returned VFS I/O while the host channel closed, then VFS invalid
after the old grant disappeared. POSIX mapped those statuses accurately, but
neither told the user which volume was lost. The native CWD share now checks
the Volume catalog's exact mount ID and generation only on a failed share;
when that incarnation has retired, it returns `LOST`. POSIX maps `LOST` to
`ESTALE`, and shared picolibc names that error. The final private trial of
release `c041b264` produced twice: `zsh: fork failed: stale file or directory
(volume removed or replaced)`. It had no panic; after USB rebind, `cd /work`
released the old CWD and external commands worked. The trial restored the
production image hash, selector and three active services. Normal deployment
then reported `ASTRA_DE25_RELEASE PASS`. Live release verification passed;
FAT32 `/usb0` and exFAT `/usb2` write/read/remove, UDF `/usb1` read, and
ISO9660 `/cd0` README read matched its source SHA-256 above. All four Desktop
icons were visible; services were active, `astra.service Result=success
NRestarts=0`, and the panic log was empty. Haiku's
`src/system/kernel/fs/vfs.cpp` disconnects removed mount vnodes from CWD and
clones I/O contexts with retained vnode references. Astra retains its exact
lost CWD identity until the application changes directory, rather than
silently rebinding it to replacement media.
Evidence: Beast `/var/tmp/astra-stable-usb-publish-20261024.log` and
`/var/tmp/astra-stable-usb-deploy-20261024.log`; board WORK
`stable-before-volumes.txt`, `stable-after-volumes.txt`,
`stable-cd-read.txt`, `stable-udf-read.txt`, `stablefat.txt`,
`stableexfat.txt`, `cd-eject-after.txt`, `cd-reinsert-volumes.txt`,
`cd-reinsert-read.txt`, `udf-holder-cwd.txt`, `udf-holder-lsof.txt`,
`udf-busy-volumes.txt`, `udf-unmounted-volumes.txt`,
`udf-remounted-volumes.txt`, `udf-remount-read.txt`,
`pre-replug-lsof0.txt` through `pre-replug-lsof2.txt`,
`pre-replug-unmounted.txt`, `replug-detached-volumes.txt`,
`replug-returned-volumes.txt`, `replugfat24.txt`,
`replugexfat24.txt`, `replug-udf-read.txt` and `replug-cd-read.txt`;
`loss-pre-volumes.txt`, `loss-holder-lsof.txt`,
`loss-recovered-volumes.txt`, `physical-replug-safe.txt`,
`physical-replug-returned.txt`, `cablefat24.txt`, `cableexfat24.txt` and
`cable-udf-read.txt`; Beast candidate build logs
`/var/tmp/astra-picolibc-estale-build.log` and
`/var/tmp/astra-estale-userspace.log`; diagnostic evidence
`/var/tmp/astra-stale-cwd-evidence-20261024/diagnostic-syslog` and
`forkdiag.err`; final Beast logs `/var/tmp/astra-stalecwd-vfs-tests.log`,
`/var/tmp/astra-stalecwd-userspace-build.log`,
`/var/tmp/astra-stalecwd-publish.log`,
`/var/tmp/astra-stalecwd-volume-ui-root.log`,
`/var/tmp/astra-stalecwd-deploy.log`, and
`/var/tmp/astra-stale-cwd-evidence-20261024/final-stale.err`;
board WORK `stalecwd-release-volumes.txt`, `stalecwdfat.txt`,
`stalecwdexfat.txt`, `stalecwd-udf-read.txt`, `stalecwd-cd-read.txt`,
`stalecwd-cd-list.txt`; local post-release frame
`/private/tmp/astra-post-frame.png`;
local remote-desktop captures are `/private/tmp/astra-cd-ejected.png` and
`/private/tmp/astra-udf-busy.png` and
`/private/tmp/astra-loss-stale-cwd.png`. Desktop syslog at 18:38:28 and 18:38:39 UTC
records `/usb1` icon withdrawal and restoration. Haiku's
`src/servers/mount/AutoMounter.cpp` restores prior
partition mount settings by saved device/partition identity; Astra adapts that
pattern to native path reservations and exact mount generations. Physical
CD-R remains unqualified because only DVD-R media is available. Board host
OOM during package installation needs separate resource qualification. The
recurring `ramfs:2: failed` boot log came from a persisted, user-tier service
definition pointing to removed `/services/ramfs`; the current image no longer
ships that service and `/ram0` uses the native tmpfs provider. Its definition
was saved in WORK `inspect-ramfs-service.txt`, then disabled through native
`service disable ramfs`, preserving the file for recovery. After clean
stop/start, service inspection showed `boot: no`, the latest boot had no
`ramfs:2: failed`, `/ram0` appeared on Desktop, and `astra.service` stayed
active with `Result=success NRestarts=0`. Native Terminal also created, read,
and removed `/ram0/ramgate.txt` successfully; WORK `ram-gate-read.txt` holds
the readback. Performance qualification remains
deferred.

Latest physical optical checkpoint, 2026-10-24: a Pioneer BDR-XD08
(`08e4:017a`, serial `1854081922802053`) with the SpongeBob `DVD_VIDEO`
UDF DVD auto-mounted read-only at `/cd0` on the production DE25. The Desktop
showed its volume icon; Atlas opened `VIDEO_TS`; native root enumeration and
`VIDEO_TS.IFO` read succeeded, with the 16,384-byte file matching the Linux
provider mount by SHA-256. Native `unmount --eject /cd0` refused a held file
and `lsof /cd0` named the Terminal process and exact IFO path. The first
Desktop Eject safely unmounted but the drive refused tray release with SCSI
`53/02`, and Astra showed "Eject failed; the disc remains in the drive".
Linux `eject` succeeded with `CDROM_LOCKDOOR(0)` before `CDROMEJECT`; the
shared provider worker now uses that sequence, allowing lockless drives to
skip unsupported unlock. A coherent private DE25 candidate
`3e2683c3c31538d55e722b4e14ee25d261d0fae1809b9cbcc21e00c274604d59`
then returned native eject status zero and the disc left `/dev/sr0`. Its
five-minute trial had no panic; guard restored the original production image
SHA-256 `652855117dcac14bf2aa290ad810a0162c90f6d8a7fdd4ea104a4f7ddd40c0c8`,
selector and three active services. The final lockless-drive refinement was
released as `8ccd548b5a0c9ec619c5f8dc98622ea13b7b015c348a2952aae8d91f82f97993`:
the normal deployment reported `ASTRA_DE25_RELEASE PASS`, POST passed, and
all three services were active with `astra.service NRestarts=0`. After the
user reinserted the DVD, the final release auto-mounted `/cd0`, showed its
Desktop icon, and Atlas enumerated one root folder. Desktop context Eject
then withdrew the icon, removed the disc and logged `desktop volume ejected`
without a service restart. ISO9660 on recordable DVD is now qualified below;
CD-R media remains unqualified.
Haiku reference: `src/add-ons/kernel/drivers/disk/scsi/scsi_cd/scsi_cd.cpp`
uses a SCSI start/stop eject operation; Astra retains the Linux optical ioctl
path behind its native volume lifecycle. Beast evidence:
`/var/tmp/astra-optical-eject-build-20261024.log` and
`/var/tmp/astra-optical-eject-provider-final-20261024.log`, plus
`/var/tmp/astra-optical-eject-final-build-20261024.log` and
`/var/tmp/astra-optical-eject-final-deploy-20261024.log`. Board evidence:
`/var/tmp/astra-optical-eject-trial-20261024/` and its WORK export, plus the
production WORK `.astra/syslog`.

Final-release USB regression, 2026-10-24: on the selected
`8ccd548b5a0c9ec619c5f8dc98622ea13b7b015c348a2952aae8d91f82f97993`
DE25 release, native `volumes --all` listed the user-authorized disposable
SanDisk's three GPT partitions simultaneously: FAT32 `ASTRA_FAT` at `/usb0`
read/write, exFAT `ASTRA_EXFAT` at `/usb1` read/write, and UDF `ASTRA_UDF` at
`/usb2` read-only, with exact partition numbers and byte ranges. Native
Terminal created, read back and removed a distinct file on each writable
volume; all commands returned status zero and later lookups returned status
one. It read the UDF fixture and refused a write with status one; the attempted
file was absent. `astra.service NRestarts=0`. Board evidence:
`/var/lib/astra/hostfs/work/final-usb-*.txt` and matching status files.

Physical MBR gate, 2026-10-24: on that same selected release and the
user-authorized disposable SanDisk Cruzer (`FC006A81F5493213`), native Terminal
unmounted all three GPT partitions. Disk Setup initialized an MBR map, created a
256 MiB FAT32 partition, formatted it as `ASTRAMBR`, and deleted it after a
native `/usb0` create/read/remove test. The shorter partition initially
retained the old 4 GiB FAT boot sector; the provider correctly reported its
source `FAILED` and did not mount it until native formatting completed. Disk
Setup then initialized GPT. With Astra stopped, the exact prior three-entry
GPT table was restored; FAT32 was reformatted at its original 4 GiB extent,
while exFAT and UDF content remained intact. The UDF partition's full 64 MiB
SHA-256 matched its pretest backup
(`8b0a7e0b14fc260713244855fca73dc5e02fc96e5f750c816b7bd7871c7f5414`).
After restart, all three services were active, `astra.service NRestarts=0`,
and native FAT32/exFAT write-read-remove and UDF read passed. The active-slot
`/usbN` assignments changed across restart: UDF `/usb0`, exFAT `/usb1`,
FAT32 `/usb2`. The user now requires stable `/usbN` assignments for the same
physical drive across reboot; current dynamic policy must be replaced. Board evidence:
`/var/tmp/astra-mbr-physical-20261024/` and
`/var/lib/astra/hostfs/work/mbr-*.txt`. Stopping `astra.service` returned
`exit-code` after its provider sent QEMU SIGTERM and the launcher saw a broken
pipe; `reset-failed` plus a fresh start succeeded. The source now distinguishes
an explicit systemd SIGUSR1 stop from unexpected TERM, and reports a clean
provider stop only after QEMU exits and storage cleanup succeeds. Beast's
storage-launcher, runtime-supervisor and service-contract tests pass. The first
DE25 trial still marked stop failed because QEMU handles SIGTERM and exits 0;
the provider accepted only direct SIGTERM death. That condition and its
QEMU-style launcher test are corrected in source; the clean DE25 stop/restart
gate passed in the current release above. Haiku reference:
`src/servers/launch/LaunchDaemon.cpp` handles explicit target stop separately
from job failure, requesting graceful quit before falling back to SIGTERM.

Physical ISO9660 data-CD fixture, 2026-10-24: Beast generated a 410 KiB
ISO9660/Joliet/Rock Ridge image named `ASTRA_CD`, with root `README.TXT`,
`SECTORS.TXT` (2,048 generated lines spanning sectors), and
`FOLDER/NESTED.TXT`. `isoinfo` verified the image structure and extracted all
three files byte-for-byte against their source checksums. Local burn image:
`build/data-cd-fixture/Astra68-Data-CD.iso`, SHA-256
`0a20bd5c15a66b2eafd2c18cd284a62e2c5a1a8c41486adf2b4d2ef935fed11c`.
The user supplied blank DVD-R. Pioneer BDR-XD08 burned and finalized this image;
after reinsertion, `/dev/sr0` reported `iso9660 ASTRA_CD` and its first 205
2048-byte sectors matched the image SHA-256 exactly (three trailing pad sectors).
Production release `0828d7670691c135f6745d003332d45361f893fff564dc18d9d28225640f29c3`
auto-mounted it read-only at `/cd0`, showed `ASTRA_CD` on Desktop, and Atlas
opened its root with three entries and one folder. Native Terminal listed root
and `FOLDER`; all three native file reads matched source SHA-256, including
`SECTORS.TXT` across multiple sectors. Physical CD-R medium remains a separate
gate; native eject and automatic reinsertion remount of this DVD-R passed
above. During
`wodim`/`growisofs`
package installation, Linux OOM killed QEMU; systemd restarted Astra once.
This was host memory exhaustion, not a guest kernel panic. Resource policy for
package maintenance under the 512 MiB guest requires investigation.

Previous production filesystem checkpoint, 2026-10-24: coherent release
`2fd5a272d3f75564800f0b41a398739551ee61cc4d61e5907f1636c0db617b31`
was selected and running on DE25. Optical eject now refuses while **any**
partition on the same disc remains mounted; the prior source-only guard could
retire a sibling mount. Desktop exposes `Eject` for a mounted physical optical
volume through its existing common command menu. It first performs the same
safe unmount and exact busy-holder reporting as `Unmount`, then requests the
native source eject and reports failure without claiming the disc left.
Host catalog regression covers mounted optical siblings, unmount, eject
failure and success. Beast storage provider suite and Desktop MC68040 build
passed. A root-observed host-QEMU Volume UI gate passed its 33-holder modal,
context Unmount, disappearing icon and ISO image-source retirement. The earlier
user-observed Beast gates could not read root-owned desktop syslog; nested
AArch64 QEMU also failed release 11's `posixd` launch under x86 emulation, so
neither was used as candidate acceptance evidence. Private DE25 boot reached
Desktop with `/dh0`, `/usb0`, `/usb1`; a USB0 right-click opened the shared
context menu. Its guard restored the prior selector, identical stopped
production-image SHA-256, and all three services (`TRIAL_RESTORED result=0`).
The normal deployment migrated the stable image, verified running kernel and
release, and reported `ASTRA_DE25_RELEASE PASS`. Its services were active,
`astra.service NRestarts=0`, FAT32 and exFAT icons are visible, and the only
panic report predates this release. Haiku reference:
`src/kits/tracker/PoseView.cpp` and `src/kits/tracker/FSUtils.cpp` guard boot
volume removal and dispatch selected-volume actions; Astra adapts this to its
native medium/partition graph and separate Eject operation. Beast evidence:
`/var/tmp/astra-fs-eject-release12-20261024.log`,
`/var/tmp/astra-fs-eject-root-observed12-volume-ui.log`, and
`/var/tmp/astra-fs-eject-deploy12-20261024.log`. Board evidence:
`/var/tmp/astra-fs-private-trial-eject12-20261024.log` and its work directory.
At this release's qualification, no optical drive was attached. The later
physical gate above supersedes its eject status. Performance qualification
remains deferred until correctness gates finish.

Latest physical USB UDF gate, 2026-10-24: on the user-authorized disposable
SanDisk Cruzer serial `FC006A81F5493213`, a 64 MiB GPT third partition was
added after the existing 4 GiB FAT32 and exFAT partitions without changing
their extents. A `genisoimage -udf` fixture was verified as UDF on Beast and
as a read-only Linux mount on DE25 before Astra started. The selected
production release discovered all three partitions simultaneously and
auto-mounted UDF `ASTRA_UDF` at `/usb2` read-only. Native `volumes --all`
reported source 13, partition 3 and exact geometry. Guest `cat` read the
fixture bytes; a guest write returned status 1 and no file appeared in the
provider mount. Double-click opened Atlas on `/usb2` with one root folder.
After closing Atlas, native `unmount /usb2` succeeded and its Desktop icon
disappeared; `mount --read-only 13` restored the icon and exact file contents.
Provider mountinfo showed `udf` with `ro`, and all three host services are
active with `astra.service NRestarts=0`. The panic report predates this boot.
Board evidence: `/var/tmp/astra-usb-udf-gate-20261024/` (fixture, partition
tables and QMP gate script), `/var/lib/astra/hostfs/work/udf-20261024-*.txt`
(guest command outputs), and `.astra/syslog` in that WORK export (Desktop and
Atlas logs). Physical data-CD/media-change/eject gates remain open.

Optical replacement software checkpoint, 2026-10-24: the real Linux provider
loop-device fixture now simulates removal and reinsertion of the same ISO9660
medium while a root directory handle retains the old Linux mount. The new
medium gets a distinct source but cannot mount over the retained old bytes.
After the handle closes and the old mount reaps, automount admits the new
read-only source; root access and safe unmount pass. The same image/UUID is
intentional, exercising generation fencing. The provider's actual eject
worker then receives Linux `EINVAL` for `CDROMEJECT` on this loop device and
leaves the source present, unmounted and carrying that error. Beast provider
test passes both normally and under ASan/UBSan. Haiku reference:
`src/system/kernel/disk_device_manager/KDiskDeviceManager.cpp` uninitializes
old media before rescan, and `src/add-ons/kernel/drivers/disk/scsi/scsi_cd/scsi_cd.cpp`
invalidates its I/O scheduler on media change; Astra retains its native
medium generations and Linux mount cleanup fence. This software gate does
not qualify a physical optical drive, disc replacement, or eject.

Previous production filesystem checkpoint, 2026-10-24: coherent release
`04aa23c8dd06eeba18f8c972cb27dc4ad360de59b96e67a2140eda0de2dde5d8`
was selected and ran on DE25. The stopped legacy `/dh0` image was copied,
verified, and refreshed with release-owned commands, services, Kits, apps,
system fonts, startup manifest, sound and event catalog before atomic
publication at `/var/lib/astra/state/system/storage-terminal.img`. `/config`,
`/home`, `/local`, installed fonts and other user paths remain in that image;
the old release-keyed image remains intact for rollback. Private boot of the
migrated copy reached Desktop, read system configuration, wrote/read `/home`,
and mounted the disposable USB FAT32 and exFAT partitions read/write. The
private guard reported `TRIAL_RESTORED result=0` with no panic. Production
deploy reported `ASTRA_DE25_RELEASE PASS`; QEMU runs unprivileged, its loop
device backs the stable image, all three services stay active with zero
restarts, and native `volumes --all` plus read/write probes passed on both USB
formats. The 11:46 panic log belongs to the failed earlier cutover; no new
panic has appeared since the 12:39 production boot. Physical CD media/eject
and full performance qualification remain open.
Beast evidence logs are `/var/tmp/astra-fs-private-trial-migration11-20261024.log`
and `/var/tmp/astra-fs-migration-deploy11-20261024.log`.

The first production cutover attempt (`f7a65563...`) exposed a real release
migration defect: copying the old image without refreshing its OS files made
the new HostFS exit with status 8 and caused a boot panic. Production was
stopped and rolled back to its original image. A second inactive candidate
(`0e74524d...`) exposed Python bytecode written into a sealed release during
migration; its package was pruned before activation. The final migration uses
content checksums, rejects redirected managed directories, refreshes only
release-owned paths, and suppresses Python bytecode. Deployment now restores
prior service units and stable-state backup on failure, and waits for HostFS
startup before reporting success. Haiku reference:
`src/add-ons/kernel/file_systems/packagefs/volume/Volume.cpp` keeps packaged
system content separate from writable shine-through directories; Astra adapts
that ownership split within its existing system image.

Latest physical filesystem gate, 2026-10-24: coherent candidate
`92ec0d3406fcec38eee09d8b84dc2e01810e9b1eb286e11b4c57e055d0dd4785`
verified and booted privately on DE25. On the user-authorized disposable SanDisk
Cruzer serial `FC006A81F5493213`, native Disk Setup created a 4 GiB GPT exFAT
partition and reported success. Native Disk Setup formatted that partition as
exFAT, then FAT32; both auto-mounted writable. Native create/read/delete passed
on each, and a file survived unmount and remount in each format. Native delete
removed the partition and retired its mount. No provider error or panic appeared.
The trial guard reported `TRIAL_RESTORED result=0`: production selector
`ccf82877667a74461e8824c4dbcbe2ead56548cd19bfd18b50b80fd2ba295a30`,
production image hash and all three services were restored. The USB now has its
two earlier 4 GiB FAT32/exFAT partitions. Physical CD media remains unavailable.

Two causes were fixed before this gate. Installed formatter binaries inherited
`terasic` ownership from transfer, while the provider correctly required trusted
root ownership; root installation now seals every release path as root-owned.
The partition worker copied its script pipe onto fd 0 before duplicating
`/dev/null` from fd 0 to stdout, so `sfdisk` changed GPT then exited with a
write error. Descriptor setup now preserves `/dev/null` first and retains
provider stderr for diagnostics. Host provider/discovery, AArch64 provider,
release integrity and private physical gates passed.

The cutover risk found before this physical gate was the old launcher's
release-keyed mutable `/dh0` path. The completed production migration above
uses a release-independent state path and refreshes system files while retaining
user data. Haiku reference:
`src/system/kernel/fs/vfs_boot.cpp` mounts the persistent boot volume at
`/boot`, while `src/add-ons/kernel/file_systems/packagefs/packagefs.cpp`
manages packaged system content separately; Astra adapts this separation to
its single Linux-owned system image and immutable runtime releases.

Latest physical gate, 2026-10-24: candidate
`4954053aec412226fc07905622a55d7908aff0ceb970464ef69de1e797cbf53c`
verified and booted privately on DE25. Native removal of the remaining FAT32
partition left a GPT disk with no partitions; provider and QEMU survived.
Desktop published `NDOS`; double-click displayed "Astra cannot open this disk:
no supported filesystem was found." Native Disk Setup then recreated a 4 GiB
FAT32 partition, but still displayed "Disk edit failed" after the physical
map and catalog changed. Its native FAT32 format of the unmounted writable
source returned "Protected or write-protected disk; format refused". No panic
appeared; private trial guard restored the production selector, stopped-image
hash and all three services (`TRIAL_RESTORED result=0`). Candidate remains
inactive. Provider phase/error and worker-exit diagnostics are being added
before the next coherent release to isolate both failures. Haiku's
`src/kits/storage/disk_device/DiskDeviceJobQueue.cpp` reports a disk job's
status after `Do()` finishes; Astra retains the same terminal-status contract
across its asynchronous provider fence.

Current physical gate, 2026-10-24: candidate
`839934eddabff5c189076c8b2856fedb37ead4554f90454b729b91c0d9f64583`
verified and booted privately on DE25; production selector stayed
`ccf82877667a74461e8824c4dbcbe2ead56548cd19bfd18b50b80fd2ba295a30`.
Native Disk Setup created 4 GiB FAT32 and 4 GiB exFAT partitions on the
disposable SanDisk Cruzer. Both mounted writable; native file create/read/delete
passed on both. Unmount and native deletion of exFAT removed `/dev/sda2`,
retired its source and icon, and left provider and QEMU alive. Guard reported
`TRIAL_RESTORED result=0`, no panic, and all production services active. The
USB now has a single FAT32 partition. Creation exposed an asynchronous status
bug: Disk Setup displayed a failure while the map edit and rediscovery
completed. Provider published transient partition-retire/reread errors before
its retained operation finished. Status now stays busy until completion or
its five-second failure deadline; provider test checks this contract. The
empty-disk Desktop path now admits a physical device with no live source as
`NDOS`, opens a clear error, and offers Disk Setup. This and the status fix
need a fresh DE25 release and physical retest before production activation.
Haiku reference for Desktop behavior: `src/kits/tracker/Tracker.cpp` requests
mountable devices separately from mounted volumes, while
`src/kits/tracker/IconCache.cpp` falls back to a generic device icon. Astra
adapts that to its native device/source/mount graph and existing volume icon.

Physical delete failure, 2026-10-24: candidate
`756da8522d0fc6ef8ed4bc03928361a276fc47f732f38f9e29f6a94b31b52300`
verified on Beast and installed **inactive** on DE25. Its private boot
correctly left the stale 4 GiB exFAT signature in the 4 MiB `/dev/sda2`
partition in FAILED state, with no writable mount. Native Disk Setup kept
4096 MiB in its shared Stepper and enabled format recovery on the failed
source. Deleting that source removed `/dev/sda2` from the physical GPT map,
then the storage provider exited `EINVAL`, killing the private QEMU. The trial
guard restored production services and checked the stopped production image
and release selector; no candidate panic report appeared. USB now contains
only the 4 GiB FAT32 `/dev/sda1` and unallocated remainder. Provider phase,
partition-completion and udev-event diagnostics are being built to isolate
the fatal error before any further physical edit. Production is still
`ccf82877667a74461e8824c4dbcbe2ead56548cd19bfd18b50b80fd2ba295a30`.
The second guarded trial reproduced deletion on `/dev/sda1` and isolated the
error: Linux delivered a partition `change` event after that sysfs node had
disappeared; its geometry was zero, and discovery returned fatal `EINVAL`.
Discovery now normalizes that exact observation to removal while retaining
sequence and disk-incarnation checks; incomplete observations for still-live
sysfs nodes leave known geometry intact. A focused discovery regression and
the host provider, AArch64 provider and MC68040 Desktop build gates pass.
The USB is now a GPT disk with no partitions. Physical retest remains open.
Haiku reference: `src/kits/storage/disk_device/DiskDeviceList.cpp` handles
partition deletion separately from ordinary device changes, and
`src/system/kernel/disk_device_manager/KDiskDeviceManager.cpp` emits explicit
removal notices. Astra adapts this to Linux udev's less specific `change`
event by checking the actual sysfs node before retiring its exact observation.

Physical partition-edit checkpoint, 2026-10-24: the user authorized erasing
the complete 57.7 GiB SanDisk Cruzer serial `FC006A81F5493213`, observed as
DE25 `/dev/sda`; the internal SD remains protected. Candidate
`9c1dbfa3b844c3099f0525ae0d0e8c3a9ec6f16d4205ef8250603f8b1cc07cb8`
booted in a guarded private trial. Native Terminal unmounted all four former
USB partitions. Native Disk Setup then initialized a GPT map, created a 4 GiB
FAT32 partition and a 4 MiB exFAT partition; Linux `sfdisk` and the native
catalog confirmed post-edit rediscovery. Disk Setup lost device selection
after edits, and rebuilding its shared Stepper after each keypress reduced an
entered `4096` to `4`. Those UI paths now preserve the live device identity
and in-progress numeric input. The 4 MiB partition reused an old 4 GiB exFAT
boot sector and was incorrectly auto-mounted writable. The provider probe now
checks FAT and exFAT declared extent against the admitted source window,
leaving oversized signatures in FAILED state; Disk Setup and catalog permit
safe formatting of a FAILED physical source. Host and DE25 AArch64 probe,
catalog and provider gates pass. Physical retest and production release gates
remain open; do not write to the current malformed `/dev/sda2` until a fixed
candidate has rejected it. Trial reported `TRIAL_RESTORED result=0`; production
selector, stopped production-image hash and all three production services
were restored. Haiku references for this repair:
`src/add-ons/kernel/file_systems/exfat/exfat.h` and `Volume.cpp` describe the
boot-sector block count and sector shift; `src/add-ons/kernel/file_systems/fat/support.cpp`
reads FAT BPB sector geometry. Astra adapts those checks at its existing
bounded, read-only provider probe before auto-mount admission.

Native partition-control checkpoint, 2026-10-23: Filesystem Kit ABI 15.2,
native Volume protocol 4, provider protocol 6 and Host command 6 now carry
one typed initialize/create/delete request with exact device reference and
byte geometry. Disk Setup uses shared Interface Kit tabs, fields, stepper and
buttons; destructive operations require `ERASE`. The provider holds an
exclusive observed parent-disk descriptor, fences mounted descendants and
in-flight probes/work, runs fixed `sfdisk` commands, retires old sources,
then rereads the kernel map before incrementing the device generation. A
Beast real-loop regression covered MBR initialize, FAT32 partition create,
discovered source retirement and delete. Full provider format/mount gate,
host partition worker, AArch64 provider build, QEMU DE25 build, native VFS,
NDK, HostFS and Disk Setup build gates passed. A coherent release is being
staged; guest runtime, DE25 physical map editing, post-edit rediscovery and
production deployment are **not yet certified**. The authorized disposable
SanDisk Cruzer serial `FC006A81F5493213` remains unchanged by this new path;
internal SD and selected production release remain untouched. Haiku reference:
`src/apps/drivesetup/MainWindow.cpp` validates and commits explicit disk
modifications; `src/kits/storage/disk_device/Partition.cpp` keeps partition
identity and geometry distinct from filesystem content. Astra adapts these
patterns to exact native generations and a single provider lifecycle.

Earlier partition-worker and admission checkpoints below describe their state
at the time; the native-control checkpoint above supersedes their open items.

Partition worker checkpoint, 2026-10-23: a typed provider-only worker now
executes fixed `sfdisk` initialize/create/delete commands on a retained
exclusive parent-disk descriptor. It validates device kind, capacity, logical
sector size, partition map, filesystem type, bounds and action-specific fields;
the client cannot pass an executable path, command or script. A disposable
128 MiB Beast loop gate passed MBR and GPT initialize, create and delete via
the actual worker, with kernel partition reread only after its descriptor was
closed. Both host and DE25 AArch64 FAT32/exFAT exclusive-FD gates, full
provider/discovery gates and DE25 AArch64 provider build pass on Beast. This
worker is not yet connected to native
Disk Setup or provider admission: device-wide sibling/holder fencing, bounded
completion, kernel reread and rediscovery still need implementation. No
partition edit is enabled in a guest, and no physical disk was repartitioned
by this checkpoint.

Partition admission checkpoint, 2026-10-23: the exact parent-disk opener now
uses catalog-wide edit admission. It rejects active descendant Linux mounts,
pending mount cleanup, concurrent sibling format, stale device generations,
system disks and physical write protection before opening the observed device.
The provider resource gate also rejects queued and running probes, live workers,
retained format descriptors and pending mount resources for that device.
Focused catalog and real-loop tests cover busy, queued probe, stale, format
and read-only refusal; full provider/discovery gates and the DE25 AArch64
provider build pass on Beast. Device state fencing, source retirement,
kernel reread and native edit lifecycle remain open.
The exact AArch64 partition worker also passed MBR/GPT initialize, create,
delete and post-close kernel reread on a disposable DE25 `/dev/shm` loop,
using the board's installed `sfdisk`. Gate log is
`/var/tmp/astra-partition-de25-worker-20261023.log` on Beast. Its temporary
loop and binaries were removed; only production `/dev/loop0` remains and all
three production host services are active. The USB `/dev/sda` was not edited.
DE25 root had only 199 MiB free. The inactive 2026-10-19 IRQ trial tree was
copied to Beast at `/var/tmp/astra-board-archive-20261023/irq-origin-physical-20261019/`;
a checksum dry run verified 13,245 entries and zero changed or missing files.
After confirming no mount or live process used that tree, its board copy was
removed. Board root now has 1.8 GiB free (94% used), with unchanged USB and
internal SD partition inventories, production `/dev/loop0` alone, and all
three production host services active. `/dev/shm` served only the small
disposable block test; it cannot host a production HostFS backing image.

Partition-edit groundwork, 2026-10-23: discovery now has one exact
parent-disk `O_RDWR|O_EXCL` opener for the future native edit path. It
revalidates current catalog generation, disk geometry, Linux disk sequence,
reserved system ancestry, physical write protection and block-device
capacity. The existing real-loop provider gate now tests stale generation,
exclusive ownership and refusal while the device is mounted. Beast's full
format/provider and discovery gates pass, and the DE25 AArch64 provider
build passes with `CROSS_COMPILE=aarch64-linux-gnu-`. No guest-facing
partition edit is enabled yet. A separate Beast probe showed stock `sfdisk`
cannot reopen the inherited exclusive descriptor after dropping to the
provider user (`Permission denied`). See `docs/DEVICE_AND_VOLUME_PLAN.md`
for the remaining safe worker/native ABI seam. The inactive physical candidate
below predates this provider change and must not be used to claim a partition
edit gate.

Filesystem simultaneous-partition and safe-removal checkpoint, 2026-10-23:
the same inactive candidate
`e8a19a632becd5449e9f741fedf3dbdb009f48f1f85aa5fd35b93560c965139d`
passed another guarded private DE25 boot. SanDisk Cruzer serial
`FC006A81F5493213` exposed four MBR partitions at once: FAT32 `/usb0`
and exFAT `/usb1`, `/usb2`, `/usb3`, all mounted writable. Native Terminal
created, read and removed a distinct file through every mount. Desktop
displayed all four volume labels. With shell CWD on `/usb0`, native `lsof`
identified its directory handle and `unmount /usb0` refused removal with
both actual process/handle records, inode and path. After CWD moved to
`/work`, `lsof` showed no holders, unmount removed only `/usb0`, and an
exact-source native mount restored `/usb0` while the other three stayed
mounted. Disk Setup selected mounted partition 3, displayed four device
mounts and disabled both format actions. Guard reported
`TRIAL_RESTORED result=0`, no candidate panic, unchanged selected production
release and stopped production-image SHA-256
`f1689b87db68a49505f3d9e5e712a9fd9123e821fadbea0410ea998cfb29cbdd`;
all three production host services are active. Evidence is under
`/var/tmp/astra-fs-private-trial-multipart-20261023/` on DE25; HDMI frames
are `/private/tmp/multipart-busy.png`, `/private/tmp/multipart-desktop.png`
and `/private/tmp/multipart-disksetup-busy.png` locally. Partition-map
editing, physical data-CD and optical media-change/eject gates, and full
production deployment remain open.

Filesystem post-format checkpoint, 2026-10-23: candidate
`e8a19a632becd5449e9f741fedf3dbdb009f48f1f85aa5fd35b93560c965139d`
passed a guarded private DE25 boot with a matching image. Native Disk Setup
reformatted only user-authorized disposable SanDisk `/dev/sda3` (1 GiB at byte
offset 8,590,983,168), first as FAT32 and then exFAT. After each format the
new source automatically mounted writable at `/usb0`, with label `ASTRA`,
without manual mount or host udev trigger. Native Terminal created, read and
removed a file on each filesystem. Desktop showed the named `ASTRA` icon;
double-click opened Atlas on the empty exFAT root. The provider regression
gate failed before the fix when a sibling source temporarily held format
exclusivity, then passed with retryable `EBUSY` and `FORMATTING` handling;
the full real loop provider gate and discovery unit gate passed on Beast.
The private trial completed without a panic and reported
`TRIAL_RESTORED result=0`; selected production release
`ccf82877667a74461e8824c4dbcbe2ead56548cd19bfd18b50b80fd2ba295a30`
and its stopped image SHA-256
`4f2497801acd8dd182127ae69175775e717a3cbaffb2cdd719624f09eca99d5b`
were unchanged. All three host services restarted active. Evidence is under
`/var/tmp/astra-fs-private-trial-format-fix2-20261023/` on DE25; Atlas HDMI
capture is `/private/tmp/format-fix2-atlas.png` locally. Candidate remains
inactive. Native partition-map editing, physical data-CD and optical
media-change/eject gates, and full production deployment remain open.

Filesystem physical checkpoint, 2026-10-23: candidate
`724ae9e632647ccdf8afac3652bbe8cbfc802b04414aa83a0ef11fcfcff2266f`
passed a guarded private DE25 boot with its own matching state image. User
authorized erasing SanDisk Cruzer serial `FC006A81F5493213`; only its disposable
`/dev/sda3` and `/dev/sda4` partitions were reformatted. Native Disk Setup
converted FAT16 to FAT32 (`ASTRA32T`) and NTFS to exFAT (`ASTRAEX`) through
the provider's held exclusive descriptor. Both probed with their new labels,
mounted writable, and passed native Terminal create/read/remove. exFAT mounted
automatically; FAT32 required an explicit native mount after format, so the
post-format automatic-mount gate remains open. Desktop showed both named
icons, and double-clicking `ASTRAEX` opened Atlas with the empty root and
status zero. Stopped-media `fsck.vfat -n /dev/sda3` and the pinned
exfatprogs 1.4.3 `fsck.exfat -n /dev/sda4` passed clean. Evidence is under
`/var/tmp/astra-fs-private-trial-format-coherent-20261023/` on DE25; captured
Atlas frame is `/private/tmp/astra-fs-atlas0.png` locally. The guarded trial
reported `TRIAL_RESTORED result=0`, matching production-image hash and
unchanged selected release `ccf82877667a74461e8824c4dbcbe2ead56548cd19bfd18b50b80fd2ba295a30`.
`astra.service`, `astra-audio-host.service`, and
`astra-remote-desktop.service` are active with zero restarts. That candidate
remained inactive; its post-format FAT32 automount gap was resolved in the
later checkpoint above.

HDMI DVD playback certification is deferred. The first native SDL presentation
trials showed up to 3.875-second video gaps and 3.2 seconds of audible-path
silence despite zero host audio underrun counters. A later trial caused a
kernel panic in deadline removal; a worker-service timer deferral fix passes
host kernel/boot tests and a 20-second native QEMU smoke on a coherent current
image, but has not passed physical regression. A private DE25 trial with
matching ROM, userspace, storage provider and QEMU launched services, then
board SSH stopped completing banner exchange while ICMP stayed alive. The
trial service's journal subsequently reported an out-of-memory kill; its
guard restored production. The stopped production image SHA-256 before and
after was
`971106b2159133b9a7359659fe74625fe5d4386df4515e2c79913ff613957ff4`;
the selected release before and after was
`ccf82877667a74461e8824c4dbcbe2ead56548cd19bfd18b50b80fd2ba295a30`.
All three host services are active with zero restarts. The 138 MiB video
fixture was in host tmpfs on a board with no swap; do not repeat that staging
method. Filesystem work is the current priority. See `docs/VLC_DVD_SPIKE.md`
for playback evidence and deferred gates.

DVD playback/VLC probe on 2026-10-22: DE25 mounted physical CSS-protected
`DVD_VIDEO` at `/cd0`. Static VideoLAN DVD libraries and FFmpeg 9.0.2
decrypted and navigated title 1 on physical `/dev/sr0`. Software decode of
30.03 seconds delivered 900 MPEG-2 frames converted to RGB565 and English
AC-3 converted to 48-kHz stereo PCM in 6.958 seconds wall, 4.903 seconds
user CPU, 10,544 KiB peak RSS. A paced 30-second run delivered 29–34 frames
per full second after startup, with no later decode stall, but opening disc
caused an initial 3.864-second read lag. Both tests used null output: native
Astra video/audio presentation and HDMI stutter certification remain open.
Production host services stayed active with zero restarts. Beast
cross-configuration of VLC 3.0.24 passed; static core compilation exposes
missing POSIX networking and pthread cleanup/cancellation APIs. Native-backed
process-private POSIX semaphores pass target and host gates. Qualified FPGA
shell has 6,046 ALMs free but +0.007 ns worst setup slack; software DVD decode
and conversion have measured headroom, so FPGA decoding is not justified now.
Exact evidence and next gates:
`docs/VLC_DVD_SPIKE.md`.

Selected production is panic-fix/Fonts-namespace DE25 release
`ccf82877667a74461e8824c4dbcbe2ead56548cd19bfd18b50b80fd2ba295a30`,
published on Beast from source revision `99f10831-dirty` with the pinned
FluidSynth checkout and the existing production Linux kernel package. Its
ROM SHA-256 is `ca408ae75ce49c1fee7ab73ece2784ba16a16e4e91fc4b1624ab24d31a6ee36a`;
kernel/ROM build revision is nonzero `0x99f10831`. The exact package verifies
on Beast and DE25. Beast kernel `test all verify`, 120-second native panic
smoke, and the native Fonts UI long-style import/selection/uninstall gate pass.
The read-only installed package passed a private DE25 stage-8 boot and 20
native Fonts information roundtrips on 2026-10-21, with no panic. Its guarded
restoration reached the old production desktop and verified identical stopped
production-image hash and selector, with all three host services active and
zero restarts. Evidence is under `/var/tmp/astra-panic-fix-physical-20261021/`
on DE25 and `/var/tmp/astra-panic-fix-physical-info-retry-20261021.log` on
Beast. First private attempt rejected a writable staging package before guest
boot; its guarded restoration passed. A second exact-package private soak
passed 32/32 DE25 cycles in 5,631 seconds, including 640 native Fonts
information roundtrips, with zero candidate panics. It crossed the earlier
cycle-23 failure window. Guarded restoration again matched stopped production
image and selector and reached stage 8 with all three services active and zero
restarts. Evidence is under `panic-fix-full/` in the same DE25 trial root;
Beast log is `/var/tmp/astra-panic-fix-physical-soak-20261021.log`.

During the private soak, stopped production image was copied read-only to
Beast. Both hashes match `7c13d449dca3d9318b90f38a234b8d2ea2e7f0d625647cdcd0d916272c5ab647`.
Journal-recovered copy has neither legacy `/config/system/fonts/installed` nor
new `/fonts/installed`; no imported AFNT migration is required for this image.
Current source was installed on a separate clone, and
`verify-state-preservation.py` passed: 25 existing user-state entries retained,
one missing default seeded, both observer input hashes unchanged. Clone is
`/var/tmp/astra-production-upgrade-20261021.img` on Beast, SHA-256
`6983be2276c8cd251484eac82434644c21de7a269a0eaeb17776991252800bcc`;
production image has not been modified. Native exact-ROM panic smoke on this
clone passed 120 seconds with no panic; log is
`/var/tmp/astra-production-upgrade-native-smoke-20261021.log` on Beast. This
clone also passed the native Fonts UI long-style import, information, system
choice, and uninstall gate with the exact ROM; log is
`/var/tmp/astra-production-upgrade-font-ui-20261021.log` on Beast. This
clone was a rehearsal. After the soak restored the old production guest, its
new stopped image was copied to Beast with matching SHA-256
`a8e17e4f3a3045b86d863c7ca236abfc7e2f96aaea018f3284f2b578c41922fd`.
Final image again had no legacy imported AFNT directory. The separate upgraded
clone passed the same state-preservation gate: 25 existing entries retained,
one default seeded, both input hashes unchanged. Its SHA-256 was
`fc86f2557b66232acbbf724043f0e1792753e2945744ecc83686da967da8de73`;
DE25 staged it as `root:astra 0600` under the new release identity before
activation. Old image and release remain retained as `previous`.

Standard deployment passed as
`ASTRA_DE25_RELEASE PASS release=ccf82877667a74461e8824c4dbcbe2ead56548cd19bfd18b50b80fd2ba295a30`
in `/var/tmp/astra-panic-fix-production-deploy-20261021.log` on Beast. The
deployer verified the packaged Linux kernel, installed release, live non-root
QEMU path, audio and remote services. Production guest reached stage 8 and a
completed desktop frame; all three host services remain active with zero
restarts, no new panic report, and `previous` points to `57078599`. Physical
1920×1080 HDMI DMA capture passed with four channels; frame SHA-256 is
`a5808cd0d67ed6dbaf1252af07c761d264665962672b159991910d783f9da31e`.
The RGB frame was inspected and shows the expected desktop/icons without
visible corruption. Beast native QEMU against the exact ROM and final
preserved-state clone passed the 10-second panic smoke, full Atlas DH0/RAM0
root, 257-entry, UTF-8, empty/error and retirement gate, and Volume UI's
33-holder busy dialog, modal scroll, safe unmount, disappearing icon and ISO
source-retirement gate. Logs are
`/var/tmp/astra-panic-fix-correct-native-smoke-20261021.log`,
`/var/tmp/astra-panic-fix-atlas-native-correct-20261021.log`, and
`/var/tmp/astra-panic-fix-volume-ui-native-correct-20261021.log` on Beast.
These gates used the native x86-64 QEMU build and a privileged log observer;
QEMU itself ran as UID 1000. Earlier attempts with the packaged AArch64 QEMU
under Beast binfmt failed to load `posixd`, and unprivileged-observer runs
could not read root-owned syslog exports. Neither is a candidate kernel panic
or valid native gate result. Broader live monitoring remains open.

Physical USB hotplug passed on the selected DE25 release on 2026-10-22. A
SanDisk Cruzer connected through the USB hub appeared as `/dev/sda` with an
Ubuntu installer partition table. The provider discovered the partition
sources and automatically mounted its 51.6 GiB ext4 `writable` partition
read/write at `/usb0`; native `volumes --all` reported the exact mounted
source. The desktop displayed its `writable` icon, and double-click opened
Atlas on `/usb0` with five readable root folders and status 0. Native Terminal
created a uniquely named 24-byte file on `/usb0`, read the exact bytes back
through `/usb0`, then removed that file; a second native lookup confirmed its
absence. The provider's private mount remained read/write, `astra.service`
and remote desktop stayed active with zero restarts, and no new panic report
appeared. Captured RFB frames, native command output, syslog snapshot and
device inventory are on Beast at `/var/tmp/astra-physical-usb-20261022/`.
This drive's EFI partition was FAT12, not FAT32, so FAT32 was untested at
that checkpoint.
Physical USB DVD discovery and read-only UDF automount passed with a Pioneer
BDR-XD08 and a `DVD_VIDEO` disc: `/cd0` exposed `VIDEO_TS`, IFO/BUP files and
VOB segments through native directory enumeration. Physical data-CD ISO9660,
optical media change, safe removal and eject remain unqualified. The USB drive
was mounted during its 2026-10-22 test; current physical attachment varies.
Partition discovery represents each Linux-reported partition as its own source,
but native catalog records omit partition number/bounds. This drive's FAT12
source remained unmounted and had no desktop icon; its recognized ISO9660
source also remained unmounted because that USB policy permitted only ext4,
FAT32 and exFAT. Neither multiple supported-partition automount nor partition
editing was physically qualified at that checkpoint. New requirements in the
production PRD call
for per-partition `NDOS` icons with clear open errors, read-only mounting of
recognized optical filesystems on USB, and a native Disk Setup app reached
from `Disk > Format...`; no destructive device action has been enabled. The
post-release source now recognizes FAT12/FAT16 and admits their USB mounts
read-only, plus ISO9660/UDF on USB read-only and NTFS on fixed/USB data
partitions read-only. Focused Beast catalog, probe, mount and native client
tests pass, including same-device multi-partition catalog mounts; exact-kernel
NTFS3 module builds with expected vermagic. Coherent QEMU/DE25 qualification
and production deployment remain open. Other common formats remain unimplemented;
[AstraFS](ASTRAFS.md) remains design-only.
Whole userspace `all` passes on Beast; its first `test` run found an
uninitialized Fonts selection record in a host layout test. Initializing that
record passes the focused Fonts test and the complete userspace `test` gate,
including m68k Fonts relink. Logs are
`/var/tmp/astra-fs-formats-userspace-20261022.log` and
`/var/tmp/astra-fs-formats-userspace-tests-20261022.log` on Beast. QEMU host
and DE25 target builds against native catalog version 3 both exit zero; logs
are `/var/tmp/astra-fs-formats-qemu-host-20261022.log` and
`/var/tmp/astra-fs-formats-qemu-de25-20261022.log`. These are build gates,
not a live guest or DE25 media test.

The 2026-10-23 in-progress filesystem candidate publishes physical device
capacity and partition map, and source partition number, byte offset and size
through Volume catalog 4 / Filesystem Kit ABI 15 / Linux provider protocol 3.
Probe results distinguish blank `NDOS` sources from recognized but unsupported
formats and retain the detected type for clear UI/CLI errors. Desktop now
presents nonmountable physical source icons and refuses to launch Atlas on
them. Focused Beast storage catalog, discovery, provider and probe gates pass,
including partition geometry changes, removal with unavailable sysfs geometry,
and an unsupported swap signature; the AArch64 provider builds. Full userspace
`all test` and native-host and DE25 QEMU builds pass. Logs are
`/var/tmp/astra-partition-provider-final-20261023.log`,
`/var/tmp/astra-partition-userspace-final2-20261023.log`,
`/var/tmp/astra-partition-qemu-host-20261023.log` and
`/var/tmp/astra-partition-qemu-de25-20261023.log` on Beast. Beast kernel
`test all verify` and ROM `all verify` pass against this candidate. The
coherent native QEMU Volume UI gate passes on a separate clone with a
privileged log observer and QEMU running as UID 1000: Desktop icons, 33 exact
busy holders, modal diagnostics, safe unmount and ISO source retirement. Log:
`/var/tmp/astra-partition-volume-ui-root-observer-20261023.log`. The matching
native Atlas gate also passes DH0/RAM0 roots, 257 entries, UTF-8, empty/error
states, reflow and stale-incarnation refusal; log:
`/var/tmp/astra-partition-atlas-root-observer-20261023.log`. Earlier
unprivileged-observer runs could not read root-owned icon logs and are invalid
UI verdicts; a stale installed host provider also caused the first startup
attempt to fail before QMP. Beast's test provider now matches protocol 3;
the previous installed binary is saved at
`/var/tmp/astra-storage-before-catalog4-20261023`. DE25 media and release
gates remain open. No production image or selector was
changed for this candidate. Haiku review and adapted source paths are recorded
in `docs/DEVICE_AND_VOLUME_PLAN.md`.

Fresh complete DE25 kernel package built on Beast for the existing running
`6.12.11-astra-storage1-gd7d192a9ddd9` kernel; package checksum passes and
contains `sr_mod`, ISO9660, UDF, exFAT and NTFS3 modules. Clean publisher
staged and verified candidate release
`65518857cb5e3f9dd250b0a4ae14132a47865f14d11ceda3b7b37e79767f60a0`
with catalog 4 / Filesystem Kit ABI 15 / provider protocol 3. Board installed
it without activation. Its isolated DE25 boot on 2026-10-23 showed four GPT
USB partition sources: ISO9660, FAT12, blank NDOS and ext4. Desktop showed
NDOS and three USB mount icons. Native `volumes --all` reported exact partition
geometry and FAT12/ISO9660 read-only versus ext4 writable. Guest `ls`
enumerated all three roots. Ext4 write/read/remove passed; double-clicking the
ISO9660 icon opened Atlas with nine entries and status zero. Evidence and
checksums are on Beast at `/var/tmp/astra-partition4-physical-20261023/`.
No candidate panic. Private run stopped production audio/remote hosts, so
their guest service retries are not a complete release gate. Production
selector stayed `ccf82877...`; stopped production image SHA-256 stayed
`43b65a5fb6db078ee5af29413d4f6b6217a76c9351037dd345a99204e4f8b`.
Restored production reached stage 8; all three host services active with
NRestarts=0. Native Disk Setup and safe edit controls, FAT32/exFAT physical
media, data-CD ISO9660, optical change/eject and full release acceptance remain
open. Same installed RBF and timing capacity are recorded in
`fpga/de25/TIMING_CLOSURE.md`.

After that private gate, Filesystem Kit's export map was corrected to match
its ABI 15 library identity. A clean Beast userspace rebuild followed by
complete `all test` passed; the resulting `filesystem.library.15` exports one
`ASTRA_FILESYSTEM_15.0` namespace, and rebuilt `config.library.1` imports
15.0 symbols. Log: `/var/tmp/astra-filesystem-abi15-full-pass-20261023.log`.
The shared Volume client now provides exact snapshot record and ancestor
lookups with focused graph tests. An unshipped native Disk Setup inventory
prototype compiles and links against those interfaces; it lists physical
devices, partitions and whole-disk sources using shared Interface Kit controls
and retries catalog snapshots during hotplug. It has no edit controls or
partition/format operations yet. Source changed after release
`65518857...` was staged, so that candidate does not represent this gate.
Clean publication staged and verified matching release
`c0d905b4c68e705872d28cbca64c4b24eba67da101226e0c3738513f73e6fc94`
on Beast; log is `/var/tmp/astra-fs-abi15-publish-20261023.log`.
Its ROM checksum `008898b4c4250f41ae91d51b7786a830fc4c48559d90005b54f2312012dcd6e3`
matches the tested ROM. Matching native host QEMU and host storage provider
ran root-observed guest gates with QEMU as UID 1000 against disposable clones.
Volume UI passed exact 33-holder diagnostics, safe unmount, disappearing icon
and retained ISO source release; Atlas passed DH0/RAM0 exact roots, 257
entries, UTF-8, empty/error cases, reflow and stale-incarnation refusal.
Logs: `/var/tmp/astra-fs-abi15-volume-ui-native-20261023.log` and
`/var/tmp/astra-fs-abi15-atlas-native-20261023.log`. New release remains staged
on Beast; it has not been selected on DE25 or tested there. Native Disk Setup
still lacks format/partition operations and its application bundle.
Board read-only inventory after these gates still shows the 57.7 GiB SanDisk
Ubuntu installer at `/dev/sda` (ISO9660, FAT EFI, blank, ext4 partitions),
and the selected release remains `ccf82877667a74461e8824c4dbcbe2ead56548cd19bfd18b50b80fd2ba295a30`.
The attached installer has not been reformatted. No data CD is attached.

The next 2026-10-23 filesystem candidate ships native Disk Setup as a
read-only inventory application. Desktop offers `Disk Setup...` on physical
source and mounted physical-volume context menus, passing the exact catalog
reference; the application groups physical devices and sources with shared
Settings pane, List and Scroll View controls. It deliberately has no
partition or format edit authority until provider-side safety and disposable
media gates exist. Desktop now retains the complete catalog graph and keeps
only icon indices, so mounted-volume ancestry survives filtering. The Linux
provider publishes observed write protection for devices and sources, including
partition inheritance from the parent disk, and rejects writable mounts of
read-only sources. Native catalog/discovery/provider/probe gates pass on Beast;
log: `/var/tmp/astra-fs-disk-setup-provider-final-20261023.log`. Full userspace
`all test`, ROM `all verify`, provider-index image tests, and native QEMU
desktop launch of five application bundles including Disk Setup pass;
logs: `/var/tmp/astra-fs-disk-setup-userspace-20261023.log`,
`/var/tmp/astra-fs-disk-setup-rom-20261023.log`, and
`/var/tmp/astra-fs-disk-setup-desktop-apps-20261023.log`. The native Volume UI
gate also passes exact 33-holder diagnostics, safe unmount, icon removal and
retained ISO source release; log:
`/var/tmp/astra-fs-disk-setup-volume-ui-20261023.log`. NDK `dist-check`
passes after documenting new exact-reference helpers; log:
`/var/tmp/astra-fs-disk-setup-ndk-final-20261023.log`. Current board
read-only inventory still sees only the 57.7 GiB SanDisk Ubuntu installer
(ISO9660, FAT EFI, blank and ext4 partitions); no data CD is attached. This
candidate is not deployed or selected on DE25. Partition creation/deletion,
formatting, FAT32/exFAT physical writes, optical change/eject and final
release gates remain open.

Clean publisher staged and verified the final inventory candidate on Beast as
`1b4f085c95b5d88fb5bdfa0bc023e6a72a8fe1094ca18757069b37333c618107`.
Its source manifest SHA-256 is
`bf167a48fbe50fff1d75c044663f84497a34347ae8ee806e21dd623048691407`;
ROM SHA-256 is
`81357bd80b8a095115dec0e0c301e6534468b8e3609202df5a79dff1e2e95df4`,
identical to rebuilt ROM. The first staged candidate `468cba94...` is
superseded by this one after a catalog sequence-overflow mutation fix and
regression. Matching native QEMU release-image gates pass: five desktop
application launches including Disk Setup, and complete Volume UI busy,
unmount, icon-retirement and ISO-source cleanup. Logs:
`/var/tmp/astra-fs-disk-setup-final-publish-20261023.log`,
`/var/tmp/astra-fs-disk-setup-final-desktop-apps-20261023.log`, and
`/var/tmp/astra-fs-disk-setup-final-volume-ui-20261023.log` on Beast.
Production DE25 remains on `ccf82877...`; this release has not been installed
or selected there. The 57.7 GiB Ubuntu installer remains intact; no data CD
or disposable FAT32/exFAT drive is attached. Board root had 1.4 GiB free at
this checkpoint; any future private candidate transfer must budget both its
incoming and installed copies before consuming that space.

Post-candidate source now rejects a partition extent beyond its parent's
known device capacity before catalog admission. Discovery ignores that invalid
partition without terminating the provider and can admit a later corrected
observation. Focused catalog tests pass on Beast host and AArch64 QEMU;
discovery and provider tests plus the AArch64 provider build pass. The staged
`1b4f085c...` package predates this guard. DE25 still sees only the SanDisk
Ubuntu installer and internal SD, so no destructive FAT32/exFAT test has run.
Partition write protection now inherits optical class from the parent device,
even when an inconsistent partition observation reports another transport
class. A focused discovery regression passes; no writable optical partition
is published by that path.
Beast and DE25 both provide `sfdisk`, `mkfs.fat`, `mkfs.ext4` and `wipefs`;
neither currently provides `mkfs.exfat`. Those host utilities are not a native
Disk Setup administration API and have not been wired to guest requests.
The root-observed native QEMU optical-image gate now passes ISO9660 and UDF
root reads, desktop icon appearance/disappearance, write refusal, busy-holder
diagnostics and image-source retirement using the matching release ROM and
current host provider. Beast log:
`/var/tmp/astra-fs-optical-guest-20261023.log`. Image sources retain their
optical contract and deliberately refuse FAT12/FAT16/NTFS image mounts; those
formats still need block-device guest/physical gates. The first expanded full
lifecycle trial failed at its later HostFS-restart Terminal click after the
new Desktop reported ready. The gate now waits for a complete current Desktop
icon inventory and a quiet display frame before that click. Two consecutive
full lifecycle retries pass ISO/UDF, RAM remounts, HostFS restart, fresh
Terminal, stale-reference refusal and durable shutdown with a clean `e2fsck`.
Failure log: `/var/tmp/astra-fs-udf-guest-20261023.log`; passing logs:
`/var/tmp/astra-fs-udf-lifecycle-retry-20261023.log` and
`/var/tmp/astra-fs-udf-lifecycle-repeat-20261023.log`. The first failure is
consistent with a test click before the new frame settled; no panic was
reported. Physical media gates remain separate.

Post-candidate catalog admission now also refuses a mount when another live
physical source on the same medium generation has an overlapping byte range
and a mounting, mounted, quiescing or retained mount. Unknown source bounds are
treated as overlapping. Disjoint partitions still mount concurrently. Beast
host and AArch64 QEMU catalog tests pass, alongside discovery and provider
tests and the AArch64 provider build. Automount now treats overlap `EBUSY` as
a per-source refusal, leaving the source visible without stopping discovery.
This invariant is newer than the staged
`1b4f085c...` release and has not been deployed to DE25.
The overlap fence also follows retained physical sources through a retired
medium on the same device. A newly observed medium cannot mount the same bytes
until the old Linux mount is actually detached. Beast host, AArch64 QEMU and
sanitizer catalog tests pass this media-change case.
Discovery also retires a disk's current medium and partition sources when its
reported capacity, partition map or write protection changes, even if the
observation is PRESENT rather than CHANGE. This forces every partition through
fresh bounds and read-only checks; stale geometry cannot remain mountable.
Per-partition write-protection changes retire and reprobe that source.
The real Linux provider now has disposable block-image tests for USB-class
ISO9660/UDF, FAT12/FAT16 and NTFS read-only mounts. They check detected label
and format, reject writable admission, open the mounted root, reject file
creation, then unmount and release the loop/device lineage. Beast provider
gate passes normally and with ASan/UBSan; the mount-seam gate passes normally,
and the AArch64 provider builds.
The same provider fixture now passes FAT32 and exFAT writable creation, exact
readback after unmount/remount, deletion and cleanup. The exFAT gate uses an
isolated Ubuntu `exfatprogs` 1.2.2 package extraction at
`/var/tmp/astra-exfat-test/pkg/usr/sbin/mkfs.exfat` on Beast; nothing was
installed on DE25. `storage-provider-exfat-test` passes with that path as
`EXFAT_MKFS`, and the exFAT branch also passes under ASan/UBSan. The ordinary
provider test includes FAT32; exFAT needs the explicit formatter argument.
These are provider gates, not MC68040 guest or physical DE25 qualification.

Coherent filesystem release `79b5dc6057401bb17a152abbd3df423cf017595ca24791f0cda9078de5a9eefe`
was staged and verified on Beast from the current source; ROM SHA-256 is
`e4ce70bd9be912b76b119fb6d1c0f4eed419aa064921a6f489160ec196972348`.
Matching native QEMU/provider and disposable image passed the full Linux-first
lifecycle gate (ISO/UDF, busy holders, Atlas, RAM remount, HostFS restart,
stale references and clean e2fsck), all five desktop application launches,
and the Volume UI's 33-holder/unmount/retirement gate. QEMU ran unprivileged
under a root observer. This release was never selected for production.

The user authorized erasing USB serial `FC006A81F5493213` only. DE25's
57.7 GiB SanDisk Cruzer was repartitioned as MBR with a 4 GiB FAT32
`ASTRA_FAT`, 4 GiB exFAT `ASTRA_EXFAT`, and 1 GiB blank NDOS partition.
Internal `/dev/mmcblk0` and the production Astra image were not formatted.
The exFAT formatter was extracted from Ubuntu arm64 `exfatprogs` 1.1.3 into
`/var/tmp` for this fixture, not installed system-wide. Host FAT32/exFAT
fsck passed after format. A private DE25 candidate boot showed all three exact
partition sources, simultaneous writable `/usb0` and `/usb1`, and NDOS on
Desktop. Native Terminal created and read exact files on both supported
volumes. Guarded teardown detached provider mounts; offline read-only Linux
mounts recovered the same bytes and both filesystems passed fsck. A second
private trial safely unmounted both volumes, observed both Desktop icons
disappear, remounted source IDs 11 and 12 writable, read both files back,
observed icons return, and opened each root in Atlas with one file and status
zero. Trial logs and readback artifacts are under
`/var/tmp/astra-fs-private-trial-fat-exfat-20261023/`,
`/var/tmp/astra-fs-private-trial-removal-20261023/`,
`/var/tmp/astra-disposable-cruzer-format-20261023/`, and
`/var/tmp/astra-disposable-cruzer-verify-20261023/` on DE25.

First private trial failed because staged release root was mode 0500 and the
unprivileged input helper could not traverse it; the staged root was changed
to 0555. Second trial's guard incorrectly watched serial for `desktop ready`;
Desktop had reached stage 8 and published its viewport in syslog. That guard
timed out and restored production; one production runtime restart followed a
transient provider `ENODEV`. Later corrected FAT32/exFAT and removal trials
completed with guard result zero, matching stopped production-image hashes,
unchanged release selector, active host services and no candidate panic.
Production remains release `ccf82877...`. Disposable private image and board
candidate stage were removed after evidence capture; board free space returned
to about 1.3 GiB. Native Disk Setup still has no partition/format authority.
At that checkpoint, FAT16 and NTFS guest/physical gates, optical
data-CD/media-change/eject gates, and full release qualification remained open.

The same disposable Cruzer now has a 1 GiB FAT16 `ASTRA_FAT16` third
partition and a 1 GiB NTFS `ASTRA_NTFS` fourth partition; the FAT32 and
exFAT extents and data remain intact. Host readback confirms `LEGACY.TXT`
on FAT16 and NTFS; NTFS seeding used the board's existing `ntfs-3g` tool.
A guarded NTFS3 trial found a kernel identity collision: the running DE25
`6.12.11-astra-storage1-gd7d192a9ddd9` Image differs from Beast's package
of that same release, and the board has no matching `ntfs3.ko`. No module
from the different Image was installed. A new complete Beast package with
unique release `6.12.11-astra-storage2-gd7d192a9ddd9` built and passed its
manifest and module-vermagic checks. Board production was restored after
each guarded host mutation. Evidence is on DE25 under
`/var/tmp/astra-legacy-partitions-20261023/` and
`/var/tmp/astra-ntfs-seed-20261023/`.

Coherent release `b2ef5a6834b5fb91de66dbba278e85554724a17e4adf57c73e58109aee693f84`
includes that complete package. Beast real FIT/full-kernel stage passed with
1,345 modules; native QEMU Linux-first lifecycle and clean e2fsck passed.
The exact new ROM also passed the native Volume UI gate with 33 named busy
holders, modal scrolling, safe unmount, disappearing icon and ISO retirement;
Beast log is `/var/tmp/astra-fs-correctness-gate2-volume-ui-20261023.log`.
DE25 staged and armed the package, rebooted into `storage2`, and exact
installer verification passed for the running Image, DTB, modules and boot
script. The original production runtime selector remains `ccf82877...`;
its services returned active, guest reached stage 8, and no new panic report
appeared. The kernel transaction remains rollback-capable and unconfirmed.
The private DE25 candidate mounted all four Cruzer partitions simultaneously:
FAT32 `/usb0` read/write, FAT16 `/usb1` read-only, exFAT `/usb2` read/write,
and NTFS3 `/usb3` read-only. Native `volumes --all` reported exact partition
numbers, offsets, sizes, labels and access modes. Native Terminal read seeded
FAT16 and NTFS files byte-for-byte, attempted writes produced no new file,
and the provider's Linux namespace confirmed `vfat` and `ntfs3` mounts were
read-only. Double-clicking each read-only desktop icon opened Atlas with one
root entry and status zero. Guarded teardown restored all three original
services, identical stopped production-image hash and unchanged selector;
candidate panic report was absent. Evidence is under
`/var/tmp/astra-fs-private-trial-fat16-ntfs-20261023/` on DE25.

The running `storage2` package also passed two 1920×1080 HDMI DMA captures
with four channels and identical frame hash
`6a3a5d2fb2d44ea2c1f3dd11bd1c2c636509c462d0f8f1b6ca383d02e0d69141`;
the frame was visually inspected and shows the production desktop without
corruption. Remote desktop was restarted after the exclusive captures; all
three host services remain active with zero restarts in this boot. A repeat
kernel arm revealed that deployment left a 497 MiB duplicate incoming release
after an already-installed release was reused; the verified temporary copy
was removed and `deploy-de25-release.sh` now retains `INCOMING` for its exit
cleanup. This fix is not in the staged `b2ef5a68` source snapshot. Native
Disk Setup partition/format authority, physical data-CD/media-change/eject,
full release gates and production runtime activation remain open.

Disk Setup now reads the native usage inventory as well as the volume catalog.
It accepts a view only when both have the same catalog sequence, and shows the
number of mounted volumes and retained resources for the selected physical
disk or partition. The app has `VOLUME_USAGE` inspection authority, not
destructive administration authority. The MC68040 app build and image-policy
test pass. Coherent release snapshot
`922a064ae25cb53126eb06e17d73923e3ef0b589917dc17ed04ed0fa93c6423d`
is staged on Beast at `/var/tmp/astra-fs-correctness-gate3-20261023`. Its native
Beast QEMU/provider desktop gate launched all five applications, including
Disk Setup, and passed 5,801 completed frames in two seconds. This gate used
the native x86-64 QEMU/provider from the same sources; the packaged AArch64
DE25 executables are not a valid Beast throughput or boot-time test. The
post-publication test-harness diagnostic change is not in this release source
manifest. Partition-map edits, filesystem formatting, physical data-CD and
media-change/eject tests, and production runtime activation remain open.
The 2026-10-23 exclusive-descriptor loop gate found that stock `mkfs.fat`
reopens `/proc/self/fd/N` and fails `EBUSY` while the verified block device is
held with `O_EXCL`. Native formatting therefore needs a formatter that writes
through the admitted descriptor; dropping exclusivity at execution is not an
acceptable production admission contract. See `docs/DEVICE_AND_VOLUME_PLAN.md`.
Pinned dosfstools 4.2 and exfatprogs 1.4.3 sources plus narrow inherited-FD
patches now live in `third_party/storage-format/`. Beast host loop gates format
FAT32 and exFAT while retaining `O_RDWR|O_EXCL`, reject regular files and a
mounted target, identify both new volumes, and pass their fsck checks. A
disposable loop MBR edit through `sfdisk`'s inherited FD also publishes its
partition after close. Exact archive-hash builds produce host and DE25
AArch64 binaries. The first AArch64 build accidentally linked host glibc 2.38
symbols and failed under the DE25 sysroot; explicit target headers and sysroot
fixed that. Both AArch64 formatters then passed the same exclusive-descriptor
loop test under Beast QEMU user mode as the non-root caller against the DE25
sysroot, with host fsck checking their output. These formatters are not in the
selected production release and no native Disk Setup administration path
exists yet.

Unshipped source candidate after the exclusive-descriptor work bumps the
native volume catalog to version 5 and the private storage-provider protocol
to version 4. Catalog records now distinguish physical write protection from
filesystem read-only policy, and a format admission state fences sibling mounts
across a device. Format completion invalidates the old source generation and
requires a fresh probe. Beast catalog host/AArch64-QEMU tests, catalog
ASan/UBSan, discovery and provider tests, and the full VFS host suite pass;
the MC68040 Disk Setup object builds. This is lifecycle groundwork only:
there is no guest format/partition request, provider formatter execution,
physical native administration gate or production deployment for this source.
The next unshipped storage slice shares exact Linux block-device verification
between probes and an exclusive `O_RDWR|O_EXCL` format opener. A real disposable
loop gate confirms exact device identity and retained exclusivity; fake/stale/
reserved devices refuse. An existing provider worker now executes the pinned
FAT32/exFAT formatters as the non-root provider user through only that held FD.
Beast's disposable host loop gate passes both formats and fsck; catalog abort
restores original metadata if worker admission fails before writes. Provider
control, native Disk Setup, operation-result reporting and production packaging
remain unconnected, so this does not enable formatting on Astra yet.

The next provider slice bumps its private protocol to version 5. Its control
socket starts FAT32/exFAT formatting against the exact source generation and
reports the result separately, retaining the exclusive block FD until worker
exit. Same-identity udev CHANGE events during formatting do not retire the
source; REMOVE and changed physical identity still do. Completion increments
source generation and reprobes before automount. The worker has a 300-second
deadline. Beast's disposable loop provider gate passes both formats, fresh
labels/probes, generation changes, control-wire validation and pinned exFAT
fsck. The DE25 AArch64 provider builds with protocol v5. The release
constructor builds and packages pinned AArch64 formatters next to
`astra-storage`; its fresh-output fixture passes. Native Disk Setup capability,
guest request, partition editing, physical format gates and production release
remain open.

Unshipped native format path adds Filesystem Kit 15.1 exports and uses a
separate `DISK_ADMIN` grant for
Disk Setup; Desktop and Terminal retain lifecycle authority only. VOL protocol
3 carries exact-source FAT32/exFAT admission and prior-generation result
polling through HostFS and QEMU to private provider protocol 5. Disk Setup uses
shared Field, Label and Button controls for volume name, format choice and
typed `ERASE` confirmation; it checks a fresh catalog/usage snapshot before
admission and polls asynchronous completion. Beast host VFS validation and
role-isolation tests, MC68040 HostFS/Disk Setup links, and host/DE25 QEMU builds
pass. The first fresh native guest image stopped before stage 8 with supervisor
manifest status `0x20`: the new `VOLUME_DISK_ADMIN` name exceeded Astra's
15-character capability field. Authority is now `DISK_ADMIN`, with an NDK
compile-time length gate; Filesystem Kit provider and consumer manifests all
declare library 15.1. NDK `test` also caught and fixed a C++-incompatible
compound literal in the shared volume-reference parser. A fresh 256 MiB image
then passed the matching ROM/QEMU/provider desktop gate: five app bundles,
including Disk Setup, launched and completed 5,862 frames in two seconds.
The native Volume UI regression passed 33 exact busy holders, modal catalog
changes, safe unmount, disappearing icon and ISO source retirement. Logs on
Beast: `/var/tmp/astra-fs-format-desktop-cap-native-20261023.log` and
`/var/tmp/astra-fs-format-volume-ui-native-20261023.log`. The failed boot is
retained as `/var/tmp/astra-fs-format-desktop-native-diagnostic-20261023.log`.
Actual guest formatting, physical USB qualification, native partition-map
edits and production activation remain open.

Superseded production `57078599` panicked again at
2026-10-21 08:52:18 UTC (board clock 16:52:18 +0800), reason `deferred worker
service failed`. Systemd restarted `astra.service`; stage 8 returned at
16:53:07 board time. All three host services are active; `astra.service`
NRestarts=2 and the other two are zero. Report preserved on Beast at
`/var/tmp/astra-production-panic-20261021/panic-latest.log`, SHA-256
`a4d233fecb0e733f892eed67ca01dbc3e16fbdaad25c0e9fe0f70b3c896d0d56`.
Old ROM lacks deferred-IRQ source/status attribution, so this panic does not
establish failure of unselected candidate.

Old production also panicked at 2026-10-20 16:54:45 UTC (board clock
2026-10-21 00:54:45 +0800), reason `syscall dispatch contract failed`, then
systemd restarted `astra.service`. The report is preserved
on Beast at `/var/tmp/astra-production-panic-20261020/panic-latest.log`, SHA-256
`30e9f700c2253f76c40cbf150cff4ba2a8bf1914056a4b616d073b0244820a96`.
Its syscall-exit trace identifies syscall 14 (`WAIT_MULTIPLE`), status 9
(`KERNEL_PROCESS_CORRUPT`), next context NULL while runtime remains active.
This old ROM does not record the failing wait branch; the new candidate's
wait-failure trace must be inspected if this recurs.
This old-ROM panic does not establish failure of the unselected candidate.

The exact `fbf87fa4` candidate completed two independent private DE25
panic soaks on 2026-10-20. The first passed 128 cycles, the second 142;
each cycle ran 20 native Fonts information roundtrips, 5,400 total. Both
units exited successfully. Each restoration reached production stage 8 and
completed desktop, with all three host services active and NRestarts=0.
Stopped production image and selector hashes match their before/after
manifests for each run. Beast native-QEMU 24-hour soak of the same ROM
completed successfully: `ASTRA PANIC SOAK PASS seconds=86400
remote-failures=8613` in `/var/tmp/astra-host-irq-candidate-native-24h.log`,
user-unit Result=success and ExecMainStatus=0. `remote-failures` counts
deliberate remote-desktop launch retries, not soak errors. Production cutover
remains paused until the full exact-release workflow finishes; these long runs
do not prove every historical panic shared the fixed host-IRQ cause. Board
evidence remains under
`/var/tmp/astra-irq-origin-physical-20261019/overnight-private-ready-full/`
and `overnight-private-ready-full-second/`.

Fonts namespace consolidation passed isolated Beast userspace build/tests,
VFS and supervisor host suites, image/preservation checks, NDK dist-check,
native QEMU AFNT import/restart/uninstall/corrupt-inventory gate, and the
native Fonts UI long-style import, information, choice and uninstall gate.
The UI gate runs QEMU unprivileged under a root observer. The initial native
import probe failure was a test manifest missing `SYSTEM:r`; the service
had loaded and adopted the saved face. The first UI gate used the retired
shipped-font path; it now enumerates `/dh0/fonts/system` through POSIX. The
native import probe exercises `/FONTS/system`. No product defect
was found in these failures. Production remains unselected for this change.

It gives `FONTS:` one disk root with protected shipped AFNT under `system/`
and imported AFNT under `installed/`; Config Kit keeps inventory identities
and preference only. The stopped-image preservation observer now includes
`/fonts/installed`, so release upgrades must retain imported AFNT bytes and
inode metadata. `FONT_CATALOG` is the read-only service capability,
distinct from the filesystem assign. NDK catalog snapshot/face/strike calls
continue to hide storage and AFNT parsing. No production image or selector
has been changed for this work. The 2026-10-19 stopped production backup had
no legacy `/config/system/fonts/installed` directory. Before any cutover,
inspect the then-current stopped image again; images with legacy imports need
a verified data migration before selection.

While the native Beast soak ran, the current Fonts image publisher was
dry-run on a clone of the stopped 2026-10-19 production backup. Its
`verify-state-preservation.py` gate passed: 25 existing user-state entries
retained with bytes and inode metadata, one missing default seeded, and both
observer input hashes unchanged. Source backup SHA-256 remains
`9f1faf7eb70ae96b33bd248c330259a971083ca5c744f96e64124642bf3324b6`;
clone is `/var/tmp/astra-font-namespace-dryrun-20261020.img` on Beast,
SHA-256 `67d24d701b5b863c132092a2310c4c914982e33a77d76e2db751f17791496b62`.
This backup predates any possible later font imports; it does not replace the
stopped-current-image preflight or qualify a legacy-import migration.

Certification resumed by user request with a stop-on-panic rule. The exact
private `bf8445bb` release panicked on DE25 during native Fonts information
roundtrip 18 of a preserved-production-state trial. This is a new observed
failure, `deferred worker service failed`, not the earlier syscall-dispatch
panic. The worker failure record says work `0x20` (deferred IRQ dispatch),
class `5`, service result `2` (FATAL), worker state `3` (RUNNING); the release
trace does not retain the IRQ source or dispatch status. Nearby user events
show `/services/media` launch failure `ASTRA_STATUS_PEER_DEAD`, but causation
is not established. Deferred IRQ counters at panic were queued=dispatched=1950,
pending=0, dropped=0. In `service_deferred_interrupts`, the read sequence and
dispatched counter advance after trace write but before a fatal dispatch
classification; a trace-write failure would leave an IRQ pending. The
observed worker failure therefore points to a fatal IRQ dispatch status,
whose source and value the old release did not retain. Panic report SHA-256
`99ac6506c1a0e6aea698df14ca9a51b804487cf32d3751d03464867032be1f21`,
matching source `26f9902c0d1331561060f807d61ea6e87298c2c6`, kernel ELF
SHA-256 `fbff7cc2b1ccdfe861c1bf41a0faf33e5ca1364259e10c0163f45f4ddbe9f118`.
Full private console, panic, trace ring, frame captures, postpanic state,
ROM, ELF and SHA verification are retained on Beast under
`/var/tmp/astra-font-preserved-panic-evidence-20261019/`. Production cutover
is PAUSED. Harness restored production release `57078599` to stage 8 and
completed desktop, all three services active/NRestarts=0, selector and
stopped production image unchanged. No user data was upgraded or overwritten.

Stopped production image and WORK/SOUND backup on Beast under
`/var/tmp/astra-font-production-backup-20261019/` passed before/after stopped
hash checks and archive verification: image
`9f1faf7eb70ae96b33bd248c330259a971083ca5c744f96e64124642bf3324b6`,
exports archive
`3cf5171647a34ce4668b8bc38895983614658e058059e5119add81d69cc25433`.
An isolated candidate clone passed strict preservation of all 25 existing
`/home`, `/local`, `/config` entries, content and inode metadata; its preboot
SHA-256 was `e24c47577f80149983d1a2eedf41414f83cf1e2f0c7027f1f937eeda6591eda8`.
A first private preserved-state test used a fixture-only malformed-frame probe
assertion on production profile, so it stopped without product failure. A
separate backup restore check opened the live production image for hashing,
breaking storage provider's exclusive lease; provider exited `ENODEV` and
systemd restarted once. This was an observer error, not a kernel panic. Its
journal is retained in the upgrade evidence; no live-image reads remain in
the corrected procedure. Production recovered to stage 8, and the later
private panic trial restored with zero restarts.

Failure-only deferred-IRQ attribution is present in existing IRQ trace:
retain source, vector, raw dispatch status and wake count before the fatal
worker return. Haiku reference reviewed read-only:
`src/system/kernel/interrupts.cpp` `io_interrupt_handler`, which preserves
individual handler results and handled/unhandled accounting. Astra retains
its native deferred IRQ worker and strict panic invariant. Existing K1/K10
baseline at source `3851eaa0` passed twelve markers; after diagnostic change,
full kernel host tests and K1/K10 still pass all twelve markers and budgets
in `/var/tmp/astra-irq-fatal-trace-{host-final,k1}-20261019.log`. A focused
test forces the existing IRQ pool-corruption path and verifies exact fatal
source/status trace arguments. The failure-only diagnostic is not a fix and
does not qualify a release.

Private diagnostic release `eb7aa80d4b57b4401030396e238bd70fa1422680cab9e2ba7af21275a06dc84c`
contains committed source `b9ef1daaa0f0accaededa6d8a9fd81c161db4a48`;
its DE25 ROM SHA-256 is
`80bb949abb27806505baf16c6c9fc816b8a65b132e9ca2dfb28c1e2d44260864`.
The first private trial was invalid: copied panic-capture/QEMU fixture paths
still named a retired trial root, so QEMU exited before the Fonts gate.
A second trial completed all 20 information-pane roundtrips, then failed a
test-only display-probe launch assertion absent from the production profile.
After correcting those private fixture paths and removing only that
test-only assertion, four independent DE25 boots from the verified
preserved-state clone completed 20 information-pane roundtrips each, 80
total, without a panic. Each trial restored production release `57078599`
to stage 8 and desktop; all three services were active with zero restarts,
and stopped production image hashes and selector matched before/after.
Checksum-verified per-cycle logs and hashes are on Beast under
`/var/tmp/astra-irq-fatal-diagnostic-evidence-20261019/`. These clean
repetitions do not explain or negate the observed deferred-IRQ panic.
Plane ASTRA68-55 tracks this deferred-worker failure. Production cutover
remains PAUSED pending root-cause evidence and a fix.

Overnight IRQ-origin diagnosis stopped on first failure at cycle 23, during
Fonts information roundtrip 6. Cycles 1-22 passed. The private DE25 guest
panicked with `deferred worker service failed`; first IRQ corruption record
identifies `sw/kernel/irq.c:948`, where `kernel_thread_wake_all_irq` failed
while delivering Vega graphics IRQ source 8 (endpoint slot 2, owner 1,
generation 1, state 3, references 2). IRQ dispatch reported FATAL, with no
pending or dropped deferred IRQs. The wake failure's internal cause remains
unidentified; the endpoint record identifies the failing call, not whether
its wait queue, registration, deadline, or ready queue broke first. Unit
`astra-irq-origin-overnight.service` is stopped/failed as intended. Checksummed
per-cycle evidence is retained on DE25 under
`/var/tmp/astra-irq-origin-physical-20261019/overnight/` and copied to Beast
under `/var/tmp/astra-irq-origin-evidence-20261019/overnight/`; cycle-023
`SHA256SUMS` verifies there. The private trial restored production to stage 8
and desktop; production selector and stopped image hashes match before/after,
and all three production services are active with zero restarts. Production
cutover remains PAUSED. Source `4ecf1a519f52f9893fa69e2b25943bd1b9f12ca7`
records the first IRQ pool-corruption site in `sw/kernel/irq.c` plus endpoint
slot, owner,
generation, source, state, references, and cache-corrupt flag. Panic report
prints this sticky first record and IRQ pool health; normal IRQ dispatch
behavior is unchanged. Haiku reference reviewed read-only:
`src/system/kernel/interrupts.cpp`, `io_interrupt_handler`, including handler
result and handled/unhandled accounting. Full Beast kernel host suite, MC68040
`all verify`, and K1/K10 qualification passed; logs are
`/var/tmp/astra-irq-origin-{kernel-gates,k1}-20261019.log`. Immutable private
release `4a3019a7a9e6bd38d0f18078626264280e3db5339a7307c780bf8516718f4f30`
has ROM SHA-256
`a3ec45dd5c87d6b719ef08b7e4dfb93947624f1c3c7d28ee465af78dd83fc492`.
One watched DE25 smoke boot passed 20 Fonts information roundtrips, restored
production to stage 8/desktop, three units active with zero restarts, and
matched stopped production image and selector before/after; evidence is on
Beast under `/var/tmp/astra-irq-origin-evidence-20261019/smoke/`.

Host IRQ code audit found a shared wait-set race: source 12 handled host-channel
and audio completions inside hard IRQ, waking threads while interrupted kernel
code could be linking or withdrawing registrations. Timer IRQ already defers
scheduler mutation for the same reason. Haiku references reviewed read-only:
`src/system/kernel/interrupts.cpp` holds its per-vector lock while calling
handlers, and `src/system/kernel/events/wait_for_objects.cpp` advances the
notification list before a wake can change it. Astra's native single-CPU
contract now routes host completion through its existing deferred IRQ worker:
hard IRQ masks the level source and queues it; worker ACKs before scanning
waiters, then rearms the source. No second completion or wait mechanism was
added. Source `fbf87fa4d56c31471624e60b44613c5483c193e1` also retains
failure-only thread-wake branch and queue/registration/ready/deadline state in
panic reports if the hypothesis is incomplete. Beast `make test all verify`
passes, including hard-versus-worker host IRQ and deep wake-failure tests;
K1/K10 QEMU qualification passes all 12 markers. Logs are
`/var/tmp/astra-host-irq-deferred-kernel-gates-20261019.log` and
`/var/tmp/astra-host-irq-k1-20261019.log`. Private
DE25 release `6dae3fe550e5e8c7fe43b1d12d91d6c531c31c3253ae058fb51641b0607d7c34`
has ROM SHA-256
`f8bdc9f9d0e3c54e7abf4ab0774f190961365503292c06ae92af4b857e6db70a`.
One preserved-state DE25 boot passed 20 Fonts information roundtrips without
panic, then restored production stage 8/desktop, three services with zero
restarts and identical stopped-image hash/selector. Smoke log and hashes are
on Beast under `/var/tmp/astra-host-irq-evidence-20261019/smoke/`. An eight-hour
stop-on-first-failure private repetition started at 2026-10-19 13:24 UTC as
`astra-host-irq-overnight.service`; results are under the board trial root's
`overnight/`. Do not claim panic resolved or resume production cutover until
the long run clears the prior 23-cycle failure window and the exact release
passes remaining gates.

At 2026-10-19 13:46 UTC, the first private run had passed five cycles and
started cycle six. A second eight-hour run is queued on DE25 as
`astra-host-irq-soak2.service` through the first unit's `OnSuccess=` trigger.
It uses the same immutable private release and preserved-state base image,
but writes separate checksummed cycle evidence under
`/var/tmp/astra-irq-origin-physical-20261019/overnight-second/`. Its start
guard requires the first unit's `OVERNIGHT COMPLETE` journal record; a panic,
failed run, or premature clean stop cannot start it. The first and second
QEMU trials cannot overlap. These runtime-only test units do not alter the
production release selector or storage image. Production cutover remains
paused pending both run results and remaining release gates.

First soak stopped at cycle 75, not its eight-hour deadline. Cycles 1-74
passed; cycle 75 completed all 20 private Fonts information roundtrips with no
captured deferred-worker panic. Production restore then panicked in the older
production release at 2026-10-19 18:23:17 UTC with `syscall dispatch contract
failed`; its QMP socket broke and the harness returned 1. This is a production
guest panic, not evidence of the private candidate's original deferred-IRQ
failure. The `OnSuccess=` guard correctly left the second soak inactive.
Cycle-75 `SHA256SUMS` verifies on board and Beast; stopped production image
hashes and selector match before/after. Evidence is preserved on Beast under
`/var/tmp/astra-host-irq-cycle75-evidence-20261019/`, including production
panic report SHA-256
`11ba456b3b80e1b1fcce18277a95b2956e84f0c4b8e3e0907bf17d4894b81050`
and journal SHA-256
`207a8e2f2f832e3f76f096d4157f1c9dd5cd0c575296b791083f4ba2d22b19ae`.
Production restarted to stage 8; all three services are active, with
`astra.service` NRestarts=1 after that panic. Board Linux also logged repeated
ext4 block-path warnings for inode 112146, identified as
`/var/lib/astra/log/qemu-console.log`; keep this separate storage-health issue
visible. Neither soak is a completed qualification. Do not restart the same
cross-release cycle harness until the production panic and host filesystem
warning are accounted for. Production cutover remains paused.

The repeated Linux ext4 warning is the same inode/logical-block warning
already diagnosed after the offline root repair below: inode 112146 is
`qemu-console.log`, and the requested logical block is one past legacy
triple-indirect range, not an on-disk block reference. It has recurred since
2026-10-18; it does not by itself establish new root corruption. The separate
production `WAIT_ONE` syscall returned `KERNEL_PROCESS_CORRUPT` in the old
release, which predates the current host-IRQ deferral; the exact internal
branch is not present in that release's failure report.

To qualify the candidate without repeatedly booting the old production
kernel, a private-only harness keeps production stopped across candidate
cycles and restores it once on exit. It still hashes the stopped production
image and checks the selector before restore; candidate and production QEMU
cannot overlap. One physical smoke cycle passed 20 Fonts information
roundtrips with no candidate panic; all evidence checksums and before/after
production image/selector comparisons passed; checksummed smoke evidence is
copied to Beast under `/var/tmp/astra-host-irq-private-smoke-evidence-20261019/`.
Production returned to stage 8 and completed desktop with all three services
active and zero restarts. An eight-hour, stop-on-first-failure run started at
2026-10-19 23:53 UTC as
`astra-host-irq-private-soak.service`. Evidence is under board trial root
`overnight-private-only/`; its exit trap must restore production once and
verify the same invariants. Production desktop is intentionally unavailable
during this run. No completed eight-hour qualification or cutover claim yet.
First unattended candidate-only cycle passed with 20 roundtrips, checksummed
evidence, matching stopped production hashes and selector, and no panic.
A second independent eight-hour candidate-only run is staged as
`astra-host-irq-private-soak-second.service`: first unit's `OnSuccess=` starts
it only after a successful production restoration, verified restoration
manifest, and `OVERNIGHT COMPLETE` journal record. It writes separate evidence
under `overnight-private-only-second/`; failures leave it inactive. First run
caps at 128 cycles and may finish before eight hours if cycles are short;
second caps at 256, so its eight-hour deadline controls. Exact
harness scripts and unit files, with SHA-256 manifest, are archived on Beast
under `/var/tmp/astra-host-irq-private-soak-evidence-20261019/`.

The first candidate-only soak stopped at cycle 3 on 2026-10-20 00:04 UTC.
Cycles 1-2 passed 20 Fonts information roundtrips each; cycle 3 booted to
stage 8 and logged the Fonts desktop icon, but the immediate injected double
click did not launch Fonts within 30 seconds. No candidate panic was captured.
The gate's `desktop_icon` observes a layout log emitted before the desktop's
first frame completes; a click before presentation is a plausible test race,
not a proven kernel failure. The private driver now waits for the same
completed-desktop display counters used by the production-restore gate before
clicking. The shared Fonts test reports fresh logs on open failure. A new
physical smoke was run before another long run. Cycle-3 and restoration
SHA256SUMS verify on Beast under
`/var/tmp/astra-host-irq-private-failure-20261020/`. The harness restored
production to stage 8 and completed desktop; selector and stopped production
image hashes matched, and all three host services were active with zero
restarts. The second soak did not start. Production cutover remains PAUSED.

The completed-desktop gate passed a fresh DE25 smoke cycle at 2026-10-20
00:12 UTC: 20 information roundtrips, no panic, cycle and restoration
SHA256SUMS verified on board and Beast, and production returned to stage 8
with unchanged stopped image/selector and all three host services active with
zero restarts. Beast evidence is under
`/var/tmp/astra-host-irq-private-ready-smoke-evidence-20261020/`. A new
candidate-only long run started at 00:13 UTC as
`astra-host-irq-private-ready-full.service`, writing separate evidence under
`overnight-private-ready-full/`. Its `OnSuccess=` chain points to
`astra-host-irq-private-ready-second.service`, which requires the first run's
restoration manifest and `OVERNIGHT COMPLETE` record before starting. The
second writes `overnight-private-ready-full-second/`. Both units are runtime
only; first cap is 128 cycles, second cap is 256, and each has an eight-hour
deadline. The first is running and the second inactive at this checkpoint.
No long-run qualification has completed; production cutover stays PAUSED.

At 2026-10-20 01:18 UTC, the candidate-only DE25 run had passed 21 cycles
of 20 Fonts information roundtrips each and was executing cycle 22. A
separate Beast boot using the release package's AArch64 QEMU executable
failed before stage 8: `/services/posixd` launch returned I/O status 11.
The same failure occurred with both the fresh packaged image and the
checksummed preserved-state base image. Beast is x86-64, so these runs used
Linux binfmt to emulate the AArch64 QEMU executable; neither is valid
candidate-kernel panic evidence. The installed Beast storage provider
matched a fresh build byte-for-byte, the packaged `posixd` matched its
build output and the preserved image's copy, and offline `e2fsck -fn` found
the packaged filesystem clean. The current native x86-64 QEMU build with
the exact candidate ROM and fresh packaged image reached stage 8 and passed
120 seconds with 17 remote-desktop retry events and no panic. Its user unit
was `astra-host-irq-candidate-native-smoke.service`. A 24-hour native Beast
soak of the same ROM and image began at 01:18 UTC as
`astra-host-irq-candidate-native-24h.service`, with output in
`/var/tmp/astra-host-irq-candidate-native-24h.log`. The double-emulated
boot failure's exact I/O branch is still unproven; do not count those runs
as native-host or DE25 acceptance evidence. No long-run conclusion is
available yet.

The physical candidate-only run passed cycle 23 at 2026-10-20 01:23:37 UTC
and entered cycle 24. Its predecessor release first panicked at cycle 23,
Fonts information roundtrip 6. This crosses that specific first-failure
window once; it does not establish a deterministic fix or complete either
long soak. The candidate has not been installed as production.

A focused host regression now stages the exact half-linked wait queue that
`enqueue_wait_registration` exposes before publishing its count, then raises
the host IRQ. Current deferred source leaves that queue untouched in hard
IRQ and services the host completion after the test finishes publication;
`build/test_interrupt` passes on Beast. A separately compiled negative
control using the pre-`fbf87fa4` `interrupt.c` fails at the host dispatch
assertion because its hard IRQ service observes the half-linked queue.
This proves the forbidden reentrancy and verifies the boundary change, but
the old physical panic did not retain enough wait-row state to prove it
occurred at that exact instruction. Haiku read-only reference:
`src/system/kernel/interrupts.cpp` guards vector dispatch with a spinlock;
`src/system/kernel/events/wait_for_objects.cpp` saves the next notifier
before invoking a callback. Astra uses its existing deferred worker instead
of copying Haiku's lock scheme. No release binary changed for this test.

Release `0f3e6309` is REJECTED: fresh DE25 cancellation/retirement/empty-store
phase passes, but the second boot panics before the import workflow completes.
Reason `syscall dispatch contract failed`; trace records syscall 80
(HOST_CHANNEL_WAIT), process status 9 (CORRUPT), next context NULL and runtime
active. Exact failing branch is not established. This is a current-kernel
failure, distinct from the old production deferred-worker report. Do not
accept a clean retry or activate this release. Full panic report SHA-256
`5d31188c9aed07d6eac80b9e44ad640c7517a516f902a3c9e06fa0458dee15e9`
and matching kernel ELF SHA-256
`ef43fec669d3dc7c9a36941274cc890a4a6b68196297870e2a9b83766e82bac1`
are retained on Beast under
`/var/tmp/astra-font-state-final-evidence-20261018/panic/`; complete failed
state archival under `attempt1/` is checksum-verified. Trial log:
`/var/tmp/astra-font-state-final-physical-cycle1-20261018.log`.
Production restored through stage 8 and completed desktop, all three units
active/NRestarts=0; stopped image and selector compare equal before/after.
The planned upgrade backup did not start: its private-process precondition
failed after the panic. Its partial directory contains only an empty preflight
file, not a usable backup. No production data or selector was changed.

Plane ASTRA68-61 tracks this current-kernel blocker under Fonts ASTRA68-36.
Failure-only diagnostics extend existing WAIT_FAILURE records to distinguish
entry capture, host wait, blocked scheduling and pending preemption, including
the raw thread wait result. No scheduler/wait behavior or panic invariant is
changed. Haiku read-only reference reviewed before diagnosis:
`src/system/kernel/condition_variable.cpp`, `ConditionVariableEntry::Wait`
and registration/removal paths; its scheduler-lock-protected condition recheck
and prepare/block ordering are the comparison points, not an assumed cause.
The existing Astra host-channel, wait-registration and scheduling mechanisms
remain the sole implementation. Full failure archive is checksum-verified.

Fault injection independently reproduces the dispatch contract failure:
HOST_CHANNEL_WAIT treats wait-registration allocation exhaustion as CORRUPT
instead of returning OUT_OF_MEMORY. Before-fix failure is retained in
`/var/tmp/astra-host-wait-allocation-red-20261018.log`. Shared native wait error
translation now owns deadline/resource-limit/allocation results for ordinary
handle waits, host channels and audio streams. Audio admission failure also
retires its pending-wait bit. Existing registration allocation now returns
the precise thread status: bad retained capacity or inaccessible successfully
allocated storage remains CORRUPT, never an ordinary resource error. Both
existing callers preserve that result. No new wait mechanism or masked panic.
This consolidates three copies of status policy; existing allocation-failure
test hooks and wait tests are extended, with no new test helper.

Host/audio exhaustion, unchanged running state, cleanup/retry and registration
corruption checks pass with the full kernel host suite in
`/var/tmp/astra-wait-resource-green-20261018.log`. Before changing the shared
wait path, the target-representative K1/K10 gate passes all twelve markers and
existing cycle budgets at 16 ns/instruction in
`/var/tmp/astra-wait-registration-baseline-20261018.log`; baseline ROM, ELF and
disassembly are retained separately. After-change target/budget checks pass
the same twelve markers and cycle budgets in
`/var/tmp/astra-wait-registration-candidate-20261018.log`. Host suite and
K1 target verification pass at pushed source
`3851eaa0c36f9fd5f66402dedd9ee10ccd6cf103`. K1 ROM is diagnostic
qualification output and must never become a release ROM. This resource-error
defect is proven, but its responsibility for the observed physical panic is not
established. Ten fresh diagnostic `b83b2f3b` DE25 boots (boot11–boot20) pass
with stage-8 desktop and malformed-frame probe; evidence under
`/var/tmp/astra-font-state-final-evidence-20261018/diagnostic-cycle2/` is
checksum-verified. An earlier observer stopped on a stale retained panic
report, since runtime-supervisor copies the console to panic-latest only after
QEMU exits. That original report was identity-checked and rotated. Exact
CANCEL→SELECT→REMOVE→LONG reproduction with the unchanged diagnostic ROM
passes all four DE25 boots. It covers cancellation/retirement, Iosevka import,
twenty information-pane roundtrips, persistent choice, uninstall, and long
license scroll/copy. A private QEMU monitor was armed to pause and capture any
new panic before shutdown; none occurred. The complete result is checksum-
verified under
`/var/tmp/astra-font-state-final-evidence-20261018/diagnostic-full/`;
Beast log `/var/tmp/astra-host-wait-full-physical-20261018.log` reports PASS.
Production was restored to stage 8 with completed desktop and all three units
active/NRestarts=0; selector and stopped image compare equal. This clean retry
does not identify the cause of the original panic or qualify a release.
The exact rejected `44694886` ROM then passed the same four-phase DE25 retry,
including all twenty information rounds, with panic capture armed. Its complete
retry is checksum-verified under
`/var/tmp/astra-font-state-final-evidence-20261018/original-retry/`;
Beast log `/var/tmp/astra-original-full-physical-20261018.log` reports PASS.
Production again restored with stage-8 desktop, three active/NRestarts=0 units,
and unchanged selector and stopped image. The first failing attempt remains
authoritative proof of intermittent `HOST_CHANNEL_WAIT` corruption; two clean
four-phase retries do not clear it. Precise failing branch remains unknown.

The independently proven native wait-resource fix is privately published
from `26f9902c0d1331561060f807d61ea6e87298c2c6` as DE25 release
`bf8445bbba949182c40ef8811e21f4ac169d9bde1813a6b7a4fa0cf6ba33e682`.
ROM SHA-256 is `50e7ed5f0a3e213959bc7520428ec9c4146cd5674ba5e9d871d57865999e14d5`;
source manifest SHA-256 is
`1f259c0bf58cf9642163b101e86400b0fc872793ae02040541198f1912714875`.
Both build headers name that full source SHA and UTC
`2026-10-19T00:03:31Z`; kernel is release mode with panic, soak, and K1
qualification selftests disabled. The complete pinned DE25 Linux kernel
package remains SHA-verified. Publisher log:
`/var/tmp/astra-font-wait-resource-publish-pinned-20261018.log`. A prior
publisher invocation failed before package creation because it lacked the
existing pinned `FLUIDSYNTH_ROOT=/home/barry/Git/fluidsynth`; successful clean
publisher used the clean v2.6.1 pinned commit.

Two complete fresh four-boot physical cycles of that exact package pass
cancel/retirement, Iosevka import, twenty information-pane roundtrips, live
system-face adoption and persistence, built-in restoration and uninstall,
and long-license scroll/copy/uninstall. Each boot reached stage 8, passed the
one-shot malformed-frame probe, and completed hardware display submissions.
Both cycles restored production to stage 8 with completed desktop, all three
units active/NRestarts=0, and unchanged selector and stopped image hash.
Both archives under `/var/tmp/astra-font-wait-resource-evidence-20261018/`
(`attempt1/` and `attempt2/`) are checksum-verified. No candidate panic was
observed. Exact candidate ROM and storage image also pass a 600-second Beast
QEMU panic soak with 65 remote retry failures and clean exit; log:
`/var/tmp/astra-font-wait-resource-panic-soak-600-corrected-20261018.log`.
The original panic's internal branch remains unidentified. Subsequent
preserved-state physical qualification exposed the deferred-IRQ panic above;
`bf8445bb` is REJECTED for production cutover.

Earlier focus-edge-corrected source `446948863f72b31af2b62dc04ab71805a4ba5357`
passes Display host/sanitizer/MC68040/analyzer gates in
`/var/tmp/astra-font-state-focus-edge-green-20261018.log`. Clean publisher
passes and verifies private release
`0f3e63093c512bc71cfcea00ebfe462d6649a998578ea3554b3ec51f0f2257eb`, ROM
`20b80b29b649f278ece1fcde07a7949593a398aabdcbbf0824ba697bf90ed329`, source
manifest `3d83c4cddd69688295b8aea844457bba30dc9235f4b1dfa980cc8d5050b727ca`.
Both revision headers match, nonzero identity `44694886`, build UTC
2026-10-18T22:11:08Z; kernel release mode, qualification/self-tests disabled.
Publisher log `/var/tmp/astra-font-state-final-release-20261018.log`.
Exact private four-boot DE25 staging rejected on the second boot; physical
acceptance and data-preserving production cutover remain open. Superseded `f885a4c1` was
never booted or activated and its disposable board staging was retired under
selector/service/process guards. Immutable package remains on Beast.

Window-state backpressure fix `05b85dd6` passes the native end-to-end Fonts
gate and all twenty consecutive DE25 information/scroll/select/copy/Back
roundtrips on the previously failing retained state. One-shot malformed-frame
probe launch count is exactly one. Controlled shutdown and settled production
restoration pass: stage 8, completed desktop, all three services active,
NRestarts=0, selector and stopped production image unchanged. Logs:
`/var/tmp/astra-font-state-notice-{native,physical}-20261018.log`.
Complete regression evidence is checksum-verified on Beast under
`/var/tmp/astra-font-settings-evidence-20261018/state-fixed/`, alongside both
failed attempts. Only the archived disposable board directory is retired.

Clean publisher from exact source
`05b85dd6ffc80ce85082e6c0e29add7b0da3f65f` passes and verifies as private
release `f885a4c137e7248aae705a0a009e8a764aaa4defea2e36d1dabb05e8c79d80f7`,
ROM `1db09fd7e4b207477f4ea6e01567949114c28f9f129328e89483c0019a5a51e6`,
source manifest `f3aa224db4ba08e557765e0a115d084fd5d988240aef3630ee19ca92e09c2950`.
Both revision headers match, nonzero build identity `05b85dd6`. Publisher log
`/var/tmp/astra-font-state-release-20261018.log`. Fresh exact-release DE25
cancellation/import/twenty-cycle/saved-choice/removal/long-notice gates and
data-preserving production cutover remain open. The passing retained-state
overlay is regression evidence, not acceptance of this new immutable release.

Additional focus-edge regression rejects coalescing a blocked deactivation
and subsequent reactivation into an ordinary active-state snapshot: an armed
button could otherwise survive the intervening focus loss. Display preserves
the existing loss/reset marker only when the retained snapshot is inactive;
ordinary active-state backpressure still preserves focus. The edge fails in
`/var/tmp/astra-font-state-focus-edge-red-20261018.log`. `f885a4c1` is superseded
before physical release gates or activation; rebuild includes this correction.

Latest exact-release DE25 gate is FAILED, not accepted: source `a55c6395`,
private release `c392a4f17a2c81096fbc5145b13d53af99c11408424d38bb966808767b20aeb5`.
Cancellation, converter retirement, empty inventory, Iosevka import and
preview, and one full information/scroll/select/copy/Back workflow pass.
The first of twenty additional information roundtrips times out. Failure
HDMI shows the details pane with focus cleared, no information-read error;
glyph commands 456, submissions/completions 153/153. Cause remains unknown.
Diagnostic-only Display logging now distinguishes failed window event delivery
from upstream input-state reset. Do not infer a font-service failure or accept
an unreproduced retry as a fix. Complete failed state, scripts, trace and RGB
evidence are checksum-verified on Beast under
`/var/tmp/astra-font-settings-evidence-20261018/attempt1/`; gate log is
`/var/tmp/astra-font-settings-physical-cycle1-20261018.log`.
Production selector remains `57078599`; stopped storage before/after SHA-256
is `88b54608daac800b21ea40e30b9863d2670e346c84ab8927cc40591e041e322d`.
Restoration waits for guest stage 8 and completed desktop; all three services
restore active with zero automatic restarts. Historical panic evidence below
remains retained. Shared input diagnosis and final Fonts release remain open.

Retained-state DE25 diagnosis reproduces after two successful repeated
information cycles. At 21:34:32.836 UTC, Display records window 3, event type 4
(STATE), sequence 173, status 6 (WOULD_BLOCK). No upstream input reset is
recorded. Its generic failure path marked input lost; Interface Kit then
cleared focus/armed interaction on the next event. Diagnostic failure state
is retained separately under `display-diagnostic/` in the same Beast evidence
root. Production again restores through completed stage 8 with all three
services active, zero restarts.

Display now retains the latest window-state snapshot on WOULD_BLOCK and
retries in its existing per-window vblank loop, like retained close/menu and
font notifications. It reconstructs current geometry, flags, generation and
z-order; unsubscribing retires the notice. It does not declare keyboard input
lost for a state snapshot. Actual lost-input handling is unchanged. Focused
regression fails before the fix in
`/var/tmp/astra-font-state-notice-red-20261018.log`; host, sanitizer, target
and analyzer checks pass in
`/var/tmp/astra-font-state-notice-green-20261018.log`. Physical requalification
is pending. No new queue, thread, ABI or client workaround.

Haiku review for this mechanism: `src/servers/app/EventDispatcher.cpp`
`_SendMessage` and `_AddTokens`, and
`src/servers/app/ServerWindow.cpp` `SendMessageToClient`/notification methods.
Adopt distinct window notifications and coalescible latest-state treatment;
adapt to Astra's existing bounded ports and retained-notification loop.
Haiku's unimplemented failed-message queue TODO is not copied. Existing Astra
keyboard retention and genuine input-reset semantics remain authoritative.

Post-trial observation found one automatic restart of selected production
`57078599`: its October 16 ROM panicked during the first restoration boot at
20:08:38 UTC, before the warm HDMI capture. Reason is `deferred worker service
failed`; the old ROM has no branch-level worker diagnostic. Original report
and journal are retained on Beast under
`/var/tmp/astra-font-import-evidence-20261018/production-restart/`; report
SHA-256 is `42be9e55fa8ef8ff15cdbfd05f84b0c9966a7988ec7894f4431f87d83950c576`.
The private font candidate did not panic. Production is active with runtime
NRestarts=1, other two services zero; selector and test-time stopped storage
identity remain unchanged. Cleanup refused its zero-restart guard and deleted
nothing. The first restoration check below was an immediate snapshot, not a
boot-settled stability result. Future trial restoration must wait for stage 8
and check the final restart count before reporting success.

The fixed kernel's earlier 24-hour Beast soak has now completed successfully:
`ASTRA PANIC SOAK PASS seconds=86400 remote-failures=8613` in
`/var/tmp/astra-panic-fixed-24h.log`; user-unit Result=success, ExecMainStatus=0.
Current kernel implementation is still the `5730bb4b` reaper fix and diagnostic
checkpoint. The old production panic lacks enough evidence to tie it to that
reaper defect. Preserve the report and qualify the final candidate; do not
claim that historical cause is established or that old production is stable.

Interface Kit 7.2 now supplies Fonts' shared Settings shell: two existing
Labels and two Containers, common spacing, growing content and stacked
actions. Ordinary controls retain layout, focus, input and hardware drawing.
There is no new painter, event loop or settings backend. Focused Interface/
Fonts tests, sanitizers, analyzers, MC68040 library/export checks and NDK
header/documentation checks pass in
`/var/tmp/astra-settings-pane-checks-20261018.log`. Coherent userspace passes in
`/var/tmp/astra-settings-pane-coherent-sdk-20261018.log`; the corrected packaged
SDK, standalone links and every installed example pass in
`/var/tmp/astra-settings-pane-sdk-final-20261018.log`. Final exact-release
DE25 gates remain in progress. The first native UI run used
Beast's older installed converter 0.2: UI/cancellation/three information cycles
and live choice passed, then the required 0.3 provenance check rejected it.
Retry explicitly selects the current built 0.3 worker; DE25 release packaging
already uses that version. The captured first-run output is retained as a
transcription in `/var/tmp/astra-settings-pane-old-worker-failure.txt`; the
original per-case filename was reused by the retry. No product fallback.

With the current worker, all four native shared-shell cases pass in
`/var/tmp/astra-settings-pane-{iosevka,long-notice,unsupported,long-style}-native-20261018.log`:
cancel/retirement/retry, family/style/size preview, three consecutive
information/scroll/select/copy/Back cycles, live Desktop/Fonts choice,
persisted restart, built-in restoration and removal. The Iosevka case verifies
its original source hash and all six complete embedded notices. The shared
shell's software gates pass; exact final DE25 qualification remains open.

The import-session candidate below passes both paced and original-burst
information workflows after restart, then a fresh four-boot DE25 cycle:
cancellation/retirement/empty inventory, Iosevka import/information/copy/live
choice, persisted restart/built-in restoration/uninstall, and the long-notice
Text View/copy/removal case. Log:
`/var/tmp/astra-font-import-physical-cycle2-20261018.log`; complete evidence is
checksum-verified on Beast under
`/var/tmp/astra-font-import-evidence-20261018/cycle2/`. Production storage
before/after SHA-256 remains
`a5726fa33cc14faf0f7ff12879d1b674349f87aa83287c77cf24108a932aac14`;
selector stays `57078599`, all three services active, zero automatic restarts.
Warm production matches known-good HDMI SHA-256
`37d1bbfe3df5fbb5b0c3a8d6b278724f1222aefd572bd6e52cd3aa1534b3b091`,
25 glyph commands and 49/49 submissions/completions; verified evidence is in
`warm-production/` under the same Beast root. The initial timeout is not
reproduced and has no established root cause. Keep its failure evidence and
retain failure capture plus repeated information workflows in final gates.

Font import session source `a30a8d2dc8a95da33ea3f38dfeae71c4c2f0dce1`
clean-builds as private release
`f59511300027b5f546e49fbfcd513c334265e65cf1e501b074f614026ee9e96f`.
The first DE25 attempt passes import progress, cancellation, converter
retirement, empty-store checks, Iosevka import and independent-size preview.
Opening source/license information then times out; no panic is recorded.
This is an unresolved physical gate, not release acceptance. The trial log is
`/var/tmp/astra-font-import-physical-cycle1-20261018.log`; failed storage,
logs, trace and HDMI frames are retained on Beast under
`/var/tmp/astra-font-import-evidence-20261018/attempt1/`.
Production selector remains `57078599`; stopped production storage before
and after remains SHA-256
`fa74893ab2579a0f3bab7de83c68b7e7d729e735422f080575a0b12b08b440b4`.
All three production services restore active with zero automatic restarts.
A separate disposable harness predicate falsely rejected blank separators in
systemctl output; it now checks each of the three recorded service states.
Information-pane diagnosis, shared Settings shell and overall release gates
remain open. Candidate is not activated.

Native import sessions are integrated coherently as Font Kit 5.0 / FONTS
protocol 8. The blocking import API is removed from all in-tree callers and
exports. Session capabilities expose state, staged-face progress, cancellation,
wait hints and explicit release; client death abandons through the existing
port peer-death mechanism. Terminal cancellation follows converter retirement
and rollback. The Fonts UI composes existing Progress/Button controls and waits
on window and import notifications together. A client-owned bounded notification
port makes service death observable, unlike an event with surviving wait owners.
Service/UI tests, sanitizers and MC68040 analyzers pass in
`/var/tmp/astra-font-import-ports-{service,ui}-final-20261018.log`; Font Kit's final
checks pass in `/var/tmp/astra-font-import-empty-port-green-20261018.log`.
Coherent userspace and probe builds pass in
`/var/tmp/astra-font-import-ports-{coherent,probes}-fixed-20261018.log`. NDK standalone
C/C++ headers, documentation coverage and HTML/PDF checks pass in
`/var/tmp/astra-font-import-ports-ndk-20261018.log`.
Packaged NDK archive integrity, standalone linking and every installed example
also pass in `/var/tmp/astra-font-import-ports-sdk-20261018.log`.

Native admission exposed an ownership cleanup bug: after moving both notification
endpoints, the importer closed the empty port and misreported INVALID_HANDLE.
The real port contract reproduces this in the focused host regression
`/var/tmp/astra-font-import-empty-port-red-20261018.log`; cleanup now closes only
still-owned endpoints. The original native failure is retained in
`/var/tmp/astra-font-import-ports-cancel-native-20261018b.log`.
Native cancellation/retirement/empty-store/restart, catalog reads during pending
conversion, valid multi-face publication/restart and failed-collection rollback
now pass in `/var/tmp/astra-font-import-ports-{cancel,concurrent,collection,rollback}-native-20261018.log`.
The concurrent gate uses direct session admission; its older thread/invalid-source
contender could acquire the sole retained session before the intended actor.
AFNT import/idempotence/selection/uninstall/restart/damaged-inventory fallback
also passes in `/var/tmp/astra-font-import-ports-afnt-native-20261018.log`.
The native UI passes progress, Cancel, converter retirement, same-source retry,
import, source/license scrolling/selection/copy, live Desktop/Fonts adoption,
saved restart, built-in restoration and uninstall in
`/var/tmp/astra-font-import-ports-ui-native-20261018.log`. The saved original
source hash and six complete notices are verified. Cancellation preserves the
source browser's selected row and viewport. All native session/software gates
pass; broader shared Settings shell and DE25 release acceptance remain open.
No new DE25 release claim or production activation.

Native font import coordination now runs in the Fonts service's existing
wait loop. The converter still uses the existing native worker thread and
ready/resume semaphores; the coordinator owns a private source/name snapshot,
stages faces through the existing store, and publishes through Config Kit only
after successful worker retirement. Catalog/metadata/render reads and system
selection remain available; competing inventory writers receive BUSY. The
coordinator checkpoint `92706287` retained Font Kit 4.2 / FONTS 7; the session
integration above replaces that contract and supplies native UI progress/cancel.

Focused coordinator ownership/publication/rollback tests, service regression
tests, sanitizers and MC68040 analyzers pass in
`/var/tmp/astra-font-import-coordinator-final-20261018b.log`. Ambiguous inventory
commit failures retain objects and disable further inventory writes and new
user-font choices until service reload; built-in recovery selection remains
available. This prevents a stale cached manifest from overwriting a commit
whose reply was lost.

Native AFNT import/idempotence/selection/uninstall/restart/damaged-inventory
fallback, failed-collection rollback with an empty object store, and valid
multi-face collection publication/restart pass in
`/var/tmp/astra-font-import-coordinator-{afnt,rollback,collection}-native-20261018.log`.
The final concurrent catalog/import and restart gate passes in
`/var/tmp/astra-font-import-coordinator-concurrent-final-native-20261018.log`.
The same gate rejects the archived blocking Fonts
service in `/var/tmp/astra-font-import-blocking-native-red-20261018.log`.
Initial probe failures exposed test assumptions: zero-deadline native thread
waits return TIMED_OUT, and an elevated test parent can starve its normal-priority
actor. The corrected probe uses ordinary application priority and a delayed
real converter. No new kernel or host-converter mechanism is introduced.

Font information source `f4fb7263bea50dea60ec620c99774c3830e8842f` clean-builds
as private release
`ecd9020a954d0b9f7d5ca935f5c109f30b74c73ae6408922555078a55d941b83`, ROM
`47adcafd98486fffbb4e508e0182f1a1ea43801620629a13df302b2229a09c30`, source
manifest `daa9045b1a61b5998cdcf455093901054c5cbf7ced853133de6e44ad536b7fc4`.
Both revision headers match. Publisher passes in
`/var/tmp/astra-font-information-release-20261018b.log`; the first attempt
stopped on the mirror's missing default FluidSynth path. The retry explicitly
uses the existing pinned `FLUIDSYNTH_ROOT=/home/barry/Git/fluidsynth` checkout.

The first complete DE25 cycle passes three boots: native malformed
geometry/header/opcode refusal and repair on every boot; Iosevka import,
source/license scrolling and copy, live selection, persisted restart,
built-in restoration and uninstall; then the formerly panicking long-license
information/copy/Back/removal workflow. Inspected HDMI frames show clipped
text/selection, original-source hash, readable license URL, explicit raw-byte
fallback and visible Back action. Complete trial evidence is checksum-verified
on Beast under `/var/tmp/astra-font-information-evidence-20261018/cycle1/`;
trial log is `/var/tmp/astra-font-information-physical-cycle1-20261018.log`.
Stopped production storage remains byte-identical, SHA-256
`1cb708822a46a1e84e80aad8a0a44c665004d00ef60948a2cac649f78882bbd2`.
Production selector stays `57078599`; runtime/audio/remote services restore
active with zero automatic restarts. A second fresh-storage cycle also passes,
with stopped production SHA-256
`d15f2e21ce25036772f63d813690afe7f9253c85d83ebda564ca5017b2c5036a`.
Its evidence and warm-production capture are checksum-verified under `cycle2/`
and `warm-production/` in the same Beast root. Restored production matches
known-good HDMI SHA-256
`37d1bbfe3df5fbb5b0c3a8d6b278724f1222aefd572bd6e52cd3aa1534b3b091`,
25 glyph commands and 49/49 submissions/completions.

Fourteen of fifteen corresponding candidate HDMI frames are byte-identical.
The remaining frame shows a new frame-probe window: the disposable harness
incorrectly marked this successful finite probe `required`, which correctly
makes Supervisor restart it forever. Archived syslog confirms repeated new
probe PIDs and successful launches, not resurrection of a closed window.
The test now uses existing `restart=never` policy and checks exactly one launch;
the native gate passes in
`/var/tmp/astra-font-frame-refusal-one-shot-native-20261018.log`. The physical
harness isolates and retains each boot's syslog. Corrected qualification passes
all three boots, with exactly one probe launch per boot, in
`/var/tmp/astra-font-information-physical-one-shot-20261018.log`.
All fifteen HDMI frames are byte-identical to the clean second-cycle frames.
Corrected trial and final warm-production evidence are checksum-verified under
`one-shot/` and `warm-one-shot-production/` in the same Beast root. Stopped
production storage SHA-256 remains
`4b8810950b8c5a0237dd70e554ba7e6cf6784f7f46f0e4fc3375260886299906`;
warm production again matches its known-good HDMI frame, with active services
and zero automatic restarts. No Display teardown change is warranted by this
observation. Source/license information and rejected-frame correctness gates
pass. Candidate is not activated; broader
Fonts Settings shell/import workflow and release acceptance remain open.

Font information integration is implemented: Font Kit 4.2, FONTS protocol 7,
read-only NAME-only snapshots, shared counted JSON validation and Fonts' native
Source and license Text View with selection/copy/Back. Coherent userspace/ROM
and packaged NDK checks pass in
`/var/tmp/astra-font-information-coherent-final-20261018.log`. Native Iosevka import,
information/copy, live selection, restart, restoration and uninstall pass in
`/var/tmp/astra-font-information-iosevka-final-20261018.log`.

The larger legal-text gate exposed a real panic, reproduced in
`/var/tmp/astra-font-information-long-notice-20261018{b,c}.log`:
`critical service display exited: status 36`. Retained fatal diagnostics identify
a rejected draw list (status 1, builder failure 0). Surface fill queued the full
virtual document bounds, exceeding draw-command geometry limits despite a small
viewport. A representative host replay now fails before the fix in
`/var/tmp/astra-font-information-render-red-20261018b.log`; shared Surface fill
now clips before hardware submission using the same bounds calculation as its
software oracle. The document is not truncated. The host/sanitizer gate passes
in `/var/tmp/astra-font-information-render-green-20261018.log`.

Display also wrongly treated malformed client content as a fatal service error.
Its existing per-frame refusal path now discards the unrenderable update,
retains the committed front bank and schedules remaining damage for composition.
Host checks prove other-window progress and repair/retry; original failure is
`/var/tmp/astra-font-frame-containment-red-20261018.log`, focused final checks
pass in `/var/tmp/astra-font-frame-containment-final-20261018.log`.
The final resource-capacity checks also cover RGB frames, preserving committed
content and other-window progress when a client exhausts the batch; tests,
sanitizers and analyzers pass in
`/var/tmp/astra-font-frame-capacity-final-20261018.log`. The formerly panicking
long-document workflow now passes in
`/var/tmp/astra-font-information-long-notice-final-20261018.log`. A native
client submits malformed geometry, headers and opcodes, then repairs each
frame; Display refusal, recovery and Fonts readiness pass in
`/var/tmp/astra-font-frame-refusal-native-20261018.log`. Unsupported-preview and
long-style native gates pass in
`/var/tmp/astra-font-information-{unsupported-preview,long-style}-final-20261018.log`.
Font-information component gates now pass, including corrected one-shot DE25
qualification above. Remaining Settings shell, responsive import/progress/
cancellation and overall Fonts release acceptance are the active work. No
overall release certification is claimed. Production remains `57078599`, with user data and
board selector unchanged.

Font-information groundwork passes its software gates. Interface Kit 7.1 adds
shared read-only Text View from TextModel, TextSurface and ScrollView, with a
native Interface Gallery example. Existing control/context layouts stay
unchanged. TextSurface now owns exact command/payload accounting as well as
rendering: the 30-line, 80-column regression fails before this fix in
`/var/tmp/astra-text-view-capacity-red-20261018.log`. Focused control, sanitizer,
analyzer, MC68040 library/export, Fonts layout and Display checks pass in
`/var/tmp/astra-text-view-capacity-green-20261018.log`. The larger native
420-pixel viewport, scrolling, hardware glyph submissions and exact multiline
UTF-8 clipboard bytes pass in
`/var/tmp/astra-text-view-capacity-native-final-20261018.log`; packaged NDK
documentation and examples pass in
`/var/tmp/astra-text-view-capacity-ndk-final-20261018.log`.

Worker metadata 0.3 retains complete embedded copyright/license/URL records,
encoding/language IDs and language tags; unsupported or malformed encodings
retain exact source bytes. Long-notice, sanitizer, host-channel and AArch64
gates pass in `/var/tmp/astra-font-notices-final-20261018d.log`. The shared
AFNT metadata bound is explicitly 1 MiB, with exact-bound/one-byte-over checks.
Native Iosevka import persists its original source hash and six complete
notices; live switching, restart, restoration and uninstall pass in
`/var/tmp/astra-font-notices-native-ui-final-20261018.log`.

Plane ASTRA-56/57 track these components. Native qualification also exposed
separate Terminal paste and native/POSIX stdout-redirection defects, retained
as ASTRA-58/59 with original failure logs. Font Kit metadata query and Fonts
information pane are implemented under ASTRA-36; ASTRA-60 tracks the Display
panic and rejected-frame containment. Overall Fonts qualification and release
acceptance remain open. Production stays
`57078599`, untouched. No candidate activation or user-data replacement.

Corrected preview source `616129a2de9e9d9aebffcb90aeeea4e9a80e18c4`
clean-builds as release
`c06c1a344ee0e0d100ff6e18f7ab41ee767efc6cd3cf60fd6b5a79c69d2da18e`, ROM
`d48a988f65445011b145502a9cb28f4f784bb7b43a8362214f2f485b44c52529`, source
manifest `3454c61d1ffe6cff99a36f5091795bf3c08fb995333e96fb08d9ed06d1e5b681`.
All final native UI gates pass, including fresh completion notices, pointer
removal, long styles and unsupported previews; logs are
`/var/tmp/astra-font-preview-final-{readiness,iosevka,unsupported,long-style}-20261017.log`.
The first corrected DE25 cycle also passes import, selection, restart,
restoration and removal. Inspected HDMI captures show independent Iosevka
11/13-pixel previews while system text retains its default face, all action
buttons inside the window, live changes across Desktop/menu/Fonts, restored
built-in pixels, and the removed family absent. Complete evidence is
checksum-compared on Beast under
`/var/tmp/astra-font-preview-evidence-20261017-layout/cycle1/`.
The stopped production image stays byte-identical across this trial, SHA-256
`96fcad0293f7994584fe19cd9e6a6a4d4f6265a9ba67934483b2d063630163c4`;
services restore active with zero automatic restarts. The second complete
cycle also passes, with evidence under `cycle2/` in the same Beast root and
production image before/after SHA-256
`b18658e0174cd11220d96c779089a69d6fbdf38d471a0bcc622d94e6ccb39e4e`.
All eight corresponding HDMI captures from the two cycles are byte-identical.
After the second cycle, fully started production again matches known-good
HDMI SHA-256 `37d1bbfe3df5fbb5b0c3a8d6b278724f1222aefd572bd6e52cd3aa1534b3b091`,
25 glyph commands and 49/49 submissions/completions; evidence is under
`warm-production/`. Runtime/audio/remote services remain active with zero
automatic restarts. Shared Label previews, action visibility and repeated
restoration/removal now pass their correctness gates. Remaining Fonts program
and release work stays open; production remains `57078599`, with no candidate
activation or user-data replacement.

The `4a0925b7` shared Label candidate clean-builds and verifies as release
`77115da14f297d0442784a5a62df31f4067531bfa8d3776c97e39af2a241a072`, ROM
`166fda32c1539fecb3a440aa763338f20abc17b204f6acae6a3810499f67d9fd`, source
manifest `67fbecfaed5a639ea3712c2ef043b4fae51eb3a4114aa2f3fe6c6e521cf53c6b`.
Its first DE25 workflow passes import, live switching, restart, restoration and
uninstall, but visual acceptance is rejected: bottom action buttons extend
beyond the default window. The supposed pre-selection preview captures also
preceded detail completion and still show families; they do not prove preview
size changes. Complete failed evidence is checksum-compared on Beast at
`/var/tmp/astra-font-preview-evidence-20261017/cycle1/`.
The stopped production image stays byte-identical across the trial, SHA-256
`c31dcb28e0799276ec614791897d42c55adecee0726f0c6d7840df69e450c214`;
runtime/audio/remote services restore active with zero automatic restarts.
The release remains withheld. Fonts now gives its existing ScrollView a
minimum intrinsic height plus flex growth rather than an oversized fixed
intrinsic height. A real controller/common-control regression fails before
the fix in `/var/tmp/astra-font-preview-layout-red-20261017.log`; final host,
sanitizer, MC68040 analyzer and readiness checks pass in
`/var/tmp/astra-font-preview-layout-final-focused-20261017.log`. Initial
corrected native Iosevka, long-style and unavailable-preview gates pass.
Native gates now wait for successful detail/preview/confirmation/removal
presentation and exercise pointer removal inside the window. Final native and
corrected physical gates remain pending; production remains `57078599`.

The Fonts preview checkpoint uses shared Label, not an application
renderer: Interface Kit 7, Font Kit 4.1, GUI 25. Detail selection carries the
catalog face and strike together and updates the common Label's explicit font
in place. Display measures through the existing AFNT registry/render loop;
bounded UTF-8 chunks retain fractional advance, and actual line height prevents
descender clipping. Ordinary Labels retain fixed nominal theme sizes. Exact
font-ID prefix accounting also closes the common control/menu capacity gap.
Focused host/sanitizer/MC68040/analyzer checks pass on Beast in
`/var/tmp/astra-font-label-preview-focused-final-20261017.log`. The first metrics
draft passes native Amiga import, measurement, hardware drawing and restart.
The first full userspace/ROM/NDK distribution gate passes in
`/var/tmp/astra-font-label-preview-coherent-20261017.log`. A matching cause for
the failed restoration interaction is now reproduced: a font notice that could
retry after queue backpressure incorrectly marked input as lost. Common
controls then reset focus and Return did not activate the intended button.
The real-control regression fails before the fix in
`/var/tmp/astra-font-notice-focus-red-linked-20261017.log`. Delivery now reuses
the existing non-loss retry path; genuine earlier input loss still propagates.
Final focused/coherent checks pass in
`/var/tmp/astra-font-notice-focus-green-20261017.log`, including the full
userspace/ROM and NDK distribution checks. Native Iosevka, Amiga and long-style
gates pass in `/var/tmp/astra-font-label-preview-{iosevka-ui,amiga-native,long-style-ui}-20261017.log`.
A valid AFNT with an unsupported preview bitmap format exposed an application
exit that blocked removal; its native failure is retained in
`/var/tmp/astra-font-preview-unsupported-red-20261017.log`. Fonts now uses the
same Label update path to show an explicit unavailable caption while keeping
management usable. The final native check passes in
`/var/tmp/astra-font-preview-unsupported-final-20261017.log`; its first fixed
run removed the font but exposed an absent-versus-empty inventory assertion
in the test, corrected in the final run. Physical preview/
restoration gates remain pending. Production remains on `57078599`,
with its data unchanged; no preview software has been deployed to DE25.

The `31b709ec` Fonts candidate passed native adoption/persistence checks but
failed physical live-pixel acceptance. Release
`0824c4104c68a0f870cb1f67f1df09c71b1722a6b6b5474ccd3dced04578de84`
(ROM `05bc7fa465f8c7d010ee741a882755354cf00044d5770df7b8ce45fe70014ebd`,
source manifest `e3fe4d7047c33c8d1f9ae14daf874efa5f7213949ae9de32e0935885ceb53dc1`)
remains withheld. Captures show Iosevka in Fonts immediately, but Desktop/menu
text changes only after reboot. Desktop paint lacked presentation; Display
failed to invalidate its system-menu cache. Both paths now use their existing
presentation/invalidation mechanisms. Host/sanitizer/native builds and analyzers
pass. The corrected native GUI gate passes Iosevka import, live adoption,
persisted choice, restart and uninstall in
`/var/tmp/astra-font-live-present-ui-20261017.log`; physical retry remains pending.
Evidence is retained on Beast under
`/var/tmp/astra-font-live-evidence-20261017/`. The disposable trial imported
Iosevka, selected and persisted it, restored the built-in face and uninstalled
the imported object without a panic or retained provider. Production selector
`57078599`, fabric and user image were preserved: stopped image hashes before
and after both equal
`beed144787492f7194927676f706d980a29837e387d36ca76cc429195d78c6dd`.
Runtime/audio/remote services are restored active with zero automatic restarts.
The import gate now waits for a fresh UI completion marker; its earlier fixed
delay captured the source browser before conversion finished. Shared Label
preview extension is agreed but still pending, along with designed-style
resolution and the remaining Fonts acceptance work.

The corrected `c6cd2e7a95c2a631479adb3d6151d6ae67de5350` clean release
`927399cff10d009880914c5326b526afce465289b253b5a6fdc0721f8586781b`
verifies, with ROM
`c738b06c3db27c4376ed6830556308b334821f5e21bc1a56f35669c23f019406`
and source manifest
`5a8a2ec8ca847de2cae2b9678cecb66e6c27e424938ec8b52f53c9f1e1b577fa`.
Both ROM/kernel revision headers identify that commit. It has not been
activated. Physical captures now show immediate Iosevka changes in Desktop,
menu bar and Fonts. Ineligible Astra Mono rows show the explicit label,
requirements hint and disabled system-selection button. Restart restores
Iosevka. The first built-in restoration interaction timed out with no new
adoption records; disk inspection confirmed the choice remained Iosevka. A
diagnostic replay, with only failure-capture handling added to the driver,
passes restoration and uninstall. Captures show built-in text restored across
Desktop/menu/Fonts, inventory is empty, the imported AFNT object is absent,
and the trial provider retires. The first failure remains unexplained: a
passing replay does not close interaction reliability or release acceptance.
Beast evidence is `/var/tmp/astra-font-live-evidence-20261017b/`, including
`restore-attempt1/`; logs are
`/var/tmp/astra-font-live-physical-trial-20261017b.log` and
`/var/tmp/astra-font-live-physical-restore-retry-20261017b.log`.
The successful replay preserves the stopped production-image SHA-256
`3850f7eab69b430c5efced226e5e1beb73789934c57f2b3414b0e0e235aedc38`
before/after; runtime/audio/remote services restore active with zero restarts.
The fully started production HDMI frame after this replay again matches
`37d1bbfe3df5fbb5b0c3a8d6b278724f1222aefd572bd6e52cd3aa1534b3b091`;
capture and counters are retained in the new evidence root's `warm-production/`.
Fully started
production after the earlier trial again matches the known-good HDMI RGB hash
`37d1bbfe3df5fbb5b0c3a8d6b278724f1222aefd572bd6e52cd3aa1534b3b091`
at 49/49 submissions/completions and 25 glyph commands. Evidence is under
`/var/tmp/astra-font-live-evidence-20261017/warm-production/` on Beast.
The old trial's data was archived and checksum-compared on Beast at
`/var/tmp/astra-font-live-archive-20261017/`; its original immutable release
also verifies independently on Beast and DE25. Only redundant disposable board
images and the old trial release were removed, restoring 1.8 GiB of headroom.
Logs and captures remain on DE25; production data was not removed.

The blend-isolation FPGA shell is now installed on DE25: RBF
`9a5531c3f98c0cb384fa35c5007574f85c4c9e15e3c72d0572f0d494ed8cadb3`.
U-Boot reconfiguration and exact-SOF JTAG verification pass. The existing
`6.12.11-astra-storage1-gd7d192a9ddd9` kernel/package `ab511cc1`, versioned boot
script, and software selector `57078599` remain selected. The first fresh
production desktop capture is byte-identical to the previously qualified good
frame (`37d1bbfe3df5fbb5b0c3a8d6b278724f1222aefd572bd6e52cd3aa1534b3b091`),
with 49/49 submissions/completions. Production storage was backed up and
compared before and after fabric installation while stopped; no user image was
replaced. Runtime/audio/remote services are active with zero automatic restarts
after reboot. Following a native Fonts removal workflow, the fully started
production desktop again matches that frame byte for byte (49/49 commands).
An earlier warm capture taken at only 2/2 commands showed the initial empty
workspace; it was rejected as startup evidence. No complete font-release
certification is claimed.

The 2026-10-17 native Fonts physical candidate remains withheld. Beast clean-built
immutable release
`394c118927c981dddadf77533b43b606928779a6e53a56a9928b6dce21305e42`
from source manifest
`a12218e4d367e3440eb308afef9424e8110b0c685926c29096390888bf7b55e5`;
ROM SHA-256 is
`c9f02cea1edc6aa44bfa0c26aa528a6f8f898ad75a36e9e96c2bfd3745187585`.
An isolated DE25 image imported a TTF and persisted its selected system face.
Initial removal trials failed, leaving both inventory and choice intact, and
showed pale desktop content, missing strokes and overlapping text. The shared
blend-state fix below corrected the visual defect. A further removal retry
then exposed test readiness accepting a Fonts launch from retained older logs.
`open_fonts` now requires a fresh launch since the click, using existing trace
and syslog baselines. Its regression fails before the fix and passes afterward.
The physical retry passes without changing the font-service runtime: built-in
choice `system:astra-workbench` persists, installed inventory is empty, the
imported AFNT object is absent, no trial panic occurs, and the trial provider
retires. Captures show readable eligibility labels, confirmation and removal.
Beast evidence is under `/var/tmp/astra-font-fpga-evidence-20261017/`, including
`remove-after-readiness-fix/` and `warm-production/`; failed attempts remain
retained in `attempt-before-blend-fix/` and `remove-after-blend-fix/`.
Production selector remains `57078599`; trial storage was disposable.

Subsequent RTL review found a shared render-state leak:
`astra_render_command_processor.sv` passed the last `FILL_RECTS` blend flag
to ordinary `FILL` commands. After a blended list, RGB565 clear colors were
treated as transparent ARGB colors. The existing slow-write ordering bench,
extended with ordinary fills after blended lists, fails before the one-line
opcode qualification and passes afterward (175 commands). This explains why
a runtime restart without FPGA reset cannot restore correct clearing. The full
graphics suite, clean route, exact-shell check, and initial plus post-Fonts
production desktop captures now pass. No software workaround was added.

The clean full-feature replacement shell now routes successfully on Beast:
RBF `9a5531c3f98c0cb384fa35c5007574f85c4c9e15e3c72d0572f0d494ed8cadb3`,
40,754/46,800 ALMs, unchanged memory/RAM/DSP usage, all clocks constrained,
graphics setup +0.259 ns, worst setup +0.007 ns, worst hold +0.000 ns.
Its complete artifact manifest verifies; that shell is installed as above.
DE25's actual current boot script and retained RBF have a verified rollback
bundle under `/var/tmp/astra-font-fpga-20261017/rollback`.
Preflight also caught generic fabric installation replacing the storage
kernel's versioned boot recipe. Fabric packages now contain no kernel boot
selector; fabric installation preserves the existing script. The behavioral
regression fails before the fix and passes afterward, along with complete
kernel install/arm/rollback and the real FIT/full-package staging gate.

Production image backup is retained on Beast at
`/var/tmp/astra-font-fpga-evidence-20261017/storage-before.img` (SHA-256
`e23d0700fa45d29d0cada665ef67b9c800a901bb00a8af1ecdd7645c3b75c55d`).
The failed font archive and three old DE25 panic-trial archives were transferred
to Beast with complete SHA-256 verification before removing those diagnostic
copies from DE25; panic archives are under
`/var/tmp/astra-de25-diagnostics-20261017/`. Production images and mounted
provider data were not removed. DE25 has 1.9 GiB available after cleanup.
Full font correctness still requires live refresh, coherent designed-style
resolution, and a newly built
immutable software release with repeat physical import/select/remove gates.

Display's shared `CONTENT_SURFACE` builder now inherits the configured UI face,
closing a gap where Desktop/Atlas content still used the built-in face while
chrome used the saved AFNT. The real-AFNT regression fails before propagation
and passes afterward, checking glyph advances, submitted bitmap rows and null
font recovery. Display host, sanitizer, MC68040 and analyzer gates pass on Beast.
The native GUI gate also passes TTF import, family details, saved choice,
restart and uninstall with current QEMU source identity `0c20ae9150c2`.
The first run mistakenly used the older panic-soak QEMU, which lacks the font
channel; that failed run is retained in
`/var/tmp/astra-font-content-face-ui-20261017.log`. The corrected passing run is
`/var/tmp/astra-font-content-face-ui-current-20261017.log`. These software changes
are not installed in the retained DE25 font candidate or production selector.

Compositor width and fitting now use the renderer's existing selected
strike/glyph lookup. Window titles, menu-bar headings and launch text therefore
measure the face/style they draw, rather than always measuring the built-in
face. The selected-mono `IIW` regression fails with the old measurement; the
shared measurement and launch clipping checks pass with UTF-8 and fallback
coverage. Graphics and Display host/sanitizer gates and Display MC68040/analyzer
gates pass on Beast. The current-QEMU native GUI gate also passes import,
family details, saved choice, restart and uninstall; log
`/var/tmp/astra-font-metrics-ui-20261017.log` on Beast.
The following client-context checkpoint closes the application measurement gap;
coherent live refresh and designed-style resolution remain open.

Font Kit 3.1 and GUI protocol 22 now give native UI clients an immutable
read-only snapshot of Display's active AFNT and its catalog ID. Desktop, Atlas,
Fonts, Interface Gallery and Terminal initialize this context before layout.
Common width/fitting calls use its glyph advances, and proportional surface
text emits the existing named-face command with the same ID. Monospaced text
remains independent. Display's own controls borrow the retained renderer's
validated view; popup-menu replay also resolves named fonts. Snapshot validation
and adoption use the existing native mutex, explicitly aligned for MC68040.
Host checks cover exact selected advances, UTF-8 fitting, named submissions,
read-only ownership, invalid AFNT, malformed replies and recovery. Designed
weight/slant remains the selected face; semantic bold/italic family-variant
resolution is still unfinished, without falling back to an unrelated family.

Built-in glyph tables and lookup code moved unchanged into `font_data.c`.
HPS renderer helpers link that pure data module, rather than client surface
state or software pixel helpers. Graphics/Display/Interface/Desktop host and
sanitizer gates, full userspace `all test`, Display MC68040/analyzer, NDK
`dist-check` with installed example links, HPS display self-test and AArch64
display/host-read helper links pass on Beast. A fresh coherent ROM (GUI 22,
Startup ABI 8) has SHA-256
`4ad7a25e4539117c893093075bd212bfc61d91c6867d6f6c6fd74f727509c1f2`.
The native GUI import/details/selection/restart/uninstall gate passes with it;
log `/var/tmp/astra-font-client-ui-20261017.log` on Beast. No new font software
was deployed to DE25; production selector, fabric and user data remain intact.

The next Display ownership checkpoint replaces its one temporary named-face
mapping with reference-counted immutable AFNT snapshots. Active default,
delivered GUI session contexts, retained window documents, graphics lists and
popup menus own their references; the registry itself is weak. Successful
replay retires unused faces, failed replay preserves the previous document,
and closing an owner releases its references. The last owner closes the
read-only area, independently of catalog/file removal. Existing hardware
batches copy glyph masks during lowering and retain their own batch storage.
This follows Haiku's `FontManager.cpp:_RemoveFont` / `FontStyle.cpp` separation
of delisting from final data destruction. It adds no kernel or public ABI.
Display host, ASan/UBSan, MC68040 and analyzer gates pass on Beast; log
`/var/tmp/astra-font-lifetime-session-20261017.log`. Tests cover source absence,
replacement rollback, final release, allocation failure and failed GUI context
delivery. The native GUI import/detail/choice/restart/uninstall gate also passes;
log `/var/tmp/astra-font-lifetime-ui-20261017.log`. Live switching still needs
explicit client-adoption handling: a delivered snapshot is not proof that the
client mapped/adopted it, so notification work must preserve the previous
context across client-side failure. The following GUI 23 checkpoint implements
that adoption handoff. Live notifications/client reflow, designed-style
resolution and complete physical font qualification remain open.

GUI protocol 23 replaces the header-only font-context query with a request
carrying the client's actually adopted ID. Display retains bounded confirmed
and pending slots per session and reconciles them on the next request. Failed
client mapping/validation therefore cannot retire its still-active snapshot;
recovery and session cleanup release the same owned references. Font Kit
reads the ID through its existing context mutex, shared with text submission.
Public Font Kit ABI remains 3.1. Graphics/Display host and ASan/UBSan gates,
MC68040 builds and Display analyzer pass. Full userspace `all test`, coherent
ROM build, Graphics library contract and NDK `dist-check` pass; log
`/var/tmp/astra-font-adoption-all-20261017.log`. The GUI 23 ROM SHA-256 is
`414b14e64f8228169ca03e4159daa6672b8e34b7dbdc91a2a807cf28e3f87247`.
Native GUI import/detail/choice/restart/uninstall passes with it; log
`/var/tmp/astra-font-adoption-ui-20261017.log`.
System window/graphics, Supervisor loader-recovery and Display host tests now
depend on the GUI header, preventing stale test binaries after protocol changes.
Forced-header dry runs and refreshed host/sanitizer gates pass in
`/var/tmp/astra-font-adoption-host-final-20261017.log`.
Supervisor's existing per-process `gui_session` reference already keeps each
application's session alive until process retirement; no additional client GUI
clone, new port lease or broadened transfer rights are needed. Manually created
sessions remain their owner's responsibility. No font software is deployed to
DE25. Live notifications/reflow and complete physical qualification remain open.
Final Graphics host/sanitizer/build/contract checks pass in
`/var/tmp/astra-font-adoption-final-20261017.log`; the Font Kit binary matches
the one used by the passing native GUI gate byte for byte.

Font Kit ABI 4.0 / GUI 24 / window-event version 9 / Fonts service protocol 6
now implement live system-font changes. Selection prepares and retains an
immutable snapshot before Config Kit persistence, preserving the old choice
on preparation/write failure. The native `FONTS_CHANGED` event wakes Display,
which refreshes from the authoritative catalog and retries failures while
keeping its prior context. Window notifications retry until that window
reports the adopted ID; the context request carries an explicit window ID
checked against its session. One window cannot prematurely acknowledge a
sibling's reflow. The superseded Font Kit 3 contract is removed from all
in-tree producers, consumers, manifests, exports and package inputs.

All supervised GUI clients now have private sessions: application sessions
retain launch names, service sessions are unnamed. This prevents GUI services
from sharing independent per-process font state. Common Interface Kit controls
reuse their retained layout and full-view damage for font changes; modal alerts
and the existing Desktop safe-removal canvas also refresh and reflow. Terminal
monospaced content stays independent. Haiku references are recorded in
`docs/FONTS.md`. Fonts selection host/ASan/UBSan/native/analyzer gates pass;
`/var/tmp/astra-font-live-focused-20261017.log` retains the initial later
Display compile failure (wrong menu-cancel argument count). Corrected UI and
native gates pass in `/var/tmp/astra-font-live-abi4-ui-final-20261017.log`.
The first modal-alert extension exposed a test's stale global pointer-call
counter; its own setup now resets that counter. Coherent build, NDK and native
live GUI qualification are running. Production software/user data remain
untouched; complete physical font qualification remains open.

The coherent userspace/ROM and NDK `dist-check` now pass, including installed
example links and HTML/PDF generation. Doxygen could not resolve the anonymous
font-event payload; it now uses a named payload, like the other window events,
without changing wire layout. The first live GUI run then exposed a real
mapping-lifetime defect: duplicate area handles map to the same process
address, and refreshing the unchanged face unmapped its active font. The
diagnostic run records Fonts' user fault at `0x40000d38` in
`/var/tmp/astra-font-live-abi4-ui-diagnostic-20261017.log`.
Clients now retain an unchanged immutable context without remapping. The
shared snapshot adoption path also closes duplicate handles without unmapping
their common address. Display borrows the validated view owned by its retained
font registry instead of creating a second mapping owner. Host checks cover
alias adoption and borrowed-context retirement. Corrected Graphics/Display
host, ASan/UBSan, MC68040, analyzer and export-contract checks, full userspace
build and coherent ROM pass in
`/var/tmp/astra-font-live-map-borrowed-20261017.log`. The strengthened native
GUI gate passes live Desktop/Fonts adoption of the imported face, exact Config
Kit persistence, restart, restoration of the built-in face and uninstall;
log `/var/tmp/astra-font-live-map-ui-20261017.log`. Its ROM SHA-256 is
`b84744560902eb92bbcb2f00b15363bf9587a40ea01442345bef5542224cf574`.
The final userspace test suite and refreshed NDK `dist-check`, including
exported borrowed-snapshot API, installed example links and HTML/PDF manuals,
pass in `/var/tmp/astra-font-live-map-sdk-final-20261017.log`. Complete
physical qualification remains pending. Production remains unchanged.

The clean `cae119b7` software release was staged and verified as
`ce8f5e87f6f56f5ae3ae907aad51fecca8058f06268be0a2c02403a91e2ca038`,
ROM `30a3d0c96087be76361067b2d1f8b306ee002cf4662a2bfdc63a5c55568c6fc3`,
source manifest `316148bad238164e052e8b591a4cc755745bf06580e41874dbb17287b7e2c702`.
It remains withheld: subsequent review found retained-control font refresh
reused cached intrinsic widths. The new wider-font regression fails against
that code in `/var/tmp/astra-font-live-remeasure-red-20261017.log`.
Initialization and font changes now reuse one measurement pass before retained
layout. Labels move their siblings correctly; a scrolled List remeasures while
preserving focus and scroll offset. The first extended List fixture stayed
below its existing minimum width; it now uses a longer family label to exercise
actual width growth. Interface/Display host and sanitizer checks, MC68040
builds and coherent userspace/ROM pass in
`/var/tmp/astra-font-live-remeasure-green-final-20261017.log`.
The native Iosevka Regular 34.9.0 import/live-choice/restart/uninstall gate passes
in `/var/tmp/astra-font-live-remeasure-iosevka-ui-20261017.log`, adopting
`user:iojgz2r5qqd4swiwlocn2qcmfzeqkprz7q57rinn5g32y2af2qhq` in both Desktop
and Fonts. The test ROM is
`ef8e87f21c0c1adf2c2150e75efed50015cb4af2389888e9e34ba7dfc18cc95f`.
Haiku reference: `src/kits/interface/View.cpp:InvalidateLayout` invalidates
cached size constraints as well as layout and propagates to children.
Beast publication requires `FLUIDSYNTH_ROOT=/home/barry/Git/fluidsynth` (clean
pinned `71e85b2c`); the first attempt's missing default path is retained in
`/var/tmp/astra-font-live-release-20261017-cae119b7.log`, and the passing retry
in the corresponding `-retry1.log`. Set both ROM/kernel Git revision variables
explicitly because the mirrored Beast tree has no `.git`. A replacement
immutable release and complete physical font qualification remain pending.
No new font software or trial data has reached DE25.
The final full userspace test suite and updated NDK distribution/examples pass
in `/var/tmp/astra-font-live-remeasure-sdk-20261017.log`.

Latest work covers native font management, scoped settings, and migration of
native UI pixel rendering to Astraea. The fixed/removable filesystem remains
in scope; network mounts remain excluded. Correctness remains ahead of
performance.

Graphics offload now targets broad SDL compatibility: keep streaming textures
and writable window surfaces for existing software renderers, while moving
feasible drawing and indexed palette conversion to Astraea. DevilutionX and
ScummVM are next port targets; both need those general paths and enough CPU
headroom for game logic. The current SDL renderer already accelerates draws.
Native INDEX8 window presentation and palette updates pass host, MC68040 and
QEMU render-probe gates. A disposable full-desktop QEMU image launches
Interface Gallery, Atlas, and SDL TestDraw2 with indexed icon blits. Physical
DE25 candidate `57078599bcd151e88eb01c3767635875d89e154cebf6183fb69d41b6b1286421`
reached stage 8 and passed 1920×1080 desktop, Atlas, styled Gallery text and
live Doom HDMI captures, normal Doom quit, balanced hardware submissions, and
zero host-service restarts. A second isolated run of the same release captured
Terminal, animated SDL TestDraw2 and the DH0 right-click menu; SDL close
settled at 17,512/17,512 hardware submissions/completions. The release is now
selected on DE25 with the existing production user state; the previous release
and its state remain available for rollback. Actual DevilutionX/ScummVM port workloads and
their performance measurements remain open.
An isolated physical SDL INDEX8 probe also captured a live palette-backed
window through HDMI DMA: four hardware triangles, three surface reads, 19
palette blits, 77/77 submissions/completions, and expected RGB565-quantized
upper/lower colors. Its raw frame SHA-256 is
`a2431151cea8c717b1962c8616b9b5d237059889eddc7c915b1cc52ffaae6ba3`.
Production services returned to stage 8, active with zero restarts.

Long-run kernel stability remains an open release gate. A previous FS14 ROM
(`443536186af4`, built 2026-10-15) panicked after about 22 guest hours on
`WAIT_MULTIPLE` with `KERNEL_PROCESS_CORRUPT`; an intermediate graphics
candidate (`fc05a5`, built 2026-10-16) panicked during deferred worker service
after repeated remote-desktop exits. Neither panic report identifies the
failing internal branch. The selected graphics release passed short physical
trials, not a comparable long soak. Diagnostic-only wait and worker failure
traces, plus process-maintenance diagnostics, are under test on disposable
images. A separate deterministic reaper defect was found: after thread
records were released, a pinned owner frame deferred memory release, and the
next maintenance pass tried to release the same threads again. Teardown now
records that completed step; a focused host test failed before the fix and
passes afterward, including repeated deferred passes. Kernel MC68040 build,
host suite, and ASan/UBSan process tests pass. This defect has not been tied to
either historical panic report; stability certification remains open.
Haiku reference: `~/Git/haiku/src/system/kernel/events/wait_for_objects.cpp`
registers object selections before sleeping and preserves events arriving
during registration. Astra retains its native queue-sequence checks; this
investigation changes failure observation only. `WAIT_FAILURE` trace arguments
are source line, wait index/count, failing status, thread ID. `WORKER_FAILURE`
arguments are work mask, class index, service status, worker state. Reap
maintenance `observed` is source line and `expected` is process/thread ID;
pool-health `observed` bit order is process, port, area, ring, IRQ, transfer,
sync, device.
Haiku `~/Git/haiku/src/system/kernel/team.cpp:team_delete_team` also separates
team exit from final reference release. Astra keeps its native reaper and
records completed teardown phases across `KERNEL_MEMORY_BUSY` retries.

Beast QEMU panic soaks on disposable images passed 600 seconds each with 65
remote-desktop retries, before and after the reaper fix. The fixed ROM SHA-256
is `beb3dea8a0958263f76f7d1ec82038c9287b14982a946930599b9012cf0c5ccf`.
An isolated DE25 run of the pre-fix diagnostic ROM reached stage 8 and ran
about nine guest minutes with 58 remote-desktop retries and no panic; the
fixed ROM reached stage 8 and ran about nine guest minutes with 56 retries and
no panic. Both trials used disposable storage; their evidence is under
`/var/tmp/astra-panic-trial-20261016b/` and
`/var/tmp/astra-panic-trial-20261016c/` on DE25. The first launcher attempt
failed before guest boot because the disposable audio-mailbox directory was
root-owned; the corrected launcher passed. Production state was never used as
trial storage. Production returned to stage 8 with runtime, audio, and remote
services active and zero automatic restarts. The selected release remains
`57078599bcd151e88eb01c3767635875d89e154cebf6183fb69d41b6b1286421`.
A 24-hour Beast QEMU soak of the fixed ROM is running as user unit
`astra-panic-fixed-24h.service`; progress is in
`/var/tmp/astra-panic-fixed-24h.log` on Beast. Its completion and any exact
failure trace remain the long-run release gate, especially for the historical
22-hour `WAIT_MULTIPLE` panic. The fixed ROM is diagnostic only, not selected
on DE25.

The 2026-10-17 font-trial precheck found one automatic restart of that selected
production release and a retained `deferred worker service failed` panic from
the 2026-10-16T20:14:31Z ROM. The report is preserved on Beast as
`/var/tmp/astra-pre-font-production-panic-20261017.log`, SHA-256
`689c383252c8050574d7ae528aa96b9445a061bac1423f1664353f4d1f6c91f6`.
The diagnostic reaper fix was not selected on the board. Its separate Beast
soak remains live past 18 hours; that partial run does not close stability.

Font-management work is isolated from production storage. Interface Kit 6.0
adds a shared, composed List control with an Interface Gallery consumer and
requires draw-list views for native controls and menus. This ABI bump is
not in the production checkpoint. Interface host, sanitizer, analyzer and
MC68040 gates, full userspace `all test`, and NDK `dist-check` with installed
example links on Beast pass. A disposable Beast image passes QEMU desktop
launches for Interface Gallery, Atlas, and SDL TestDraw2, plus context-menu
pointer and keyboard interaction. A separate disposable SDL render-probe image
passes four hardware triangle commands, three surface reads, and 65/65 display
submissions. Native display and Doom input/rendering passed the isolated DE25
candidate gate. Build-time AFNT conversion covers TTF, OTF, selected TTC faces,
variable default instances, and monochrome Amiga disk fonts. Native runtime
import covers AFNT, static TTF/OTF/TTC, and default plus distinct named
variable instances. A bounded HPS C parser and converter now produce validated
MASK1 AFNT from classic monochrome Amiga disk-font hunks with explicit
ISO-8859-1/2 character maps. Parser, converter, worker, sanitizer, and AArch64
checks pass; the native service/QEMU installation and restart gate passes.
Native named-face application drawing through display's validated AFNT resolver
and Astraea glyph engine passes the disposable Amiga QEMU gate without granting
raw-font access to the application. Physical release gates remain open.
Fonts labels each style as able or unable to serve as a system font and disables
system selection for ineligible styles, using the service's catalog decision.
Catalog v3 requires exact printable-ASCII coverage as well as all three UI
sizes; replacement-glyph fallback cannot qualify symbol fonts for system use.
Style selection preserves List focus and scroll via existing control setters.
Beast host and sanitizer coverage/policy tests, full userspace `all test`,
and NDK `dist-check` pass. The normal root-observed disposable QEMU GUI gate passes native TTF
import, family details, keyboard selection, saved system choice, restart,
and uninstall with catalog v3; QEMU remains unprivileged.
The native GUI regression also passes a valid AFNT with a 127-byte style name:
the previous fixed row buffer rejected details after import, preventing choice
save. Row storage now follows catalog lengths with checked arithmetic;
eligibility and size precede the style so they stay visible when a long name
is clipped. The clean release publisher also caught missing shared AFNT
dependencies in the HPS display-helper link. The common renderer source list
now includes the existing codec; host/AArch64 display self-tests and the
AArch64 host-read helper link pass. Initial publication failed because
FluidSynth moved, then because of the missing codec link; neither produced or
deployed a release. Beast publication uses the clean pinned checkout through
`FLUIDSYNTH_ROOT=/home/barry/Git/fluidsynth`.
Fixed-size Amiga faces without all three UI sizes cannot become system fonts.
Custom variable coordinates remain
open. Shared C AFNT 0.2 validation and packing round-trip shipped fonts,
Iosevka, Source Sans, and Amiga spike AFNT on Beast. A reusable HPS C outline
converter turns static Iosevka TTF and Source Sans OTF into validated A8 AFNT;
host/sanitizer and AArch64 compile checks pass. Shared AFNT C metadata readers
now expose family, designed style, strike metrics, and validated glyph lookup
without passing disk records to UI clients. Beast host, sanitizer, MC68040,
and AArch64 codec gates pass. The HPS converter prepares 11, 13, 16, and 20-pixel
strikes, covering current proportional theme roles; Beast host, sanitizer,
MC68040/AArch64 codec, and outline-conversion gates pass. The HPS worker is
connected to the native Fonts service through the versioned QEMU host channel;
the Fonts UI sends selected files through that service. Interface Kit bundle metadata now
provides 6.0.0, matching its current ABI. [Font scope and runtime gaps](FONTS.md#7-import-and-style-compatibility)
and the [Iosevka evaluation](IOSEVKA_EVALUATION.md) record details. Config Kit
1.1 now opens a separate `CONFIG_SYSTEM` grant for resident services, rooted at
`/config/system/<service>`; apps retain private `CONFIG` and cannot receive
the system grant. [Settings ownership](SETTINGS.md) fixes the system/app split
and default-on-damage policy. Display consumes saved font choice at startup;
live selection refresh remains open. Direct
RGB565 software surfaces still have CPU glyph loops; native draw-list text uses
Astraea. Font Kit 2.3 prepares built-in bold/italic AFNT-derived strikes at
build time, removing per-frame style bitmap synthesis. Beast pixel-oracle,
sanitizer, analyzer, MC68040 library, and full userspace tests pass; QEMU and
physical DE25 styled-text visuals pass on the candidate.
SDL's RGB565 writable framebuffer and Chocolate Doom's software renderer remain
CPU graphics paths. AICON desktop/Atlas icons now upload indices and palette
metadata rather than expand pixels on the CPU. Launch-panel and title-bar
icons use the same palette-backed blit; indexed uploads preserve raw indices
until Astraea expands them. Graphics Kit 3.0 returns
palette-backed INDEX8 icon sources; System Kit 4.2 adds the native palette
update call and GUI protocol 21 carries it. These changes are QEMU- and
DE25-qualified and selected in the production checkpoint.
[Graphics audit](GRAPHICS_RENDER_AUDIT.md) tracks hardware
offload opportunities, current limits, and release gates.
Desktop's scrollbar now renders through Interface Kit commands appended to
the system draw list, removing its production CPU pixel path. System Kit 4.2
adds the checked view-to-session append API. Full userspace build/test and
System/Desktop host, sanitizer, analyzer, and MC68040 builds pass on Beast;
QEMU and DE25 candidate visual gates pass.
There is now a native validated AFNT catalog snapshot and a draft resident
`fonts` service with separate read/admin ports. Its MC68040 build, focused
policy tests, sanitizer, and analyzer pass; an isolated disposable QEMU boot
launched it with status zero. Font Kit 2.4 adds a client for read-only catalog
snapshots and admin-only choice, with malformed-reply/catalog tests and an
MC68040 library contract pass. Disposable display startup now publishes the
service and installs a native Fonts bundle. Its first UI groups installed
families and shows designed styles and AFNT strike sizes; shared List activation
uses Haiku's selected-row double-click/keyboard pattern. MC68040 bundle, image
policy, Interface sanitizer/analyzer, full userspace `all test`, and NDK
`dist-check` pass on Beast. A disposable QEMU boot reported fonts and desktop
service startup status zero; a separate Fonts autostart image launched the
native bundle and submitted hardware glyph commands. A separate root-observed
disposable QEMU gate now launches Fonts through its desktop icon, double-clicks
a family, opens its style/strike detail view, and saves a system-face choice.
The standard confined `astra-qemu-root` fixture keeps WORK/SOUND exports
root-only; this focused gate observes desktop logs through the root-owned
fixture without weakening that boundary.
Font Kit exposes an admin-only font import call. The resident service
writes a validated content-addressed object, syncs it, commits its full SHA-256
token through Config Kit, and publishes the new catalog generation. The native
Fonts UI has composed List/button views for a DH0 file browser and system-face
choice. A disposable QEMU image with current bundles passes Fonts autostart and
Astraea glyph submissions; focused service/client and boundary checks pass on
Beast. A fresh disposable QEMU image now passes a full GUI path: Fonts imports a
TTF from DH0 through its file browser, publishes a content-addressed AFNT
object and Config Kit inventory, opens family details, then chooses that
imported face as the system font. The saved `user:<token>` matches the AFNT
object token. A stray Flex break flag had placed the List outside the client area;
removing it restored pointer and keyboard interaction. Native Amiga import,
custom variable coordinates, live render-context refresh, removable-volume
browsing, and physical release gates remain open.
Font service protocol 4 and Font Kit 2.7 add administrative uninstall for
imported faces. The service refuses removal of shipped or active faces, commits
the new Config Kit inventory before retiring the catalog entry, and then
unlinks the unreferenced AFNT object. The native Fonts UI confirms removal and
reloads its catalog. A two-boot disposable QEMU GUI gate imports and selects a
TTF, selects a shipped face, confirms uninstall, and verifies inventory and
object removal. A focused native probe covers catalog compaction with another
imported face retained, generation changes, active/shipped rejection, and
restart persistence. Haiku's app-server font removal path informed the
delist-and-revision behavior; Astra uses Config Kit as its publication point.
These changes are not in the selected DE25 release.
An isolated native QEMU probe imports shipped AFNT through the same admin-only
API, checks catalog publication and idempotence, selects the imported face,
then reboots the disposable image and verifies persisted catalog and choice.
A third boot with a damaged installed-font inventory and unavailable choice
proves built-in fallback and refusal to write over the damaged inventory.
It exposed and fixed a port-call handle-order defect: the source area precedes
the reply capability in an import request. Physical DE25 release remains open.
Build-time Python conversion is a development oracle, not a user import path.
Installed-font storage uses full SHA-256 content tokens and a single
validated Config Kit inventory value as its atomic publication point.
The service caps AFNT reads, verifies referenced objects, and falls back to
built-in faces without editing damaged user settings. Runtime import accepts
AFNT and static TTF/OTF/TTC in disposable QEMU, not the selected production
release.
Beast MC68040 font-service build, focused tests, sanitizer and analyzer pass;
full userspace `all test` passed before the final logging-only change. A fresh
disposable image with the final service passes the Fonts native autostart and
hardware-glyph QEMU gate. Inventory recovery with an injected damaged settings
document passes in the isolated native QEMU import probe. Font service
protocol 3 adds a separate `FONTS_RENDER` capability that delivers a
revalidated, read-only AFNT area for an installed face; normal startup grants
it only to display. Font Kit 2.6 exposes the matching client call. Beast
MC68040 builds, focused service/client tests, sanitizer and analyzer, full
userspace `all test`, and NDK `dist-check` pass on Beast.
The disposable QEMU probe checks exact built-in and imported AFNT bytes
through this port, including after restart and damaged-inventory fallback.
Display now opens the selected, validated AFNT area at startup and passes it
to the shared render builder. Available regular proportional strikes become
Astraea glyph-run commands; missing strikes and mono text retain built-in
fallbacks. Beast graphics pixel-oracle, sanitizer, display
build/test/analyzer, full userspace `all test`, and a disposable QEMU Fonts
autostart/hardware-glyph gate pass. The gate now installs current services and
fonts into its cloned image before boot; an older image lacked both and caused
a false required-service failure. The native AFNT import probe also passes
selection, restart persistence, and damaged-inventory fallback on the current
ROM. Live selection updates, designed style resolution, client-side text
metrics, custom variable coordinates, Amiga runtime import, and DE25 visual
gates remain open. This font change is not in the selected production release.
Selecting a different system face now advances the catalog generation;
selecting the current face leaves it unchanged. The native import/restart
probe passes both cases on a disposable Beast QEMU image.
An internal HPS C worker now accepts one bounded, versioned source request and
returns validated AFNT; host, sanitizer, AArch64 QEMU-user, and physical DE25
conversion gates pass. The board test used an isolated `/var/tmp` executable
and did not modify the selected release or production settings. Native
TTF, OTF and two-face TTC imports now pass disposable QEMU gates through the
Fonts service, including AFNT catalog publication, system selection, and
restart persistence. The service runs conversion on a separate native thread
because VFS holds the caller thread's one host channel.
STIX Two Text 2.13 now passes the same native path as a variable-font fixture:
FreeType's default and named styles produce four distinct AFNT faces (Regular,
Medium, SemiBold, Bold), while the named Regular duplicate is skipped. Source
face, instance, and 16.16 design-axis coordinates are recorded in AFNT metadata.
Beast host, ASan/UBSan and AArch64 QEMU-user worker tests pass, as does a
disposable two-boot MC68040 QEMU import, catalog, selection, and restart gate.
Static two-face TTC conversion still passes after the index mapping change.
The selected DE25 release and production font inventory were not changed.

A valid 16.8 MiB three-face CJK TTC produces about 43 MiB of AFNT per face;
the current 32 MiB installed-face limit rejects it as capacity, with no partial catalog
publication. The production DE25 release has not changed.

Current production checkpoint: graphics release
`57078599bcd151e88eb01c3767635875d89e154cebf6183fb69d41b6b1286421`,
source manifest
`e1d6324b734baa6f888bc0fc9b542434ba08f9bff32274bba6ca419f54c9e96a`.
Production source 28 was stopped before copying its 256 MiB state image and
WORK/SOUND exports to both DE25 and Beast; image SHA-256
`1cf291e0176902cbf1d066e12d032d0fa322ad459d771bddd2afbf48900c221b`
and export archive SHA-256
`6b7253eb56cb0445c1cfb436c2c3a403093e4496b6777c052f7b067e8f0da41b`
match across hosts. Beast installed the candidate's system products into an
image clone, then `verify-state-preservation.py` confirmed all 25 existing
`/home`, `/local`, and `/config` entries and their content/inode metadata
unchanged. The staged image SHA-256 was
`32e4114e4864cbdd6dad8fd520420f50111febc079b168110352647a3d4bfbc0`.
DE25 booted this exact staged state with the existing exports, reached POST and
stage 8, shut down cleanly, and passed offline `e2fsck -fn` plus a second
25-entry preservation check. The postboot image SHA-256 was
`f6fc7e15e267f02a90935e5b424d0a2e715f3072560f12361a2e12557ec50ab7`.
After atomic selector change, normal production boot and warm restart both
reached stage 8. QEMU runs as UID/GID 999 with no supplementary groups or
capabilities and `NoNewPrivs=1`; runtime, audio, and remote services are active
with zero automatic restarts. Physical 1920×1080 HDMI DMA capture passed and
matched the earlier qualified desktop frame SHA-256
`37d1bbfe3df5fbb5b0c3a8d6b278724f1222aefd572bd6e52cd3aa1534b3b091`.
The older source-28 production log still records a rare
`syscall dispatch contract failed` panic from `WAIT_MULTIPLE` after about 22
guest hours. An intermediate graphics candidate also had one deferred-worker
panic with missing host services; a 9-minute isolated run of the selected
release with those services absent did not reproduce it and all eight watched
pool-corruption flags stayed clear. Neither root cause is established; longer
soak and targeted fault diagnosis remain open.

Previous production checkpoint: coherent Startup ABI 8 source 28, manifest
`443536186af4626cfc75a2f8404a0a22d6e9a9bda9e1afe60a25675f219af05c`,
immutable release
`eeb95de8da994a2d0d976f74f691f7644fcdf9354d249af9bcf7b00fab116350`.
Its clean kernel, userspace, ROM, image, seven filesystem gates, native volume
lifecycle (ISO, busy holders, RAM/HostFS/restart, stale generations), Atlas
overflow, desktop volume/safe-removal UI, Terminal/Vim and 30-second Doom
gameplay/audio/MIDI replay pass. Source 28 also passed extracted NDK examples,
six power cases, service policy, and SDL runtime/video/input/media probes.
Source 28 corrected the launcher
preflight to accept a readable, non-executable Python image selector; the
isolated new-runtime and old-runtime rollback trials both reached POST/stage 8
on DE25 with separate image/export clones and unprivileged QEMU.

DE25 now runs the complete production-storage Linux kernel
`6.12.11-astra-storage1-gd7d192a9ddd9`, package identity
`ab511cc12039f8bd0cb0d75b78295ef9513c9f40b90d6f52b07eadb1dc51844d`.
Canonical stage, arm, physical boot, vendor rollback, and re-arm/second boot
all passed through Beast. Old Astra release
`0aa2d7130c3120df7d7b64481e5ee2b6393b603767814213b673285fd68f9b36`
remains pinned as rollback predecessor with its original state image. A fresh
stopped-state backup after its final production boot is sealed at
`/var/backups/astra/fs14-final-20261015/` and duplicated under
`/home/barry/astra-fs14-production-20261015/final/` on Beast. Image SHA256 is
`84e534e5130625a97671ebc27857d0d048b9286b3f0a1620eec95cff8d078a51`;
WORK/SOUND archive SHA256 is
`a009061993c4edf9deee063b1f8510ecd89763ebbf391ad7cae8304aa4beea93`.
Archive restore matched exported content and metadata. Journal replay and a
free-block-count correction were applied only to a separate reference copy;
source 28 image install then preserved all 23 existing `/home`, `/local` and
`/config` entries and passed clean fsck. Installed candidate SHA256 was
`5044ba8fe5457913fa49a985c50696f01a626c375fc9c2bd1fb6c7ec573664cd`.

Canonical DE25 activation and kernel confirmation passed. Production source 28
reached POST/stage 8 before and after runtime restart, with QMP, three active
services, zero automatic restarts and QEMU UID/GID 999, no groups/capabilities,
and `NoNewPrivs=1`. Physical 1920x1080 HDMI DMA capture passed; DH0 double-click
opened Atlas and enumerated root. Guest `volumes --all`, `df -k` and `lsof /dh0`
returned mounted volumes, capacities and exact live holders. A unique WORK file
was written through guest zsh, read back through guest `cat` after restart with
status zero and matching host SHA256, then removed through guest `rm`. No trial
drop-in remains; production state and exports are selected.

Physical USB/CD hotplug, safe-removal and eject remain unqualified because no
such devices are attached. Network mounts are excluded; broad performance
qualification follows correctness, not this cutover.

DE25 Linux root `/dev/mmcblk0p2` has been repaired offline. A volatile U-Boot
RAM rescue boot left p2 unmounted; no persistent boot setting changed. The
first 30,951,865,856-byte streaming backup did **not** match the board and is
forensic evidence only, never a rollback source. A second full backup at
`/home/barry/astra-fs14-rescue-20261015/mmcblk0p2-pre-fsck-copy2.img` on
Beast matched the board's independent SHA256
`786e72bc43d7b250ae75c25e142d5d7c7f83c2608357bc6ec8363c3630ee66fd`.
Read-only diagnosis of that verified copy found three deleted inodes without
deletion timestamps and bitmap-count differences; it did not reproduce the
first backup's spurious multiply-claimed firmware block. A cloned repair trial
used board e2fsck 1.46.5 (`-f -p`, exit 1), followed by `-fn` exit 0; content
and metadata comparisons of `/home`, `/etc`, and `/var/lib/astra` against the
verified original were empty. Only then did the board run the same `-f -p`
offline (exit 1), with follow-up `-fn` exit 0. Its next normal boot selected
the same source 28 release and boot script, reached stage 8 with one active
`astra.service`, unprivileged QEMU, zero service restarts, clean root state,
and validated production backup checksums. Live `/home` and `/etc` content and
metadata still match the verified pre-repair image. Physical Atlas opened DH0
and enumerated 16 root entries, including 15 folders.

One post-repair `ext4_block_to_path` warning names the 6.6 KiB, readable
`/var/lib/astra/log/qemu-console.log` (inode 112146) and logical block
1074791436, exactly the first block past the legacy triple-indirect mapping
limit; [Linux ext4's parser](https://github.com/torvalds/linux/blob/v6.12/fs/ext4/indirect.c#L68-L105)
identifies that as an out-of-range request, not an on-disk block reference.
Root remains `clean`, with no new MMC I/O error.
Retain this warning in follow-up diagnostics if it recurs. Repair logs and
both backups remain on Beast; only the byte-matched second copy is trusted.

## Filesystem 14 correctness closure, 2026-10-15

Beast's exact source-only mirror of `a7c3ce60fed99e1641bea137f365395c0ad3dc33`
built userspace `all test`, DE25 boot `all test verify`, QEMU host, the storage
provider and NDK `dist-check`; ROM and kernel carry that nonzero source ID.
The tested ROM SHA256 is
`275a76097fe3913f587a9c5212cd07fc2f7f76a676d5d2e0509abbdb1fadf121`,
kernel SHA256 `20209689e7a787f4951919f521acef4086a77a3565c5495ad802ff73e5829f2f`,
QEMU SHA256 `c8ba2eacc2230e7452b07b005864c766e1851627c86067cfb3d0f27cd9af3544`,
and provider SHA256
`b0eb43048750e6bedf2418678e3abc672e0851c55cc31688ac38e25d79a773c1`.
The selected source 28 image SHA256 is
`5044ba8fe5457913fa49a985c50696f01a626c375fc9c2bd1fb6c7ec573664cd`.
Installed provider and root helper match their tested source. All seven
`emu/qemu/fs-gates.sh` gates passed twice, including native/POSIX atomic save,
file and parent sync, injected post-ack provider-reset failure (status 88),
acknowledged-byte readback after provider reset and HostFS death, and clean
provider-image fsck. Provider failure is not physical power-loss proof.

The coherent cross-system matrix passed 29 gates: Linux lifecycle,
Terminal/POSIX/Vim, FPU/fork/exec, Desktop/Atlas/volume/context UI, service
policy, SDL timer/thread/video/draw/render/keyboard/mouse/image/mixer/TTF/net/audio,
30-second Doom, six shutdown/restart variants, all 15 extracted NDK examples,
and system-log persistence on a fresh first-boot image. A separate critical
service fault test halted the guest and named the failed service. The 46
QEMU/ROM/kernel/provider/image/policy/library/fixture inputs matched SHA256
before and after the matrix and after the critical test. Logs and hashes are
under `/home/barry/astra-fs14-evidence-20261015/` on Beast:
`fs-gates-final.log`, `durability-focused-7.log`, `matrix.status`,
`matrix-run-12.log`, `service-critical.log`, and `final-inputs.log`.
Read-only Haiku reference: `~/Git/haiku/src/system/kernel/fs/vfs.cpp`
`common_sync()` resolves a descriptor then calls its filesystem's single
`fsync` operation. Astra retains one native filesystem sync with the POSIX
adapter above it; this closure changed test harnesses, not that substrate.
The system-log harness now reads the provider's private mounted root through
its root observer while the provider's raw-image lease remains intact.

## Historical filesystem investigation (superseded checkpoints)

Filesystem 14 / Kit 11 development began with native administrator control,
exact unmount fencing and retained-file image admission. Those mechanisms
passed the earlier control/provider gates. Provider protocol 2
adds physical eject, explicit image-source retirement, private retained-backing
inspection and typed admission references. Linux provider, real image mounts,
discovery and sanitizers pass in the isolated
`astra68-fs-lifecycle-gate/build/linux-first/volume-lifecycle-cleanup-2.log`.
Fault-injected rollback and CLI lifecycle tests also pass host and sanitizers:
failed cleanup remains visible and charged until actual release, mount names
reuse only after real retirement, and CLI completion respects exact generations,
failed eject, provider loss and retained cleanup. The first image-policy gate
had a stale Desktop grant expectation; the corrected second gate passes.
Configured RAM source identity now survives unmount independently of its
current mount name. RAM_READ/RAM_WRITE scoped brokers replace the static RAM0
publication, preserving restricted RAM authority across remounts without
retargeting old grants. The actual raw provider ISO gate
`volume-usage-iso-provider-4.log` passes retained-file admission, read-only ISO
enumeration with UTF-8 names, exact backing/directory usage, busy refusal and
actual source retirement; preserved root-owned data and clean fsck pass too.
`volume-lifecycle-consumers-2.log` passes the native heap consumers, HostFS
host/sanitizers and Linux image admission including actual inode validation.
System 4 host/sanitizers and complete library contracts pass in
`system-consumer-2.log`; Supervisor/catalog/Atlas native objects compile in the
third diagnostic, whose obsolete launch-report oracle is being corrected.
These are focused checkpoints, not a coherent Filesystem 14 runtime release.

Final review found detached cleanup retries and failed-authority resume gaps;
both now use quarantined FAILED state, with actual reaper fault tests passing
host and sanitizers in `volume-lifecycle-cleanup-7.log`. That gate also passes
shared root holder projection, paging and notification-failure tests, native
volume host/sanitizers and HostFS/Desktop target compilation. Usage
protocol/format 2 projects shared root holders from the
existing authenticated VFS sessions, with a process cursor key and changes
published through the existing QEMU usage sequence. No parallel holder ledger
was introduced. QEMU source `03660fae95ef3ffe36c12c5de4a2bb00a632274f0a33086e982477ec19a0b1ac`
and `volume-usage-iso-provider-6.log` pass the complete raw ISO/control gate,
including holder notification authority and stale usage-page refusal. Its
previous failure was a test helper inferring append-write operands for volume
operation 4; typed zero operands correct that oracle.
Usage attribution, detailed Desktop safe removal, live namespace integration,
coordinated 255-byte-name/4095-byte-path transport/launch contracts and full
production cutover still require coherent build and guest/board qualification.
The first native board provider test failed at transient final loop release;
its bounded retry oracle is corrected and `volume-provider-board-2.log` passes
actual AArch64 provider/image/loss/cleanup tests with transfer hashes verified.
System/Supervisor consumer gate 4 passes the final owned host/sanitizer/target
and System 4 library contracts. VFS/SDK full qualification is still in progress.
Fatal provider exit now has an outside owner retaining the exact domain inode
and child lifetimes. `volume-domain-guardian-2.log` passes real mounts, provider
and rollback sanitizers, fatal SIGKILL/SIGABRT cleanup, retained external-loop
retirement and AArch64 builds. The first guard gate exposed a real namespace
anchor defect: the child now reopens the same owned domain inside its private
namespace, with inode and mount-identity oracles. There is no global directory
sweep or alternate storage cleanup engine. The native board provider unit passes
the new constructor. `volume-domain-guardian-board-3.log` passes the actual DE25
launcher, normal/fatal descendants, crash-directory retirement and retained
external-loop cleanup with transfer hashes checked. The earlier board failures
were fixture traversal permissions and Python 3.10 signal-enum formatting; both
oracles are corrected. `volume-usage-iso-provider-7.log` passes the matching
guardian/provider raw ISO gate. `volume-guardian-hostile-caller-1.log` passes all
21 privileged-helper tests, including concurrent disk reservation, preserved
root-owned metadata and exact cleanup on success and failure.

Whole userspace build 1 stopped at the bootstrap interpreter's missing canonical
System PIC dependency; the corrected closed loader contract passes. Further
review found copied native file/client ownership across fork and shared-client
reinitialization during concurrent lazy admission. Epoch-checked native objects
and retained local pins pass focused fork/token-reuse tests, including image
sharing and failed CWD adoption. Full VFS/POSIX host, ASan/UBSan, MC68040,
C++, closed Loader and SDK header/HTML/623-page PDF gates pass in
`volume-fork-owner-final-2.log`. Its final ThreadSanitizer step exposed shared
mock-area aliasing; the corrected per-resource test model passes ASan/UBSan,
ThreadSanitizer, normal and embedded port gates with zero warnings in
`volume-lazy-port-final-2.log`. Coherent guest qualification remains pending. `df`
now queries mounted instances through canonical capacity APIs and exact public
root grants; target analyzer passes, actual guest checks are pending.
The source-only export is `/home/barry/Git/astra68-fs-release-14`. Its clean
whole build 2 exposed an ncurses configure probe missing the canonical System
static archive; the corrected dependency passes fresh configure/archive/shared
contracts in `volume-terminfo-system-static-1.log`. Release review also found
that system-image installation erased persisted `/config` policy. The corrected
installer passes actual repeated ext4 preservation, missing-default, clean-fsck
and atomic conflict-refusal tests in `installer-preservation-3.log`. Upgrade/boot
policy and stopped production-state migration remain required.
Whole userspace compilation and library ownership pass; obsolete CWD-stack and
192-byte events-path test oracles are corrected. Complete userspace tests and
kernel/ROM verification pass with explicit exit zero in
`production-filesystem-build-6.log`. Source identity is
`8bd9f48e4b2ebaecf9f2384c48bbe4202d3359f95d9caafd6d2b0559026270a7`.
The first coherent live guest gate, `production-filesystem-linux-first-1.log`,
fails before Terminal: Desktop returns 36 during graphics/control setup, and
Supervisor logs `store defaults:4: failed`. Desktop now assigns its ScrollView
child's parent before UI admission; Supervisor seeds new-store defaults only
from a validated bundle root, never beneath a raw service ELF. Rebuild 7 passes
the relevant host/target and ROM gates; actual Desktop/Terminal startup passes.
The next live gate exposed an empty root-path query in `df`; canonical `/`
fixes it. Rebuild 8 passes, and `production-filesystem-linux-first-3.log` passes
volume listing/capacity, system reads and HOME/RAM/TMP/WORK writes. Its first
native ISO admission fails with permission error -6. Its native share/control
authority mismatch is corrected in the HostChannel 5 source below; the
coordinated rebuild and full lifecycle/UI/durability remain unaccepted. That
measured build's source identity is
`8db5e77eef1cd13666f0b935d9c3316aa5e6a8d0e0167ebd06edc44f3d2c487e`.
Production remains unchanged; no network mount work or physical USB/CD
acceptance occurred.

The complete production storage kernel now builds from the frozen source-only
export with explicit exit zero in `production-filesystem-kernel-3.log`. Package
`build/de25-kernel-storage/package-3` contains 1,344 modules, including `sr_mod`,
`cdrom` and both Astra modules, with verified depmod output, matching vermagic,
full DTBs and SHA256SUMS. Release is
`6.12.11-astra-storage1-gd7d192a9ddd9`; package manifest identity is
`703bb41efcf0a00fa659da498268395c4799e1b4cd4e7631efc55dfe3c581ab5`.
The verified package, exact source manifest and build log are retained at
`/mnt/Documents/astra68/builds/production-filesystem14-20261014/kernel-3`.
This is build evidence, not physical kernel acceptance. Native ISO admission
capture 2 in the separate VFS gate tree closes with exit 1 and isolates the
refusal before the Linux provider: authenticated native PID 39 is compared
directly against retained-file host owner `0x10000027`. The owner tag/PID
boundary and usage PID projection are being corrected together; ownership
checks must remain exact. The host owner counter is allocated independently of
PID, so decoding its prefix is invalid. The coordinated fix carries the
kernel-stamped native process identity beside the existing resource owner in
HostChannel 5; accounting and release continue to use the resource owner.
Kernel tests/all/verify and the focused raw identity/PID-reuse/refusal oracles
pass. The first complete raw gate stops at an old directory-usage fixture that
substituted resource owner for PID; its typed-channel correction is being
qualified without rebuilding unchanged QEMU. Final native port negotiation now
advertises retained descriptions only when its direct transport can implement
them. Port/process/volume-client, ASan/UBSan/ThreadSanitizer, native VFS/service,
closed Loader and NDK header gates all pass with explicit exit zero in
`volume-negotiated-port-1.log`. Temporary ISO diagnostics are removed. Sources
are frozen for the next coherent source-only build.
Coherent source-only build 9 passes complete userspace tests, kernel/ROM
verification and fresh image production from source
`aa172de3945a0460ab6b8d244711e97cc77968de0309abedc69d14f55b8ab1e0`.
The first actual native ISO admission/read now succeeds. Its compound-output
oracle originally redirected only the last command; shared Atlas command capture
now groups the complete command, with no product change. Actual native gate 5
passes three ISO mount/enumeration/UTF-8-read/busy-lsof/readonly/Atlas/removal and
source-retirement cycles, plus RAM remount. It closes with exit 1 during HostFS
restart while RAM is absent: `VFS port lifecycle:4` is the CLOSED lifecycle enum,
not a syscall status, and recovery re-enters a closed client. That recovery path
was the next blocker. Supervisor now reads first-publisher policy through its
existing read-only provider bootstrap and retains immediate failures in the
existing retry/backoff state. Strict policy absence/error and failed-state tests
pass host, sanitizers and MC68040 in `supervisor-recovery-policy-{1,2}.log`.
Coherent build 10 passes with source
`f392babc5b9f951bec51b942fe587b6b630cdf8d7fb7e13b0547969fcfe53074`.
Native gate 6 proves HostFS and desktop recovery, then stops on a harness waiting
for the already-consumed boot marker. Shared window-opening logic now supports
an already-running desktop and is reused by recovery/quota gates.
`production-filesystem-linux-first-7.log` passes with explicit exit zero: three
ISO cycles, exact busy/lsof, readonly enforcement, Atlas, source retirement,
RAM remounts, HostFS restart while RAM remains absent, shell namespace refresh,
stale incarnation refusal, durable shutdown/readback and clean offline fsck.
The complete desktop overflow/context gates and 30-case filesystem suite remain
next; physical USB/CD and the production kernel/runtime cutover are unaccepted.
Desktop overflow replay accepts all 176 fixture icons but fails End-key
scrolling: Desktop activation intentionally clears application ACTIVE flags,
while display keyboard routing consulted only those flags. The correction
derives its keyboard target from the existing active application or visible
Desktop and preserves activation, stacking and popup ownership. Focused and
actual UI qualification remain required. Release preparation also found that
the constructor's storage-provider make output disagreed with its declared
packaged path. The constructor now supplies its explicit output directory;
fresh and deliberately stale-output behavior fixtures await qualification.
Storage/device/driver/shutdown documentation now describes the Linux-owned
production boundary instead of claiming the retired guest engines run in the
new release. HostChannel comments describe version 5 consistently.
The constructor's fresh-output and deliberately stale-artifact cases pass
with exit zero in `release-tools-create-2.log`.
The user's Haiku reference requirement is now persistent in AGENTS.md.
Read-only review of Haiku commit
`e9ade57f61b587a9c6599cf4effadaa37ad7822f` confirms retained objects, admission
fencing, canonical notifications and separate desktop keyboard focus. Exact
references/adaptations are recorded in the production-volumes plan. It also
exposes an existing common-dialog gap: MODAL is accepted but never enforced.
Owner-session modal routing and CWD-only busy-removal coverage are being
completed before the coherent rebuild. No Haiku checkout changes occurred.
The modal slice is now source-complete: it derives owner-session targets from
the existing ordered window list, preserves unrelated applications, retires
blocked capture/pending input and handles nested/minimize/restore/close
transitions. Focused compilation, sanitizers and actual outside-click UI checks
remain required. The new CWD-only ISO oracle runs unmount and usage observers
from `/`, requires the parent shell's exact directory PID/inode/path and then
releases it before the existing successful removal. This extends actual
coverage without changing filesystem implementation.
Read-only DE25 inventory confirms the previous selected runtime
`0aa2d7130c3120df7d7b64481e5ee2b6393b603767814213b673285fd68f9b36`, vendor
kernel `6.12.11-gd7d192a9ddd9`, internal SD only and 13 GiB available. The real
QEMU process has UID/GID 999, no groups/capabilities and NoNewPrivs 1.
Coherent build 11 (`2b585188abc77069b167322f4b8fef059115fb01383646cb623d634e610f01cc`)
is rejected despite its wrapper exit zero: the conditional subshell masked
failed display checks while later compilation/image steps continued. Its log
retains both failures and must not be treated as a pass. The geometry fixture
assigned its independent visual dialog and draggable window the same default
GUI session, so correct new modality blocked the drag. The fixture now names
its independent session and preserves the exact position assertions. Independent
review also corrected nested restore/maximize visibility ordering and swallowed
middle-button release; their exact triggers have regression tests. The next
runner uses unconditional strict-error execution and stops on the first failed
step. Source and products from build 11 are not runtime acceptance evidence.
Strict coherent build 12 passes with explicit exit zero in
`production-filesystem-build-12.log`, from source
`02f07df5ffb8fa4fa2f039b4616082c66df659c6d744318e75554e9e0532eda9`.
Display host/ASan/UBSan/MC68040, complete userspace tests, kernel host/all/verify,
ROM and fresh image creation pass, with source fingerprints checked before
and after the build. ROM SHA256 is
`e0476d1ef5d91083f5043be8d81f414d333a8b2f94374a2740972a1e7185d0bb`;
image `production-filesystem-14-12.img` SHA256 is
`c2269714e8a48a0b82b8a8093e220b5cf076fdc16f10e168732099c50cf4ebc6`.
Actual desktop overflow/context, extended native CWD lifecycle and full
filesystem qualification are next. No production cutover has occurred.
Actual Atlas/desktop overflow passes in `atlas-overflow-14-12-1.log`:
all 176 icons remain reachable through scrollbar drag, wheel, Home and End;
DH0/RAM0 readonly grants, 257 UTF-8 entries, empty/missing/non-directory roots,
reflow, stale-incarnation refusal and process retirement pass. The same strict
job is now running the complete context/busy-modal gate; it is not yet accepted.
The combined UI job closes with exit 1 after Atlas passes. The context menu
opens and its observer verifies all 33 exact file holders, but no busy-list
render appears; selection delivery versus an earlier error dialog is not yet
distinguished. Temporary numeric Desktop diagnostics are prepared locally for
an isolated replay after the filesystem slot. They change no authority or
interaction behavior and must be removed after diagnosis.
Extended native gate 8 passes with source/products from build 12, including
CWD-only refusal with the exact parent shell PID, directory inode and
`/cd0/Folder` path, followed by successful removal after CWD release.
`production-filesystem-linux-first-8.log` also passes all ISO/remount/recovery
and durable-shutdown/readback checks with clean fsck. The strict combined
session is now running `production-filesystem-full-gates-1.log`; no full-suite
verdict is available yet.
The combined native/full-suite session closes with exit 1: extended native
lifecycle 8 passes, as do resolver, hostile-caller, raw HostFS, 400-operation
stress/offline fsck and hostbench. Semantics exits with command status 126
before any of its 30 cases run. The harness already builds `fssem` explicitly;
its actual executable/launch failure is being diagnosed rather than attributed
to filesystem semantics without evidence. Full-suite logs remain at
`/tmp/astra-fs-gates.wlQPhj` on Beast. Desktop's isolated numeric diagnostic
replay now owns the test slot; root release sources remain unchanged.
The isolated diagnostic build 2 passes after removing only its relocated
generated FreeType cache. Runtime replay closes with exit 1 and proves the
Unmount callback reaches the shared usage query, which returns
`ASTRA_ERROR_IO` (-9) before any busy-list paint. This is now a usage-query
diagnosis, not a context-menu delivery assumption. Temporary Desktop numeric
logging remains diagnostic-only and must be removed after the fix.
Accepted coherent build 12, native lifecycle 8, Atlas overflow, exact sources,
ROM/image/QEMU/provider and the failed full-suite logs are SHA-verified and
retained at
`/mnt/Documents/astra68/builds/production-filesystem14-20261014/coherent12`.
The separate diagnostic tree now owns a temporary kernel exec trace to capture
the actual packed-startup capacity rejection before changing that contract.
Production remains unchanged.
Desktop usage-query diagnosis identifies the actual layout refusal: the
dialog requests a 14-pixel font strike, but installed strikes are 11, 13 and
16 pixels. Zero fitted advance becomes `ASTRA_ERROR_IO` before painting.
The source fix uses the existing theme body font consistently for measurement
and rendering, with a real-font UTF-8 wrapping regression in the existing
Desktop layout test. Temporary Desktop diagnostics are removed. Focused
host/sanitizer/MC68040 checks and the complete actual busy-dialog replay remain
required before accepting the fix.
Focused font gate 1 passes normal, ASan/UBSan and MC68040 checks. Actual UI
replay 1 validates all 33 rows, matching text CRC and real drawing, then
rejects its own fixed-timer actor race: RAM0 was removed before modal readiness
was confirmed. The fixture now uses a WORK readiness handshake. Replay 2
closes with exit 1 before the modal because the new watcher command does not
complete its native oracle; that fixture failure is being diagnosed. Neither
replay is a complete UI acceptance pass.
The unchanged semantics replay with the temporary kernel trace confirms the
startup capacity defect: 37 capability records plus 31 argv entries occupy
4,040 bytes; 11 environment strings occupy 179 bytes and their pointer vector
needs another 48 bytes. The 4,267-byte result exceeds the shared 4,096-byte page
by 171 bytes. The rejection occurs before `fssem` runs. Diagnostic session
39387 closes with exit 1 after kernel/ROM build and verification pass; its ROM
and log are retained separately. Startup ABI 8 is being completed with separate
read-only vector pages through the existing owned VM publisher, retaining the
bounded 4 KiB packed-string input and atomic replacement/rollback contract.
Producer/consumer, validation, clone, capacity and allocation-failure gates
remain required.
Initial Startup 8 host gate 1 closes with exit 134. Its vector-page allocation
failure oracle exposes an existing essential-launch cleanup leak: physical
frames are released, but the empty protected-owner ledger remains charged.
The existing prepared-executable destructor now unprotects that owner after
release. Strict capability/payload geometry and POSIX validation order are
also being completed locally; no passing Startup 8 gate is claimed yet.
The new read-only state-preservation verifier and real ext4 corruption
canaries are queued in the separate system gate. Observation compares full
binary/empty attributes and actual fast/long/broken link targets; canonical
installation and journal recovery remain separate copy-only operations.
Installer state verifier gate 1 now passes with explicit exit zero in session
61726, retained at the separate system gate's
`build/linux-first/installer-state-verifier-1.log`. It includes two complete
actual ext4 installs, full binary/empty xattrs, corruption beyond attribute
display truncation, fast/long/broken/byte-target symlinks, hardlink identity,
metadata/content/default/addition refusals, CLI and no-follow input checks,
and explicit rejection of unrepresentable directory observations. Reference
and candidate hashes remain unchanged by observation. This qualifies the
verifier/installer seam, not production-data migration.
Startup 8 focused qualification now passes: session 38712 accepts full POSIX
host tests and Runtime/POSIX ASan/UBSan; session 87931 accepts the complete
kernel host suite and MC68040 all/verify, Runtime static/PIC/dynamic contracts,
POSIX shared contract, Supervisor host/sanitizer fixtures and native `fssem`.
All recorded input hashes remain unchanged. The final fixtures retain exact
allocation accounting and query the actual surviving self handle after failed
exec. These gates qualify the producer/consumer slice; a coherent ABI 8 ROM,
image and actual filesystem/UI run remain required. Temporary publication
timing instrumentation must be removed after the same-host comparison and
before final release qualification.
Coherent build 13 passes full userspace tests/all, kernel tests/all/verify,
ROM tests/verification, fresh-image production and all 5,746 source hash/mode
checks from source
`6cca621cdc29ce4b8f15f83f687fe4bf19d67869dfae6612db11e9a09d5f9ffb`.
However, its source-only build omitted the explicit kernel/ROM revision
environment: both embedded revision IDs are zero. These artifacts are retained
as diagnostic code evidence and rejected for release. The next build must set
the authoritative source revision through the existing generators and assert
the exact strings and nonzero numeric IDs before acceptance.
Actual session 46141 closes with exit 1: Startup 8 now reaches all 30 filesystem
cases, with 29 passing, including deep CWD fork/exec, E2BIG rollback and durable
saves. The owner-sessions case fails before the orchestrated recovery/resource
checks. Native trace shows the parent killing its cloned child during failed
fork-adoption cleanup, before the application's child session open. A precise
stage/errno diagnostic retains all quota and owner-isolation assertions; no
quota-policy defect is claimed before that replay.
Actual UI replay 3, session 30456, closes with exit 1 and identifies the watcher
fixture failure: `sleep` is not installed at the assumed command path. The
watcher spins and floods output instead of waiting for modal readiness; the
earlier fixed-delay actor also never slept. The status-zero answer and
`MODAL-WATCH-ARMED` are captured, but the following shell prompt is absent.
Source hashes remain unchanged. The fixture will use an existing native delay
tool before another complete UI replay. No production migration or activation
has occurred.
Identity-correct rebuild 14 passes kernel/ROM all, verification and tests from
source `e0fce4c73b8dfa8aa2f9837cda813c3cd5428ab8b9f542ef2485634566370e02`.
Both generated headers and actual binaries contain that exact revision plus
`-fs14`, with nonzero ID `0xe0fce4c7`; zero-ID artifacts remain SHA-verified in
`build/linux-first/production-filesystem-zero-13/`. Focused session 84741 closes
with exit 1 and identifies native handle admission before the intended VFS
quota: raw clients retain reply, scope and area handles against the public
255-handle namespace. The fixture will use control-only sessions and a shared
reply endpoint to isolate the VFS limit, retaining exact admission, cleanup and
independent-owner assertions. It also exposes a real POSIX adapter defect:
`getpid()` needs broker-reply resources and returns -1 under handle pressure.
The existing direct native process-info query provides self identity without
that admission; the adapter fix and real pressure oracle remain required.
Actual UI replay 4, session 97178, closes with exit 1. Canonical admission and
execution of the existing native `sbase-sleep` helper fix the watcher. The real
dialog passes all 33 exact rows, matching CRC, actual drawing and RAM-presence
checks, but End does not scroll after the viewport and outside-Desktop clicks.
The unchanged-source, nonzero-identity run isolates the next UI focus/control
routing diagnosis; it does not qualify complete safe removal. No production
data or activation has changed.
Joint diagnostic build 15 uses source
`bdef4fe612e48ce7ce4c846ad9cc6ad4e5a00002a69931e0c60bd22e032686d5`
with that complete source ID plus `-fs14` embedded and nonzero ID `0xbdef4fe6`.
Its kernel, ROM and fresh image hashes are retained in the build log. The outer
session 1143 completion was lost during interrupted polling, so no whole-pipeline
exit or complete gate acceptance is claimed. Focused owner replay 2 stops at
`native handle pressure accepted 0 status 4`: WORK lacks transfer authority,
so duplicating that grant did not exercise handle exhaustion. The corrected
fixture duplicates an owned transferable PORT_SEND endpoint, preserving the
native 255-handle boundary and the independent 64-session VFS quota. Actual
saturation and the full suite remain pending. POSIX `getpid()` now queries the
existing native PROCESS_INFO facility instead of admitting broker-reply handles;
normal, sanitizer and target adapter tests pass, while the pressure oracle still
requires the corrected guest replay. No native rights or quotas were relaxed.
Actual GUI trace 5 closes session 19485 with exit 1 against the exact build 15
ROM/image and unchanged sources. It proves that releasing a valid ScrollView
click emits ACTIVATE for control 2, and Desktop then closes the dialog on every
activation. The local correction dismisses only for its existing Close button,
control 5; shared control behavior is unchanged. The existing GUI regression now
checks viewport End/Home before repeating outside-owner click and scrolling
checks. Read-only Haiku Alert review confirms exact button-message dispatch.
Both temporary Desktop traces are removed; corrected-binary qualification is
required before accepting the fix.
The matching build 15 startup observer closes session 68875 with exit zero and
a matching durable exit stamp in `startup8-publication-1.{log,exit}`. Immutable
ROM/image/QEMU hashes pass. Two 37-capability, one-argument publications measure
2,392 and 1,603 QEMU virtual-clock-scaled units; six-capability observations range
from 256 to 551 units. The earlier short-launch baseline was 832 units for 37
capabilities and 194/197 for six. This records the correctness change's added
publication cost; it is neither physical MC68040 cycle evidence nor a speedup
claim. Temporary kernel publication tracing is now removed locally. Coherent
build 16 must qualify the combined changes with exact source identity, complete
userspace/kernel tests, actual GUI replay and existing filesystem gates. Its
runner records a durable source/status/phase exit stamp and retains the exact
live session until completion. Production release, state and kernel remain
unchanged; network mounts remain excluded and physical USB/CD tests unavailable.
Coherent build 16 now completes with an explicit failure status rather than an
unknown outer result: session 92002 and its durable exit stamp agree on exit 1
in `filesystem-gates`, from source
`5d79ffc2f5a46651e7cdbb15f56bbb549d2e0a78d6b011a85f898025362a5005`.
Complete userspace tests/all, POSIX sanitizers/target, kernel tests/all/verify
and ROM tests/all/verify pass. Generated headers and actual binaries contain
that exact source revision plus `-fs14`, with nonzero ID `0x5d79ffc2`. Kernel
SHA-256 is `84cc341265e56ba8d7bac80ad76cb7e1f62e51ee8a61ed0307e88f69fa9c5cea`,
ROM is `b249369961c7372da16ff6c4fb4e1e10ee8934067d661711b62906ae963199b9`
and fresh image 16 is
`4fa38fa127557aea3d7bb3faa6941a070a37e707a284c8f7d7c507f6508860cb`.
The focused pressure/quota replay passes with 185 duplicated native handles,
stable PID and complete handle return, followed by lane and clean-fsck checks.
All 30 filesystem cases then pass together, including a second pressure replay
with 164 duplicates. Budgets, flood/reset recovery, lane, resolver, hostile
caller, HostFS, 400-operation stress/fsck and host authority checks pass.
The remaining failure is the orchestrated loss probe's reopen assertion in
`/tmp/astra-fs-gates.ZkELjK/semantics.log`. Its message, "grant rebound to
replacement root errno=0", currently labels every status other than wire LOST
as rebinding and does not report the actual status. Existing HostFS recovery
compares retained volume/inode/generation and closes the retired native port;
raw PEER_DEAD may therefore be the correct observed boundary. Exact stage/status
evidence is required before changing that assertion or claiming actual
replacement-root access. Read-only Haiku VFS review confirms that retained
descriptors disconnect instead of retargeting. A fixture-only diagnostic is
being prepared; production authority and quota rules remain unchanged. All
5,746 source hashes/modes pass a post-failure verification and no stale remote
jobs remain. Corrected Desktop GUI replay 6 now owns the sole test slot against
these frozen build 16 products; the complete release is still unqualified.
GUI replay 6 closes session 35153 with exit 1 and unchanged source/input hashes.
The product correction passes viewport activation, End/Home and outside-Desktop
click followed by End: modal keyboard focus survives. The following wheel
assertion fails because the fixture left the pointer outside the viewport.
Display and CommonKit deliberately route wheel input by pointer hit; read-only
Haiku EventDispatcher/Window review confirms that contract. The fixture now
uses existing `qmp.point(800, 540)` before the wheel event, retaining the exact
scroll amount, Home, live-catalog and removal assertions. No product behavior
changed; a complete corrected replay remains required.
Diagnostic source 17 is frozen separately as
`9d8d41f4a21e33bb97215abd9119cc2430ab0a2d1d53bbf3d9a6db44a10c7744`.
It adds only precise loss-probe evidence and continuation documentation to
source 16; the later wheel-fixture correction is not in that private snapshot.
Focused session 31678 closes with exit 1 against the unchanged, SHA-verified
build 16 ROM/image and matching ABI 8 libraries. Its durable stamp agrees;
source and input checks both return zero. The strict diagnostic records
`status=8 stage=hello-send session=0 scope=0 replacement_inode=102656032`.
HELLO was rejected by the retired native endpoint before any session or scope
admission; no root STAT succeeded. This proves native PEER_DEAD, not a wire LOST
reply or replacement-root access. The fixture now accepts only zero-session,
zero-scope rejection at the exact typed boundary: wire LOST at `hello-status`
or native PEER_DEAD at `hello-send`. Every other status/stage and every
successful admission still fails. Unexpected success retains a read-only root
identity diagnostic. No product implementation, authority or quota changed.
This is diagnostic evidence, not a coherent source 17 release qualification.
The next joint source 18 includes that corrected oracle and the pointer-target
wheel fixture. It must pass complete filesystem gates, including the final
HostFS-kill recovery check not reached after source 16's provider-reset failure,
and the full GUI removal replay. No production state has changed.
Coherent source 18 is now accepted by the complete filesystem runner:
`c7b24fb19f53c25e405b779a1ae324e7a468b38bdbfcf084aabb9e65d1170e6c`.
Session 14475 closes with exit zero; its durable stamp agrees on source,
status zero and phase `complete`. Userspace/POSIX/kernel/ROM checks, all 30
filesystem cases and every orchestrated check, including final HostFS-kill
recovery, pass. Resolver, hostile caller, raw HostFS, 400-operation stress/fsck
and host-authority gates also pass with zero failures in
`production-filesystem-full-gates-3.log`; detailed logs are retained at
`/tmp/astra-fs-gates.pSlkvI`. All 5,746 source hashes/modes pass before and after.
ROM SHA-256 is `137f22a87ce4ea80bd113b780733eea84c0a65aa462711b60d7d25b468f87114`,
image 18 is `b802264dde5e6ae3aa815c6f802751e42141d062d5acfcda75353481705a4378`
and kernel is `dbaae6824cd1306bf90511271ed4096a9b0ed6bbba3c4816f24094c7568cfce8`.
The exact frozen export remains unchanged while the native/runtime gate batch
runs. GUI replay 7 closes session 88784 with exit 1 after all 33 holder rows,
matching CRC, real drawing, UTF-8/wrapped paths, modal focus, End/wheel/Home and
live RAM catalog checks pass. Canonical safe removal logs `desktop volume
unmounted` and removes the icon. The final shell observer then receives no
command: the preceding desktop context menu owns keyboard focus, but the
fixture sends `volumes --all` without reactivating Terminal. Read-only Haiku
Desktop focus review confirms that ownership. The fixture now uses its existing
`atlas.click(machine, 200, 140)` before that observer, retaining every catalog
and source-retirement assertion. No product code changes. Sources and inputs
remain unchanged throughout replay 7. A final source 19 freeze and complete
matching GUI replay are required after the source 18 runtime batch retires;
do not mirror the workspace into a running gate.
Native/runtime/SDK, staged release, stopped-state preservation and physical
kernel/runtime cutover still require their remaining gates. Kernel and runtime
rollback rehearsals must precede the final stopped snapshot. Runtime rehearsal
uses existing state/export overrides with disposable data; the final snapshot
captures the last predecessor boot. Preserve both the exact image and external
WORK/SOUND metadata before migration. Production remains unchanged; no network
mounts or unavailable physical USB/CD tests are claimed.
Source 18 runtime session 10903 closes with exit 1 in `terminal-full-vim`.
Native lifecycle passes all three ISO cycles, CWD-only busy refusal, RAM remount,
HostFS recovery and durable shutdown/readback. Atlas overflow also passes;
sources and five pinned inputs are verified after both phases. The normal-user
Terminal observer then receives EACCES while traversing the provider's private
exports, before command oracles run. Its two direct make calls now use the
existing non-root build-account adapter so a privileged metadata observer can
run without building or running QEMU as root. The existing readiness regression
exercises both delegated make calls through the actual CLI and retains build
failure propagation; Beast execution and remaining runtime checks are pending.
Provider permissions and hostile caller rules remain unchanged. No product
filesystem defect is shown.
The subsequent retention attempt closes with exit 1 before copying: Terminal's
workspace ROM refresh changed the generated kernel and ROM artifacts. All 5,746
source hashes and the original image/provider/QEMU hashes still pass. Original
build, filesystem and GUI receipts remain valid, but the original source 18
kernel/ROM binaries were not retained before refresh and must not be claimed as
archived. Preserve the changed artifacts separately with exact identities, and
use a pinned ROM outside the workspace refresh path for remaining diagnostics.
The private source 19 snapshot contains only the GUI focus fix and continuation
notes; it has not been mirrored or published. The next source 20 adds the
corrected Terminal build-account fixture and its regression gate. Remaining
runtime qualification must identify that diagnostic fixture separately from
the unchanged source 18 product binaries; final publication rebuilds every
producer with its final source identity.
Source 20 diagnostic session 93859 closes with exit 1 in `terminal-full-vim`.
The focused build-account regression passes on Beast, followed by exact source
and pinned-input checks. The privileged observer then boots Terminal and reaches
the command oracles without the private-export permission failure. Its first
root listing still expects obsolete `/ram` and a TMP link. The fixture now
checks canonical `/ram0` and mounted TMP directory kinds, migrates all eight RAM
paths together, and retains alias, protected-root, write/read/cleanup and
CWD checks. Read-only Haiku mount-coverage review confirms that a mounted root
and an explicit symlink have distinct metadata. No product source changes.
Source 21 must qualify the remaining runtime gates with the same pinned source
18 products, keeping diagnostic fixture identity separate from binary identity.
Source 21 runtime session 95630 closes with exit 1 at the first root-listing
oracle. Existing-Machine observer session 11813 then closes with exit zero:
complete WORK-captured `ls -F /`, `ls -l /` and `pwd` results each return zero,
with source and pinned inputs verified before and after. All independently
granted convenience roots report directories; only `/system -> /dh0` exposes
link metadata. The fixture now checks all 15 observed root names/kinds, the
explicit SYSTEM alias and the existing protected-removal/CWD/write/cleanup
contracts. Public namespace prose now makes that same distinction. No product
source changes. Full GUI replay 8 owns the test slot against source 21 fixtures
and pinned source 18 products; the next source 22 must wait for its actual
closure before mirroring or running the remaining Terminal/runtime gates.
Full GUI replay 8 now passes: session 85537 closes with exit zero and a matching
durable `complete` stamp. All 33 holder rows/CRC, actual rendering, UTF-8 paths,
modal focus, scrolling, live RAM updates, safe removal, icon withdrawal and ISO
source retirement pass. Source 21 fixture hashes and pinned source 18 artifacts
match before and after; final cached-receipt collection 706 also closes with
exit zero. These identities remain distinct; this is complete software UI
evidence, not a claim of a coherent source 21 release.
Source 22 runtime session 13620 closes with exit 1 after the corrected root
oracles, at the existing public `which status` pipeline assertion. Observer
73049 and receipt 930 close with exit zero and prove the command emits private
provider path `/status [0]` instead of `/commands/status [0]`, with all command,
source and pinned-input checks passing. The CLI now prints its already-qualified
public path rather than the mount-relative wire path, retaining union selection,
member index and errors. The failing integration expectation remains unchanged;
target compilation and corrected runtime qualification are still required.
Independent auxiliary session 17994 closes with exit 1 at Doom startup. All six
shutdown/restart/veto/menu gates and SDL video, render, image, mixer, font,
timer, thread and drawing gates pass, each followed by source/input checks.
Doom exits before QMP and receives no runtime acceptance. Separate policy
session 91456 closes with exit zero and durable `complete`: actual image
upgrade preserves policy, restart backoff works and the critical service exits
with its named halt diagnosis. Source 22 and pinned source 18 inputs remain
unchanged throughout these checks.
The bounded Doom startup observer, session 21952, closes with exit 1 and records
the exact admission error: QEMU cannot own the root-owned, mode-0600 audio
mailbox. Both display buffers already belong to the explicit QEMU account.
Source and artifact checks still pass after the failure. An ignored diagnostic
wrapper exposes the existing initial serial stream without changing shared
harness behavior, permissions or assertions. The remaining fixture correction
must share only explicitly owned audio resources with the configured unprivileged
QEMU credentials, including each caller's own private-directory traversal. No
production audio or provider permission change is justified by this failure.
The shared AudioHost fixture now grants only the configured QEMU group traversal
of its explicitly owned temporary directory and read/write access to its socket
and mailbox. NDK audio examples place those endpoints in their already-owned
outer fixture directory. Actual target-credential access and exclusion checks,
corrected Terminal integration and Doom runtime qualification remain pending.
Independent source 22 SDK qualification passes public header/API checks, HTML/PDF
documentation and extracted dynamic/static C/C++ distribution builds. Its fresh
storage kernel package 4 is compiling under the retained SDK/kernel session
12385; no package or release acceptance is claimed before actual completion.
All nine isolated release-tools gates pass with exit zero in
`release-tools-*-1.log`, including real FIT generation/full 1,344-module staging,
kernel transaction/rollback, immutable packages, publisher, image selector,
filesystem runner and unprivileged launcher. Package 3 remains a measured
checkpoint; the final changed header inputs require a fresh package and gate.
Durability review found
that the existing file-sync probe omits the parent-directory barrier and fatal
provider tests do not reread acknowledged bytes. The extended existing gates now
pass with exit zero in focused durability gate 2: host/sanitizers, MC68040 and
analyzer, sync-error propagation/recovery, and acknowledged-data readback with
clean fsck after fatal provider termination. Native atomic-save/parent-directory
sync and the POSIX probe still require the coordinated guest run.
The guest-block power-cut runner cannot qualify loss of Linux's page
cache and must not be cited as physical power-loss evidence.

Filesystem 13.2 / Kit 10.2 now provides distinct public read/write root brokers
and complete owned catalog snapshots. Root requests authenticate the requesting
process, require exact epoch/ID/generation, enforce configured owner budgets,
refuse private/system expansion and readonly writes, and return nondelegable
grants. HostFS retains canonical owner sessions separately from public grants.
Quota-storage construction now returns an owned area to every caller, so dynamic
owners can actually retire both mappings and handles. Shared volume wire records
live in a standalone ABI header used by QEMU and the SDK.
The complete Beast filesystem runner passes all 28 semantics, finite descriptor
and session recovery, quota flood, provider reset, HostFS restart, stress,
independent clean fsck and hostbench. Sampling drains submitted host work and
accounts for non-filesystem services explicitly. This is correctness evidence,
not performance acceptance. Exact source/artifacts/logs are retained in
`production-volumes-20261012/native-volume-root-launcher-1`.

Production launcher source now uses the confined provider, exact system-image
selector and processed udev. SoundFont seeding reuses the ordered resolver and
publishes fsynced files atomically without replacing existing entries or changing
their metadata. One Python process owner replaces shell numeric-PID supervision;
the old shell intermittently waited on live helpers after peer exit. Launcher-6
passes shutdown, restart, helper recovery and privilege tests without intervention.
Earlier interrupted/manually unstuck launcher runs are retained as failures.
Release recipes package provider/selector/process owner and the complete optical
kernel Image/module/DTB set. Deployment refuses an incompatible running kernel or
module set before changing selectors and no longer recursively changes guest-data
ownership. Profiler source attaches perf to the provider's exact QEMU peer.
Release/profiler contract gates pass; production activation remains pending.

Desktop and Atlas live-volume integration is being qualified. Desktop subscribes
before its snapshot, displays mounted volumes with labels or uppercase canonical
names, and launches Atlas with exact volume identity. Atlas acquires its own
readonly native root grant, checks completion against the catalog and withdraws
results on loss; system browsing keeps explicit DH0 authority. Transient catalog
failures retain state and retry without blocking window input. Dynamic HostFS
metrics now use full incarnation identity so replacement media cannot collide
with still-retiring owners. Native UI builds, host/sanitizers, NDK HTML/PDF
(609 pages) and actual guest UI pass. `volume-ui-guest-3.log` covers DH0/RAM0
exact roots, 257 entries, UTF-8, empty/error states, input/reflow, process
retirement and stale-incarnation refusal. The second UI oracle mistook the
expected stale-launch error for a later valid launch failure; the corrected
oracle checks the actual nonzero command status and runs that negative case
after the positive views. Whole-mount recovery now uses exact mount identity
and inode without requiring unsupported birth-time metadata; subdirectory
recovery still requires its node generation. Host/sanitizers/MC68040 pass that
contract. Remaining paused block/qualification launchers use explicit non-root
test credentials, and the actual block gate passes. A final persisted boot /
shutdown check is in progress. Live process namespace, native lifecycle/
usage/lsof/ISO controls, detailed busy dialog, desktop overflow handling, long
path/name contracts and complete production cutover still remain. Network work
and physical USB/CD acceptance remain excluded/unavailable respectively.

Native metadata is now available through a distinct `VOLUMES` capability.
HostFS hosts one versioned native catalog/subscription port; applications cannot
gain data or mount authority from it. Provider and native clients share one
keyset/epoch/sequence validator. Subscription ownership comes from the kernel's
sender identity, uses the configured owner-session budget, coalesces a full
one-message queue, and retires closed endpoints. Filesystem 13.1 / Kit 10.1
exports the client API; Terminal forwards its metadata capability to commands.
`volumes` lists mounts and `volumes --all` includes devices, media, sources and
private bindings. The first semantic oracle wrongly counted WORK/SOUND bindings
as mounts; the corrected oracle checks their distinct kinds and exact names.
All 27 semantics, finite descriptor/directory/session resources, flood, direct
lane attribution, provider reset, HostFS restart and independent clean fsck pass
in `build/linux-first/volume-catalog-semantics-2.log`. Real desktop/Terminal
boot also passes the new commands, persisted system/export writes and normal
shutdown. NDK headers, HTML and PDF pass. Source/artifacts/logs are archived in
`production-volumes-20261012/native-volume-catalog-1`.
Standalone display, mailbox and boot-stage launchers now reuse `Machine`;
paused mailbox tests explicitly drop root privilege. Final stricter UTF-8/graph
validation and launcher checks are still being qualified. Data grants, control,
usage/lsof, live namespace, desktop/Atlas, long path/name contracts and complete
production launcher/release migration remain required. No production activation
or network mount work occurred.

Processed libudev discovery now subscribes before enumeration, reserves
Linux-mounted devices, containing disks and recursive slaves, and maps
disk/partition observations into the existing device/medium/source catalog.
Root/system host disks and loop-backed native image sources cannot acquire
physical guest authority. Per-device sequence watermarks reject stale events
without discarding another udev worker's out-of-order completion. Pending
partition observations wait for their processed parent. Media/partition changes
retire the previous exact generations; device removal retires descendants.
USB supported filesystems automount writable unless the device is read-only;
optical media automount read-only. Fixed volumes remain explicit mounts.
Manual unmount is not undone by automount. Discovery and probe/mount resources
remain bounded; one canonical provider reaper now handles live loss and shutdown
using real unmount/release, retaining busy and pending Linux references.
Host, sanitizers, pinned AArch64 and actual DE25 provider/discovery gates pass,
including 1000 insertion/removal cycles, actual processed inventory, a disposable
read-only optical block mount, stale authority and busy-directory retirement.
Physical USB/CD hotplug and eject remain unqualified without attached devices.

Desktop test `Machine` now uses the confined provider, not a guest image writer.
Root observers launch QEMU with explicit non-root accounts and inspect preserved
metadata; image builds run as the real build account. The hostile-caller
`astra-qemu-root` uses an installed root-owned provider, copies images within its
existing locked 4 GiB/free-space bounds, disables discovery, publishes only
observation exports, and verifies real backing retirement before write-back.
All 21 helper/security/cleanup checks pass on Beast; the matching helper and
provider are installed there only. Root-owned 0600 file data and metadata survive
native access and write-back. All 26 guest filesystem cases and finite resource
recovery pass with independent clean fsck. The full recovery run also passes
flood, direct-lane attribution and provider reset, but exposed a real HostFS
restart failure: SYSTEM was excluded from publication ownership and its old
Supervisor handle survived. A coordinated fix is being qualified: all service
publications share ownership, SYSTEM borrows the owned client for aliases,
retirement removes only that authority's ordered assign members, and bootstrap
and HostFS recovery use one read-only provider loader before releasing the claim.
HostFS metrics now retain aggregate additive counters and expose each volume's
own peaks/last failure; independently timed peaks are not summed. Metrics and
assign-retirement host/sanitizer gates pass. Fresh ROM `recovery-3.log` passes
publisher death, the canonical bootstrap restart, dependent Desktop recovery,
fresh Terminal and WORK writes. The test now waits for the new Desktop ready
event before clicking; its earlier pre-arm timestamp and old icon selection
could race legitimate service recovery. `semantics-4.log` exposed a separate
global-counter attribution race with background sampler I/O. The final
`semantics-5.log` uses authenticated QMP peer credentials and exact fixture
inode/descriptor observations: 1000 held files and 200 directories return to
zero, and 16 idle control sessions retain no new directory descriptor. All 26
cases, finite resources, full-quota flood, direct-lane attribution, provider
reset, HostFS restart, fresh Terminal and independent clean fsck pass together.
Root-observer diagnostics refuse symlink leaves; its immutable ROM copy is
completed before lending ownership of the peer's temporary directory.
One exact image/window selector now serves image publication and launchers;
the installed selector refuses symlinks, ambiguous MBRs and out-of-file windows.
The updated helper again passes all 21 security/cleanup tests (`launch-2.log`).
Development evidence is in `build/linux-first/discovery-*`, `launch-1.log`,
`semantics-1.log` through `semantics-3.log`, and `recovery-build-*` on Beast.
Production release and kernel remain unchanged. Production runner/release,
remaining standalone test launchers, public live namespace/control/usage,
`lsof`, ISO control, desktop/Atlas and long path/name contracts remain required.


Current Linux-first cutover slice replaces Supervisor's guest ext4/block reader
with a read-only canonical provider session and makes static HostFS the first
service. One allocated volume-owner shape replaces duplicated WORK/SOUND
services and covers dh0, work, sound, ram0 and tmp. Startup publishes these
ports; RAM policy is now RAM0, and temporary/work exports are no longer SD
subdirectories. Initial kernel launch no longer grants block write authority.
The retired comparison-only bootstrap reader is deleted.

Beast host/MC68040/Supervisor/HostFS builds, sanitizer readers/config validation,
VFS tests/sanitizers and ownership boundaries pass in `build/linux-first/`.
Image contract tests now match runtime 1.17 and the native startup layout.
Real boot now reaches desktop, Terminal and zsh. System-root reads and writes
through HOME, RAM0 and TMP pass. Retained failures exposed duplicated LIBS
prefixes after grant narrowing, oversized libc reads beyond the host channel's
2 MiB capacity, a public connector that could not accept scoped channels,
empty command-config roots, and a full eight-scope channel binding cache.
Canonical namespace grant assembly, payload negotiation/partial reads, the
scope-aware public connector, actual private config-directory provisioning
and channel-cache rotation address those contracts. Filesystem is now 13.0.1,
Configuration 1.0.1, System 3.3.1; Runtime Kit 1.17.1 packages unchanged runtime
1.17.0 with System 3.3.1. `boot-8.log` passes the real desktop/Terminal/zsh boot, dh0 reads, HOME/RAM0/TMP/WORK
writes and ps, followed by native shutdown status 88. Independent e2fsck and
debugfs confirm clean system image and persisted /home bytes; external WORK
bytes match. This qualifies the Linux-first software bootstrap slice, not the
remaining removable integration or complete production migration. Evidence is archived
under `production-volumes-20261012/linux-first-1` with source/artifact hashes,
all retained failures and passing boot-8/boot-9 logs. Final host/sanitizer,
MC68040, kernel, ownership and image-contract checks pass. No stale test/build/
QEMU processes remain.
A root observer now inspects root-owned logs without changing their metadata;
provider still runs every QEMU thread as an explicit non-root UID/GID with no
groups/caps and no-new-privileges. Earlier fixture errors (ungranted /startup
path and non-root cleanup of root-owned logs) are retained separately from
product failures in `build/linux-first/boot-1.log` through `boot-7.log`.

Test and production launch helpers still need coordinated provider migration;
no old HostFS-root environment or guest image writer may ship with Host 2.

The earlier [network assessment](NETWORK_FILESYSTEM_REQUIREMENTS.md) is deferred.
The latest user instruction excludes network mount work from this completion.

Production volume implementation is now active under the
[coordinated plan](superpowers/plans/2026-10-12-production-volumes.md). The user
adds safe **Unmount** in removable-volume context menus, detailed process/object
usage dialogs, `lsof` over that same native usage query, and read-only ISO file
mounts alongside physical optical drives. Image sources retain their backing
file/mount dependencies; removal must identify dependent mounted images.
Linux provider/QEMU integration is in progress; native live namespace and
Desktop integration remain pending. `catalog-1` and
`foundation-1` pass lifecycle/loss/stale completion/rollback/metadata/snapshot/
budget tests on Beast, sanitizers and the pinned AArch64 sysroot.
`probe-images-1` passes real ext4, ISO9660, UDF and unformatted-image probing,
read-only-source checks, cancellation, deadlines and child retirement on both
host and AArch64. `mount-images-5` passes real Linux Rock Ridge, pure Joliet
and UDF mounts in an isolated namespace, UTF-8 contents read by an unprivileged
consumer, mount restrictions, busy refusal/restoration and verified loop detach.
An extra loop opener causes pending cleanup with retained charges, followed by
successful retry. `board-images-3` repeats those checks on the actual DE25,
with UID/GID 999 consumers, verified binary/image hashes, preserved selected
release/services and disposable fixtures only. A root-owned mode-0600 fixture
also proves authorized descriptor lending can preserve owner/mode while UID 999
reads/writes through the descriptor. This is a primitive, not the native broker.
Preserved `/home` and `/local` are UID/GID 0; direct UID 999 path I/O would break
that data. Rooted opens and permission-sensitive mutations must use the confined
provider and existing resolver, with native grants enforced, before cutover.
The provider core now implements authenticated private control/path packets,
random provider epochs, private bind-mount export authority, bounded FIFO probe
admission, RAM aggregate limits and read-only image leases. Existing writers
refuse image admission; lease breaks retire source/mount generations. Actual
loop detach and failed-mount cleanup retain resources until confirmed. The
ordered resolver is reused for privileged opens and path mutations; metadata
ownership and modes remain unchanged. `provider-core-2` passes clean-source
host/sanitizer/pinned-AArch64 gates and native DE25 execution with UID/GID 999
consumers, including same-inode/outside-mount descriptor refusal, stale epoch
and generation checks, paginated catalog/root lending, tmpfs limits, queued
probe cancellation, image dependencies and resource return. Its source hashes
match before/after the build, transferred hashes match, and selected board
release/services remain unchanged. Provider test binary SHA-256 is
`82a22e25162f16b39522d6fa35e2d1a023fd9fcf04f4e56bcbdd1553b63f5449`.
This is still a Linux core/library with exercised private transport, not a
deployed discovery/control daemon or a QEMU-integrated native facility.
`provider-async-1` now qualifies bounded mount/unmount worker processes and the
inherited-endpoint service loop. Parent owns loop/backing/target before fork;
workers retain only the operation's descriptors. Catalog snapshots remain
available during mutation, source loss cancels/invalidates mounting work, and
actual protected-target reconciliation handles a lost acknowledgement after
Linux commits. Quiescence restores usable state only with verified root
authority. SIGCHLD/lease notifications and precise probe deadlines wake the
service loop; cancelled uninterruptible children are not busy-polled. Fresh
host/sanitizer/pinned-AArch64 and actual DE25 gates pass with identical source
hashes across the build and unchanged board release/services. New provider
test SHA-256 is
`382f90b85d1e4a9184d92dd1f804d7fd894bfee283ada3ceb96bdb83e1d9d2a8`.
`provider-launcher-3` now qualifies the actual root launcher and private QEMU
bridge on Beast and native DE25. Exact ext4 image/window/UUID selection has an
exclusive write lease and loop-retained flock before read-only probing. The
launcher mounts `/dh0`, approved private bindings and bounded `/ram0`/`/tmp`,
then execs its peer without inherited storage descriptors, groups or caps and
with no-new-privileges. Source lease loss stops the peer before actual cleanup.
Rooted opens/mutations, directory names/confinement and sensitive readdir metadata
use the shared provider/resolver; QEMU owns canonical descriptions and data I/O.
Path operations share the bounded mount worker owner. A blocked FIFO leaves
catalog queries responsive; loss cancels it without lending a stale descriptor.
Workers have parent-death handling and remain charged until actual reap.
Fresh host/sanitizer/pinned-AArch64 gates and actual UID/GID-999 DE25 QEMU pass
native path/grant/scope/description/directory checks and twelve provider resets.
Root-owned mode-0700 directories and mode-0600 files enumerate/read/write without
metadata changes. Real shutdown, fsck and independent image inspection verify
persistence. Source hashes, transferred hashes and production services match.
Launcher SHA-256: `1ded0c1d22c2b333a78e0c21915e537cce643b65580a67825410e147c984cdda`.
QEMU SHA-256: `208499bf1af198084a33fac73716a174f4e3c27af67561da2cf0acfd48b79ff0`.
This is a private bridge, not a booted production migration. Guest bootstrap,
storage and fixed volume sessions still need coordinated cutover. QEMU source
removes `ASTRA_HOSTFS_ROOT`; do not pair it with the old launcher or publish a
release until all native bootstrap/HostFS/launch/test/release consumers migrate.
Processed udev, dynamic native catalog/control/usage and UI/tools remain required.
The unreleased native catalog now projects one shared versioned record model,
with no Linux paths, descriptors or private mount IDs. Host 2 / command record 4
replaces fixed root selectors with exact 64-bit epoch/ID/generation references.
Whole-mount authority retains no Linux descriptor; exact subdirectory and open
directory objects do. STOR 37 / Filesystem ABI 13 / Kit 10 carry full-width native
volume identity, and runtime 1.17 supplies canonical bounded byte search.
Host protocol gates pass catalog visibility/refusal, stale references, rooted
scope/description/path behavior, twelve resets, retained-directory retirement,
single-worker scope revocation/orphaned opens, descriptor/directory/owner budgets,
root0700/0600 data, real shutdown, fsck and persistence. C/MC68040 runtime, VFS,
Supervisor, HostFS and hostbench builds pass; kernel host and target gates pass.
Fresh `native-volume-abi-3` repeats those gates on Beast and actual DE25 QEMU
with UID/GID 999, no groups/caps and no-new-privileges. Source hashes match
before/after; transfer hashes and selected production state match. Standalone
volume-header C++/MC68040, complete installed NDK C/C++ header contract, HTML
and PDF gates pass. AArch64 QEMU SHA-256:
`5291b0642aebdfa4720acb54cb5ec2e1ed50b05c51f2564b7bc5b11f8b18317d`.
The private provider binary remains
`1ded0c1d22c2b333a78e0c21915e537cce643b65580a67825410e147c984cdda`.
This does not finish Linux-first bootstrap, public native catalog service/live
namespace, usage/control, UI/tools or production launch/release migration.
The new complete optical kernel build uses a distinct release name and output
tree, rebuilding Image, all standard modules, Astra graphics/capture modules and
DTBs together. First build reached complete Linux products but exposed a fixed
relative include path in Astra's module recipe. That path is now an explicit
matching-source argument. Fresh build 2 completes release
`6.12.11-astra-storage1-gd7d192a9ddd9` with 1344 matching standard/Astra modules,
full Image and DTBs. Source inputs and kernel source stay unchanged; all
vermagic values and package checksums verify. Image SHA-256:
`f8b42ca62f93ba7d3d83caeb001cefdcf1d5815fc5d3f715b687d56151b81890`.
Package manifest SHA-256:
`994ea6f9ea12e8c4b23210f81798b431e30d133b079615e430e60c9ac5271368`.
Products are in `build/de25-kernel-storage/package-2` on Beast. No kernel
installation or production activation has occurred; physical boot/graphics/
storage/HDMI and optical checks remain.
Native usage queries and UI/tool integration remain unimplemented. Evidence lives under
`/mnt/Documents/astra68/builds/production-volumes-20261012/`.
No production storage cutover or board activation has occurred. Fresh board
inspection retains release `0aa2d713`, Linux `6.12.11-gd7d192a9ddd9`, all three
services active and only the internal SD attached. The user confirms USB data
and optical hardware are not yet available; physical hotplug/eject gates remain
pending. Processed discovery, native live namespace/usage/tools, storage
cutover, desktop controls, matching CD-ROM kernel and full release gates remain
required. The latest request explicitly asks to finish provider, namespace, UI
and tools together; that integration is not finished by this core checkpoint.
Performance qualification stays deferred.

Current integration slice: shared right-click context menus in Interface 5.4
and System 3.3, with GUI wire 20 / event 8. One common menu model replaces the
private ASTRA dropdown painter and input walk. Native window ownership,
immutable bounded snapshots, correlated mandatory results, keyboard/pointer
press ownership and queue-loss cancellation support Desktop's existing Open
command for DH0 and application icons. Host/sanitizer/analyzer/MC68040 and NDK
header/HTML/PDF gates pass. Release `0aa2d713` passes exact guest menus, Atlas,
desktop, four power actions, all 111 Terminal commands, all 26 filesystem
cases/recovery/fsck, bit-exact audio, Doom and 15 packaged NDK examples. It is
selected on the board with previous menu release `e7023a1b`, preserved user
state, unprivileged QEMU and no new panic. Physical menu/app/Atlas/About input,
retirement and restored pixels pass; 10,000 model actions average 1.947 µs
against 12.5 µs. The existing bitmap cache now compares a 16 KiB committed
paint snapshot across popup lifetimes, avoiding repeated GPU painting while
rejecting different labels and uncommitted repaints. Default RFB socket timing
remains frame-quantized; an explicit TCP_NODELAY probe measures 33.128/32.969 ms
open/close medians. These diagnostics do not establish an end-to-end cache
speedup or the internal two-frame feedback budget; no waiver is claimed.
Final correctness/semantic-action evidence is under
`/mnt/Documents/astra68/builds/context-menus-20261012/`. Flat menus are the
current scope; submenus, app menu-strip publication and broader Atlas operations
remain later work. See [menu continuation](HANDOVER_2026-10-12_CONTEXT_MENUS.md)
and the [common-menu plan](superpowers/plans/2026-10-12-common-context-menus.md).

Work order reaffirmed by the user: make it work, make it right, then make it
fast. Complete functional correctness and data-safety gates before resuming
two-frame menu latency, long-menu timing or broad filesystem performance
qualification. Existing failed or unqualified timing results remain recorded;
deferring measurement does not waive their budgets or claim acceptance.

Active correctness priority: finish the remaining production filesystem phases, including
Linux-owned `/dh0` bootstrap/storage, numbered fixed/USB/optical/RAM volumes,
mount/unmount/safe-remove/eject, live native namespace publication, and labeled
desktop volume icons opening Atlas. The user chooses writable automount for
supported USB filesystems and authorizes the CD-ROM kernel work. The
[production volumes design](superpowers/specs/2026-10-12-production-filesystem-volumes-design.md)
now guides implementation. The initial native Atlas `/dh0` slice is accepted on
DE25 release
`5bc3bbeae347c964d73971ec12cfbb8036608e0a26d503bc3dc187ddae62849e`:
Desktop checks the real DH0 binding, launches Atlas with native arguments, and
Atlas enumerates all batches into a read-only root icon window. Shared AICON
surface construction is in Graphics library 2.2; retained geometry and passive
ScrollView keyboard focus are in Interface library 5.3. Host tests/sanitizers,
MC68040 build/analyzer, native DH0/257-entry/error/scroll/resize/retirement gates,
and desktop applications pass. The full Terminal gate exposed an existing
process-reference validator omission: queued IPC process handles still own
references, but only table entries were counted. A focused regression fails
before the shared detached-handle query fixes that accounting; the corruption
check remains intact. Kernel host/target verification, all 111 Terminal commands
and all 26 filesystem semantics/recovery/fsck cases now pass in `kernel-green-1`
under `/mnt/Documents/astra68/builds/atlas-dh0-20261012/`. Exact staged Atlas,
desktop, bit-exact audio, Doom and full filesystem gates pass in `release-2`,
including bootstrap match/mismatch/absent and unchanged ROM/image hashes. The
bootstrap gate now captures its early verdict before desktop/Terminal logs can
wrap the bounded trace ring; a focused regression preserves that check.
`deploy-1` archives the stopped previous disk and verifies `/home` and `/local`
contents/metadata survive migration. QEMU remains UID/GID 999, without groups
or capabilities, with `NoNewPrivs=1`; all three services are active with zero
restarts and no new panic. `board-2` verifies real DH0 double-click, 16 named
entries (15 folders and one file), fresh physical pixels, process retirement
and an identical restored desktop. See [Atlas continuation](HANDOVER_2026-10-12_ATLAS.md).
Previous release is `36791f1e`; the FPGA remains `b61dbda8`. Read-only board inspection
confirms the current Image lacks `sr_mod`, despite installed ISO9660/UDF modules,
so optical support needs a matching rebuilt Linux Image and modules. Initial
Atlas is narrowed to one root-directory window showing named file/folder icons,
with real native enumeration and loading/empty/error/loss handling. Existing
layout/scroll and necessary shared icon presentation suffice; the broader Atlas
browser and collection suite are deferred. No USB storage or
optical drive is attached for physical qualification. Existing correctness and
failed performance checkpoints below remain unchanged; no budget is waived.

The retained app-latency checkpoint, now the previous release, is
`36791f1e24e86a94856446a7389392127f6e6db5fe831d5bd8d3cc88cc97f175`,
qualified on DE25 with QEMU UID/GID 999, no groups or capabilities, and
`NoNewPrivs=1`. Audio.kit 3.0.0 / `pcm.library.3` replaces ABI 2 throughout
the current tree. MIDI activity queries now submit and collect the existing
correlated reply without blocking the SDL audio callback. The recurring
35–45 ms producer stalls are gone: a 40-second physical trace has zero write
stalls over 35 ms and zero audio faults, versus 47 stalls before the fix.
Twenty physical gunshots across two Doom launches also have zero audio faults;
the user confirms that gunshots now track muzzle flash through monitor/TV
speakers. This is listening acceptance, not an instrumented acoustic measurement.
Interface Gallery no longer runs its nine benchmarks on normal launch; repeated
physical launches finish in roughly 0.2 seconds instead of six seconds.
An empty futex queue lifetime defect found during Doom launch is also fixed
before owner-page reclamation. Kernel host/MC68040 verification, native PCM
sanitizers/contracts, the full guest app/Terminal/power/filesystem gates, and
packaged NDK documentation/examples pass. Runtime, audio and remote desktop
remain active with zero restarts and no new panic. The stopped previous disk
was archived and `/home` and `/local` contents/metadata survived migration.
See [app latency continuation](HANDOVER_2026-10-12_APP_LATENCY.md) for exact
evidence, retained failed explorations, and qualification limits.
The Doom quit prompt now reads “PRESS Y TO QUIT TO ASTRA.” The target binary
has the new prompt and no old DOS prompt; fresh guest desktop/audio/Doom and
filesystem gates pass. Its QEMU and audio daemon match accepted audio release
`8dfdbdaf` byte for byte. A physical capture verifies the branded confirmation,
then F10/Y/Space closes the actual Doom process and restores the desktop.
Capture, services and panic evidence are in `board-quit-2`; user-state
preservation and unprivileged credentials pass in `deploy-quit-1`.
Broad filesystem performance acceptance remains open.

The active filesystem continuation is the `fs-phase1` checkpoint containing
Tasks 5–12 and final review fixes. Git history records its committed identity;
the deployed source manifest and release identity are recorded below.
Final review found four additional
correctness gaps: joined-assign opens bypassed namespace shadowing, two-path
HostFS operations discarded namespace continuation, native directory-relative
APIs lacked filesystem context, and the root mount helper accepted UID 0.
All four fixes pass current-source requalification: all 26 guest semantic
cases, 1,600-operation stress, raw provider confinement, VFS/POSIX host and
sanitizer suites, NDK headers/docs, and all 31 integration gates including
111 Terminal commands, six power actions, desktop applications and events.
The isolated AArch64 provider passes as UID 65534, including write bounds,
recovery and memory limits. Evidence is under `review-target-1`,
`review-integration-1` and `review-de25-provider-1` in the performance directory.
The native directory-relative API now takes filesystem context, with
Filesystem ABI 12.0/Filesystem.kit 9.0.0 and STOR 36. Host remains 0x00010015.
Native-16, on the preceding ABI 11 source, passes all 26 semantic
cases, recovery/admission/bootstrap checks, host and sanitizer suites, target
builds and NDK contracts/documentation. The current atomic write-bound contract
also passes isolated unprivileged DE25 provider qualification. Integration-1
passes all 31 gates, events, NDK/docs and provider coverage, and exits zero with
all 164 source hashes unchanged. Those retained checkpoints do not qualify
the new ABI 12 source; the review checkpoints above do. Task 12b and Task 13
performance qualification remain open.
See [the filesystem continuation](HANDOVER_2026-10-10_FILESYSTEM.md).

The filesystem and visual qualification release was
`194b9202da7eb0ff61a05ad03633bd63f687ac7afce5fe05c08601fc357af249`,
built on Beast and deployed with the ABI 12 filesystem guest,
worker-only resolver optimization and refreshed SDL icons. Its own staged
ROM/image pass all 26 semantics and recovery/fsck before deployment.
The previous release
`299a6ade22eedddce3ae4db7f7cf8d15f08efa9e81be394649512ab4aa60f3a8`
holds the privilege-corrected baseline; its QEMU, ROM and immutable guest image
match original baseline `1d7137c1adfdec99518e65d1651e99846970ae0c3296852ab4cff93957032b1c`
byte for byte. QEMU runs as
`astra` UID/GID 999, with no supplementary groups or capabilities and
`NoNewPrivs=1`. The launcher drops privileges before executable startup;
the input/affinity helper runs as the same user. Root retains display/audio
hardware and power duties. The existing Linux arena driver exposes a bounded
front-panel page, so QEMU cannot open `/dev/mem`, the graphics arena or capture
device. Physical permission/mmap bounds tests pass. Desktop reaches stage 8;
runtime, audio and remote desktop are active with zero restarts. The privilege-only
baseline update preserved its stopped runtime disk byte for byte; the ABI 12
release uses its own coherent guest image. HostFS data remains in place.
The preceding ABI 11 release's stopped mutable disk was archived and its
checksum verified before normal retention removed that release/state.
Current deployment and guest evidence: `review-release-1` and `review-deploy-1`.
Evidence is on Beast under
`/mnt/Documents/astra68/builds/fs-phase1-performance-20261011/runtime-user`.

Ten SDL desktop icons render as native instrument glyphs in all three
strikes. Physical DE25 desktop and responsive Doom gameplay captures appear
in the README from release `194b9202`, with provenance in `docs/images/README.md`.
Fresh gameplay changes after a keyboard turn; normal quit restores the identical
desktop. Lossless conversion, installed identity and unprivileged credentials
pass before/after capture in `review-visual-2`. All 14 physical
benchmark workloads complete on both releases; the first target pair establishes
a diagnostic baseline, not qualification.
Five-run interleaved performance measurements are complete; acceptance fails.
The benchmark
harness must preserve each image's own ABI and use matching ELF trace
catalogs; early failed attempts are retained. A measured worker-only Linux
`openat2` shortcut in the canonical resolver passes raw confinement/race and
sanitizer tests, then the five-pair stat/read probe budget on the preceding
ABI 11 guest. Full workloads
still fail the unchanged budget, chiefly spawn median (1.56× baseline) and
several latency tails. Evidence: `worker-path-1` and `worker-path-five-1`
under the performance directory. Final host five pairs test `194b9202` in
`review-qemu-five-2`: all 10 workload/fsck and 10 probe executions pass, with
immutable hashes unchanged. The stat/read probe passes (median/p95 ratios
0.980/1.006 and 0.929/0.863). Full workload comparison fails: spawn median
1.73×, cold sequential-read p95 1.23×, random-write throughput 0.867× and
several open/stat metrics exceed the unchanged limits. Physical five pairs
also finish under `review-de25-five-1`: all 14 workloads complete on both
releases and all runs exceed the unchanged 20 MB/s held-miss floor. Full
budget fails on 13 workload families: warm-open median 3.512×, readdir
median/p95 2.592/2.603×, append p95 2.487× and spawn median 1.401×, among
others. One thousand held files and 200 held directories use one fd/object,
3.20 and 11.52 KiB RSS/object; descriptors return to baseline after close.
Conservative configured provider allowance remains 56 MiB within 64 MiB.
The runner restores `194b9202`; both strict comparisons end BUDGET FAIL.
All ratios and evidence locations are in the filesystem continuation.
Performance acceptance remains open. No budget waiver is accepted; further
tuning stops at this measured failure, as Task 13 requires.

## Shutdown implementation and retained board checkpoint

Graceful shutdown and restart are in progress. The retained immutable DE25
software checkpoint is
`463154c7e4410ce4a4a5cecd07a28f80c34932135acda0ee8b77da33ececa870`.
At that checkpoint the installed release verified byte for byte, the running
QEMU matched its selected executable, the guest reached stage 8, and Astra, audio, and remote
desktop services started. A live authenticated RFB capture shows the ASTRA
menu under its unboxed trigger, one divider between About and the paired
Restart/Shut Down actions, and inactive File/Edit/View headings. Terminal
opened from its desktop icon and reached the zsh prompt. The board's `ps`
printed its header and process rows; `ls /`, `which ps`, and a missing `/proc`
lookup returned to the prompt. Twenty-five further `ps` runs completed
(median 335 ms). A target-code audit identified an MC68040 GCC atomic-exchange
retry returning a stale previous value; the common NDK mutex now uses an
explicit compare-exchange loop, with deterministic normal and contention
regressions. That board checkpoint
did not exercise the Restart or Shut Down actions. A tested,
PID-1-only shutdown syscall distinguishes successful guest shutdown from faults
and requires all other processes to have exited. The DE25 launcher requests
Linux poweroff or reboot only on QEMU's distinct clean-exit codes.
VFS now has a generic mounted-filesystem shutdown contract: release sessions,
flush (a no-op when the backend does not need guest-side flushing), then
unmount. Ext4, RAMFS, and HostFS have stop handlers; ext4 drains its journal,
disables write-back, unmounts and flushes the block device. HostFS closes its
guest sessions; Linux owns its final poweroff flush. Positive and negative host
tests and MC68040 builds pass.
The process shutdown wire contract now defines correlated READY/CANCEL replies
through a private reply port. READY alone is insufficient: a managed
participant must also exit with status zero, while missing or malformed
replies fail closed. Unmanaged processes are killed at Supervisor request.
Managed processes now create private lifecycle receive ports and publish their
senders in startup-ready; the Supervisor records the declaration. Terminal
waits for a clean zsh
prompt and no other session jobs, then asks zsh to exit normally. Storage,
RAMFS, and HostFS implement the request and flush/unmount before READY. A
reverse-order Supervisor stop sequence passed Beast-hosted QEMU boots from
the current source: the `shutdown` CLI ran in Terminal and QEMU exited 88;
an edited/busy Terminal delayed shutdown until it returned to a clean prompt.
The desktop ASTRA menu now has Shut Down and Restart actions. A Beast-hosted
QEMU run of `restart` exited 89, and a busy Terminal delayed that exit until
the shell returned to a clean prompt. QEMU pointer tests clicked both menu
actions and reached their respective 88/89 exits. The previous NDK window
validator rejected every system action except About and killed the desktop;
it now accepts the defined actions and rejects unknown values. The QEMU action
tests passed before the above DE25 deployment.
Remaining shutdown obligations are cancellation recovery after partial service teardown
and user-visible shutdown consent/status.
See `docs/SHUTDOWN.md`.

The opening continuation status is authoritative. Retained measured checkpoints
identify their source and do not replace the current release identity. Git history
holds superseded board, processor, benchmark, and milestone records. The platform is **Astra 68**, its
kernel is **Axiom**, and the user-facing system is **Astra OS**.

## Active machine

- The DE25-Nano attached to Beast is the only production target.
- Its HPS serial console is the stable by-id device
  `usb-TERASIC_DE25-Nano_TRWJGOUZ-if02-port0` (currently `/dev/ttyUSB0` on
  Beast). The adjacent `if03` port is not the console; scripts must use the
  by-id path rather than a `ttyUSB` number.
- The sole CPU is a big-endian MC68040 provided by the Astra QEMU 9.2.4 TCG
  backend on the DE25's AArch64 HPS. There is no FPGA CPU and no older-CPU
  compatibility path.
- CPU0/1 are Cortex-A55; CPU2/3 are Cortex-A76. Physical interrupt accounting
  places SD and Ethernet receive IRQs on CPU0 and neither on CPU1. The release
  launcher therefore pins input/log helpers to CPU0, the hardware display
  renderer to CPU1, the sole TCG vCPU to CPU2, and QEMU main/AIO/filesystem
  workers to CPU3. A live host-channel run proved that six workers created
  after launch inherited CPU3 while the vCPU remained on CPU2. The MC68040
  benchmark reaches about 72 MHz effective on CPU2; the rejected A55 placement
  reached about 36 MHz.
- Astra owns 512 MiB guest RAM preallocated from HPS LPDDR4A. Media RAM owns
  512 MiB of the separate LPDDR4B device at `0x40000000..0x5fffffff` for
  graphics and sound. Linux exposes 934 MiB of LPDDR4A as normal system RAM.
- The ROM aperture is 512 KiB at `0xffe00000`.

The DE25 HDMI audio sink is 48 kHz stereo signed-24-bit with a 512-frame FIFO
at `0x20106000`. A direct Linux feeder played a 30-second arbitrary PCM clip
on the live board without new FIFO underruns or overflows, both idle and with
a concurrent SD-card read on CPU1. The CPU0 feeder used 1.639 s and 1.664 s
of CPU time respectively over 30 s. Instrumented replays saw a worst refill
gap of 6.805 ms and a minimum 185-frame FIFO level under SD contention. This
was the physical baseline. The fixed-format guest PCM transport and Linux
host mixer now exist behind the owner-bound AstraHost channel. A 30-second
guest-to-HDMI run on the final release delivered 1,440,000 frames with zero
underruns, overflows, or gaps and a 191-frame minimum FIFO level. A
production-service run mixed two independent 30-second client streams during
a direct SD read, with zero faults and a 178-frame minimum FIFO level. The
no-provider guest boot and
malformed/cross-client requests fail cleanly. The daemon is packaged and
ordered before the runtime, and competing hardware ownership is rejected.
A media service and `pcm.library.2` now build as a separate
Audio Kit, with NDK declarations/documentation only and positive/negative
host tests for batching, backpressure, bad buffers/replies, and service
protocol validation. The new library selects 48 kHz stereo signed-24-bit LE
or signed-16-bit BE input and exposes per-stream pause and queue clear.
The host self-test mixes 16 simultaneous voices, exercises paused and cleared
queues, and rejects invalid format/frame/control requests. A 30-second
physical DE25 socket run now mixed 16 simultaneous voices (peak 16),
1,440,000 source frames per voice, with 1,440,544 sink frames, minimum FIFO
456 frames, and zero underruns, overflows, or software gaps. It does not yet
measure MC68040 media-service load or game mixer cost. A PCM application
certifier and isolated image profile are staged. The optional media service is
now in the default desktop image, and the desktop streams a build-time-rendered
startup chime from a detached thread when audio is available. QEMU confirmed
that a missing host audio provider leaves the desktop and Terminal alive while
reporting the media failure. Clean DE25 release
`c2d9345145bcd22ead0ea66f665858b9c8ad5986d112e33bbdfc314ebd13a809`
boots without the prior desktop thread PC=0 user fault. The earlier release's
trace showed that closing a detached thread's self-handle before its callback
made the desktop sound thread fail its first VFS call and killed only the
desktop process, not the kernel. The shared runtime now keeps that handle
valid until the callback returns. On the new release the startup sound was
audible on the physical DE25, the trace has no VFS assertion or process exit,
and Astra and the audio host remain active with zero systemd restarts. Injected
pointer input has not yet been certified as responsive; the desktop screenshot
alone is not a responsiveness gate. Broader format/rate
conversion, the full restart/latency/contention gate, adapters, and game-ready
Audio Kit remain open. The PCM
headers pass installed C/C++ syntax checks and introduce no Doxygen warnings;
the global NDK documentation gate currently fails on unrelated undocumented
command, GUI, VFS, and runtime declarations.
`docs/AUDIO_ARCHITECTURE.md` holds the acceptance criteria.

The SDL2 port has a pinned, disposable source overlay and a first Astra PCM
audio backend in `sw/userspace/sdl2/`; upstream SDL is unchanged. Beast host
and MC68040 audio-adapter tests pass, including missing/broken PCM cases.
The full SDL2 core and test archives cross-compile with Astra PIC/ABI flags.
The core now links as `SDL2.library.3`, uses SDL's upstream export list, and
ships in `SDL.kit` through the normal provider index. The unchanged upstream
examples link to that shared library. In a fresh Beast-hosted QEMU image with
the Kit, upstream `testver` exits zero and a public-API probe selects the
Astra audio driver. In a negative image without the Kit, the same probe fails
to load with status 34, even when a bare SDL2 ELF is copied into `/libs`.
Upstream `testaudioinfo` opens PCM during setup and fails in host QEMU because
that machine has no PCM provider; Terminal now requests PCM for child commands
where one exists. Playback and physical DE25 qualification remain open.
Native per-thread priority and POSIX scheduling adapters cleared the previous
SDL pthread-priority build blocker; kernel,
runtime, and POSIX host suites and MC68040 builds pass. Per-thread signal
masks and handler stacks are implemented in the native kernel, and the POSIX
adapter inherits masks on thread creation.
The requested-stack-size pthread attribute now validates against Astra's guarded,
demand-grown native stack reservation. Its public layout change bumped libc and
dependent Kit manifests to ABI 2; Beast's POSIX host suite, MC68040 library
build, and full Kit test pass. The native display service accepts RGB565
windows and copies damaged pixels into hardware BLIT batches; Beast host
renderer/compositor positive and negative tests and MC68040 builds pass.
An SDL Astra video/input backend now builds in the disposable overlay, with
host positive/negative adapter tests. The shared POSIX entry adapter sends
startup-ready before invoking an upstream app's `main`, so an indefinite GUI
event loop cannot stall Supervisor boot. Unchanged upstream `testdraw2` builds
against `SDL2.library.3`. A fresh Beast-hosted QEMU image auto-launched an SDL
window probe, reached `SDL_VIDEO_READY` after presentation, and completed six
display submissions including 63 blits. The same image with unmodified
upstream `testdraw2` booted to stage 8, rendered, accepted Escape, and exited
zero. The regression gate rejected an older image where window cleanup killed
the display service and faulted the app. SDL keyboard focus, native
show/hide activation, its required video-quit callback, and cursor-only
display updates on minimized-window close now have tests. Beast POSIX host
suite, SDL host/MC68040 checks, display host/sanitizer suite, and image
provider tests pass. QEMU counts render commands but has no scanout
framebuffer; pixel appearance, mouse behavior, and performance require a
physical DE25 check.
SDL_net 2.4.0 now builds from unchanged upstream source as a separate
`SDL2_net.library.2` in `SDL.kit`. Its POSIX DNS/interface adapters use native
Network Kit operations backed by Linux host sockets, not a guest IP stack.
Beast host positive/negative network and POSIX tests pass; a fresh Beast QEMU
image passed the `SDLNetProbe.app` forward/reverse DNS, interface, and UDP
loopback gate. Upstream `chat`, `chatd`, and `showinterfaces` build and link;
interactive and DE25 runtime checks remain open. SDL_image 2.8.12 now ships
from a clean upstream checkout as `SDL2_image.library.2`; a fresh Beast QEMU
image decoded PNG, JPEG, and QOI through SDL2's POSIX-backed file RWops,
accepted a misleading filename by content, and rejected corrupt and missing
inputs. PNG/JPEG save-and-load round trips and invalid-save rejection passed
in the same QEMU gate using upstream's built-in encoders. AVIF/JXL/TIFF/WebP
readers are not enabled. Unmodified SDL_mixer 2.8.2 now
builds as `SDL2_mixer.library.3` in the same Kit with built-in WAV, MP3, OGG,
and FLAC support. A fresh QEMU image passed ten consecutive runs loading and
starting all four formats, rejecting malformed WAV/music input, and starting
16 simultaneous mixed channels with SDL's dummy audio driver. This is not
physical PCM output qualification. The repeated gate exposed a POSIX mutex
lost-acquisition bug: after a futex wake, the contended path could acquire
the mutex and then wait on it again. `pthread_mutex_lock`/`unlock` now use
the existing Astra-native mutex substrate; deterministic unlock-wake and
spurious-wake host regressions pass. Initial hidden-window creation, physical
PCM playback, and game qualification remain open; see
`sw/userspace/sdl2/README.md`.
SDL_ttf 2.24.0 now builds from unchanged upstream source with its pinned
FreeType submodule and is packaged as `SDL2_ttf.library.2`. A fresh QEMU image
rendered the existing Atkinson Hyperlegible Next font and rejected missing and
corrupt font inputs. HarfBuzz shaping and physical DE25 text quality remain
unqualified. The native generic Codec Kit remains a design contract, not an
implemented provider service or client library.
Chocolate Doom 3.1.1 at `410d96855b5df5410ff591a90efeafa889119224`
builds from pinned upstream source with its configure/Makefiles and links
dynamically against Astra's SDL, SDL_mixer, SDL_net, libc, runtime and compiler
libraries. The 2026-10-11 Task 12 integration checkpoint passes 30 seconds of
gameplay, keyboard input, pointer lock, audio/music and clean ENDOOM exit without
faults. The former CMake/static-link blocker is historical. Evidence is in
`fs-phase1-task12-20261011/integration-1` on Beast; this is QEMU evidence, not
new physical display qualification.

Release `802122b1d20b658edd355713f995a1857f1f66e6d74b02cfa1bb70db2cbd4bb6`
deploys the audio and remote-desktop Linux providers as systemd `Wants` of
`astra.service`, not `Requires`; both log to the journal. On the preceding
release, stopping audio left Astra and remote-desktop PIDs unchanged; killing
the audio daemon restarted only its unit (`NRestarts=1`). On the final release,
killing the remote-desktop daemon restarted only that unit (`NRestarts=1`):
VNC was listening again five seconds later while Astra PID 2151125 and audio
PID 2151124 remained unchanged. The guest media service detects a missing
provider and is optional at boot: QEMU tests confirmed `service inspect media`
reports `failed` after a retry while the desktop and terminal stay responsive.
A remote-desktop broker failure likewise reports a guest service failure and
automatically recovers after the broker returns, even under continuous manager
traffic. Full Linux journal text is not yet bridged to Astra; provider fault
events are available through `events`.

Release `fc065481ccc7493d699a48c0cd3f37835412f2fafcf1f50481d7387d19492f97`
corrects the RFB server's nonstandard 24-bit pixel format to 32-bit BGRA.
The Beast-hosted live VNC test authenticated to the DE25, negotiated a
1920x1080 32-bit framebuffer, and captured a complete frame. The macOS
`tools/AstraRemote` SwiftUI/TigerVNC viewer builds and reaches the desktop
from the Mac using its saved Keychain credential. Release
`3344ee9901d0dba851ee00c3f8e296b3466ad0524da3c13c538ddc8416af8ca1`
adds a nonblocking tap of the final Linux PCM mix and QEMU's VNC audio
extension. The physical DE25 audio probe rejected unnegotiated and invalid
formats and matched the rendered desktop startup motif byte for byte over
VNC; the Mac viewer logged that AVAudioEngine started and received a PCM
packet, and the user heard the motif cleanly. The first generated 440 Hz probe
was crackly because its Python producer fed 10 ms batches without queueing
headroom and closed the voice before draining. The regression probe now uses
1024-frame batches, checks queue space, finishes and drains the voice, and
compares the entire received sample. Astra, audio host, and remote desktop
remained active with zero systemd restarts.
Release `a43ea3c8e721909292b1caefb7582e72a3662d68be7b6661d151785c2b904f2a`
replaces the generated startup motif with the user-provided 5.72-second
48 kHz stereo WAV. The build converts it to S16BE PCM, and image publication
verifies the output against the source rather than accepting an old fixed
byte count. Host tests reject malformed, empty, wrong-format, and same-size
stale assets. The DE25 VNC-audio probe matched all 1,098,240 PCM bytes;
the marker regression test rejects silent markers. Astra, audio host, and
remote desktop were active with zero systemd restarts after the release.
Release `a1d77ad3b7594d112354315371bd5483c3470377709ca86dc3e5b2c1045b5a74`
adds cursor-shape updates on window topology changes, input-state reset, and
system-menu transitions. Display host, sanitizer, and MC68040 builds pass;
the DE25 release gate passed and the live RFB frame capture and Astra,
remote-desktop, and audio-host services remain active. A physical reproduction
of the formerly stuck resize cursor has not yet been repeated by the user.

The latest retained and deployed production Agilex 5 capture shell is
`b61dbda8`. It routes with every clock constrained: 40,854 / 46,800 ALMs,
317 / 358 RAM blocks, and 67 / 376 DSP blocks. Graphics setup slack is
+0.262 ns; worst setup is +0.130 ns and worst hold +0.001 ns. Exact build and
deployment evidence belongs in `fpga/de25/TIMING_CLOSURE.md`. The older
45,797-ALM capture shell below is historical, not current capacity.

The latest isolated source checkpoint removes the old tile controls, Copper
targets, validators, line builders, and scene AXI arbiter while retaining
framebuffer scrolling/wrapping and sprites. On 2026-09-25 its exact full DE25
route passed at 41,077 / 46,800 ALMs, 302 / 358 RAM blocks, and 46 / 376 DSP
blocks. Worst setup slack is +0.012 ns on an HDMI output pin; the 165 MHz
graphics clock has +0.801 ns. Full graphics simulation, ARM host build,
Linux host tests, and DE25 build contract pass. On 2026-09-26 it was
installed on the DE25 together with matching runtime release `2006eb8d…`.
The boot bundle was saved in `boot-rollback-01d05c63`, and the previous
release is kept as `previous`. The board passed POST, reached initial-image
stage 8, and has the display helper READY and three services active with no
restarts. The user reports HDMI output is on the TV; the Cam Link is not
attached. The full physical validation pass has not been run. See the exact
identity, timing, and artifact hashes in `fpga/de25/TIMING_CLOSURE.md`.

Next-session plan for DE25 physical validation: `docs/HANDOVER_2026-09-26_SDL_HARDWARE.md`.

Texture engine, uncommitted as of 2026-09-26 (`docs/TEXTURE_ENGINE.md`):
the TRIANGLES opcode (rasterization, nearest and bilinear sampling,
modulation, NONE/BLEND/ADD/MOD/MUL) and ARGB8888 destinations for every op
are in RTL. They match `sw/userspace/graphics/src/texture_reference.c` bit for
bit in simulation, and the full suite passes. A full DE25 route closes every
clock:

- 45,925 / 46,800 ALMs, 304 / 358 RAM blocks, 53 / 376 DSP blocks.
- Worst setup slack +0.051 ns (HDMI), +0.364 ns on the 165 MHz clock.
- **Only 875 ALMs remain.** The next FPGA feature must first reclaim logic;
  the largest candidate is sharing the texture engine's six attribute
  steppers.
- All 3-bit AXI read IDs are now used.
- Not deployed; the DE25 certification (`astra_render_certify` ASTRA_TEXTURE
  phase) has not run.

Managed graphics, uncommitted as of 2026-09-26: SDL drawing no longer runs on
the MC68040, and the whole SDL2 2D renderer now reaches the hardware
contract.

- `graphics.h` (`system.library` 2.4) is implemented for window-scoped GPU
  surfaces and ADLT v1.5 draw lists. They execute as render-only batches
  through the display service.
- ADLT v1.5 adds FILL_RECTS and LINES: `astra_draw_rectangles` and
  `astra_draw_lines` (`system.library` 2.6) are one draw-list command and
  one `ASTRA_RENDER_OP_FILL_RECTS` or `ASTRA_RENDER_OP_LINES` hardware
  command per 4096 records; translucent BLEND rectangles, and translucent
  FILLs, use `FILL_RECTS_OPTION_BLEND`. SDL points, fills, lines, and
  blended-line spans gather into them across SDL commands. The FILL_RECTS
  and LINES RTL is being written; until it ships the board rejects them.
- ADLT v1.4 adds TRIANGLES and a NONE/BLEND/ADD/MOD/MUL blend field with
  linear filtering. `render_builder.c` emits `ASTRA_RENDER_OP_TRIANGLES` per
  `docs/TEXTURE_ENGINE.md` §1 for geometry, rotation, color modulation,
  ADD/MOD/MUL, and filtered copies; plain and alpha copies stay blits.
  ARGB8888 is a draw target.
- Readback runs end to end: `READ_SURFACE` display request (kernel, QEMU,
  Linux helper), `ASTRA_GUI_GRAPHICS_SURFACE_READ`, `astra_surface_read`, and
  SDL `RenderReadPixels`.
- The SDL2 `astra` render driver supports every SDL blend mode except
  composed ones, color modulation, any `CopyEx` angle, `RenderGeometry`,
  linear scaling, ARGB8888 targets, and readback.
- Host tests pass at every layer, each checked against a deliberate
  perturbation. The QEMU gates pass: `test-display.py`, `test-terminal.py`,
  `test-sdl-video.py`, `test-sdl-draw2.py`, and the new
  `test-sdl-render.py` (TRIANGLES commands and surface reads counted).
- `test-display.py` had failed since d7fed3bb, because window-change renders
  presented the cursor every time. They now present it only when the pointer
  shape changes.
- Next: the texture-engine RTL (FPGA line), then the DE25 visual and
  performance gate. QEMU counts TRIANGLES but renders no pixels.

The contract and phase record are in `docs/MANAGED_GRAPHICS.md`.

The active display checkpoint has a fixed 1920x1080x60 HDMI output at 148.500
MHz and a 165 MHz graphics-build domain. The FPGA performs nearest-neighbor
logical scaling, integer fit/letterboxing when possible, fractional fit/fill
otherwise, composed-line replay, and logical copper-beam translation. The
deployed framebuffer, tiles, sprites, copper, and boot overlay scale together; the
double-buffered 32x32 ARGB hardware pointer is the native unscaled output
plane. The MC68040 supplies logical content and mode requests but never scales
pixels.

The framebuffer scanline manager uses the LPDDR4B controller's native 128-bit
port and writes two consecutive pixels per build clock into parity-banked line
stores. The production-shaped three-span row completes in 1,164 of the 2,444
available clocks; the prior narrowed, one-pixel path needed 2,234 clocks and
failed physically under Terminal. Replay requires explicit source-row
availability and cannot reinterpret a missed row as vertical scaling. The
installed shell `99c03a1487863bfe84b4f793a75bde2b8b6bfd3c1f3c3fd1e73023bec5204b62`
passed cold boot plus 1,382 captured active-Terminal frames with no black-band,
striped-line, or unbounded-replay event.

Media RAM is a service-owned arena, not raw process memory. Applications send
validated draw lists through GUI IPC; only the display service holds the
exclusive display lease and chooses media offsets. Scanouts use two 4 MiB
slots, and the render batch and scratch workspace occupy 8..16 MiB. Persistent
window content and chrome caches receive exact, 64-byte-aligned extents from
the remaining 16..512 MiB arena. The display service's page-backed window
metadata grows with demand; removing or resizing a window makes its old Media
RAM extents available for reuse. Admission is therefore governed by the real
Media RAM, kernel handle, port, wait-set, and render resources rather than a
display-specific window count or fixed per-window slot. Render records and
uploaded data grow upward while temporary surfaces grow downward through the
complete 8 MiB workspace, so they cannot overlap.

Visible process IDs are unsigned 16-bit values in the range 1..65535. PID 1 is
the initial supervisor. The allocator wraps, skips every live PID, and keeps
the generation-plus-slot owner token private, so PID reuse does not retarget
handles or an already-open `PROC:` node. Physical `ps` output on the current
release shows the supervisor as PID 1 and subsequent services and applications
as compact sequential IDs.

The owner ledger is no longer sized from a process guess. Memory initialization
allocates one possible owner record per physical frame, so every owner that can
hold a frame can also be accounted. This removes the measured fifth-Terminal
failure where 115,809 pages were free but all 64 old owner slots were occupied.
Ports and messages likewise consume page-backed metadata only when created or
queued. There is no per-owner port quota or fixed global message pool; queue
depth is limited by its 16-bit ABI field and available memory.

The current resource-limit audit reproduced the tenth-Terminal failure at the
VFS bind-area reply: allocation, mapping, duplication, and transport all
succeeded, but the kernel rejected the server mapping because each address
space had only 32 fixed 4 MiB shared-area slots. Shared areas now occupy their
actual page-rounded spans in the 128 MiB architectural window; 40 page-sized
mappings pass, while exhaustion of the real window fails atomically. The full
8-bit handle namespace is available, and per-thread wait registrations are
allocated on first use instead of embedded behind the former 128-entry guess.
The maximum 255-member wait set and injected-allocation rollback both pass.

The inherited 32-process constant has been deleted. POSIXD, kernel library
mapping snapshots, PROC, `ps`, startup manifests, Supervisor process/VFS
bookkeeping, and DMA metadata now grow from runtime memory, preserve prior
state on allocation failure, and have positive tests above their old limits
plus negative allocation tests. VFS sessions use the complete 8-bit kernel
handle namespace rather than a process-derived quota; DMA records grow in
page-backed leaves through the complete 16-bit DMA-handle namespace. The full
kernel host suite and MC68040 target build pass. Shared-area mapping metadata
also grows in page-backed blocks: 1,056 simultaneous mappings pass, exceeding
the former 1,024-entry table, and injected metadata exhaustion rolls back
without publishing a mapping or leaking user resources. The obsolete
`KERNEL_VM_ADDRESS_SPACE_MAX` has been deleted. The audit is not yet closed:
device registration now grows in kernel-owned metadata pages, exclusive leases
have no per-owner quota, and positive coverage registers and leases 96 devices
while injected metadata and lease failures publish no partial state. Ports and
queued messages likewise grow in page-backed leaves through their complete
16-bit slot namespaces; one owner passes the former 24-port quota and the old
128-port global table, while injected port and message metadata failures leave
all prior queues and authority intact. Synchronization objects and their timer
heap, rings, and detached handle transfers now also allocate page-backed
metadata through their complete encoded namespaces. Positive tests exceed the
former 32-object, 16-ring, and 256-transfer limits; negative allocation tests
prove failure rolls back without publishing partial authority. The former
16-message per-port policy and its fake configured-capacity accounting are
gone; callers may select any queue depth representable by the 16-bit port ABI,
while actual queued records consume memory only when messages arrive.

The complete kernel host suite, MC68040 target build, NDK suite, and full
userspace suite pass with these changes. No QEMU or DE25 release attempt has
been made yet: the source audit is still checking the remaining fixed values
and separating real hardware, wire-format, and address-map boundaries from
deployment guesses.

Boot ABI 1.0 removes the independent ten-entry memory-map ceiling. Its 57
entries are derived from the complete records that fit in the linker-reserved
1 KiB handoff storage; the full resource-backed capacity validates, while an
over-count is rejected before any range access.

The earlier Startup ABI 7 checkpoint removed the independent 32-capability
ceiling. Current Startup ABI 8 retains 43 complete 92-byte capability records
beside its 92-byte header in the actual 4 KiB startup page. Argument/environment
vectors now occupy separately owned read-only pages, so their bounded packed
strings cannot consume capability capacity. Producer, Runtime and POSIX consumers
share this geometry; the active coherent qualification is recorded above.

The ELF acceptance profile no longer rejects a fifth loadable segment. The
measured compiler output for zsh, vim, Lua, Terminal, and Interface Gallery is
three `PT_LOAD` records, so four remain inline and allocation-free on the
current path; additional records spill into kernel metadata pages. A positive
test accepts and resolves five segments, while an injected first-overflow-page
failure returns out-of-memory with no partial plan. Executable launch, `exec`,
streamed loading, library publication, abort, and cache eviction all release
the overflow metadata. The complete kernel host suite and MC68040 target build
pass after this removal.

Direct process creation no longer assumes the complete ELF program-header
table lives in a 1 KiB prefix. It copies the fixed ELF header, then uses the
existing streaming parser to fetch every 32-byte program header from its
declared file offset. A header beyond the former window launches successfully;
an offset into an unmapped user page fails without publishing a process.

The shared fault-safe user-copy primitive no longer rejects spans above 4 KiB.
Its existing MC68040 loop and fault scope cover the complete validated user
range; tests pass an 8 KiB span and still reject the same span when it crosses
the user-address boundary. Individual syscalls continue to validate their own
destination buffers and protocol lengths.

The five-window physical gate also exposed an independent display-service
underflow. When an active client's event queue was temporarily backpressured,
wait source 1 represented the client's writable notification, but the dispatch
loop subtracted the three non-window sources and treated it as window index
`UINT32_MAX-1`. Display PID 10 faulted at `receive_command`, PC `0x00104ec8`,
address `0x3ffffe9e`. The dispatcher now validates the source before deriving a
window index; a blocked-input wake simply causes input readiness to be
re-evaluated. The regression failed before this fix and passes the normal,
ASan/UBSan, analyzer, and MC68040 display builds.

Immutable software release
`6205137ad1289716295af463f04e266dc96e7eec3e4c4f50c50b9c4e2f1b73ee`
is selected, byte-verified, and running on the DE25. Its QEMU, ROM, storage
seed, host display, remote-desktop, and source-manifest SHA-256 values are
respectively
`6ff7dcc3f11ab4fae062afb102420c154d4823e86c640dd5e87216b38bddd977`,
`ab7b1d29e43a9e5c7c5a970b6321a1c7d137f965449a01948de1fc64e46a9f35`,
`ef9f88bbe17ad941e0a0e16460ecebbd5e3f81d82f8f4350bf17a02aa18d55b9`,
`d7138bb4bd2e22f93d8692e0e3ee26290a18228d5ac1f13997fc1c08a85054fe`,
`3421caf2be0729c223988103ebcf18f1cc017d355f4d0edcaa906b7b42322f3a`,
and `22dc39d83ba802af3594c77d2df1b6de9649bfbd10e9313df7a561450e394401`.
It reaches stage 8; `astra.service` and
`astra-remote-desktop.service` are active with zero automatic restarts.

This release completed the library, CLI, storage, and remote-desktop audit.
The QEMU gate passes all 71 terminal commands. The `open` command returns 0
after confirmed startup without waiting for application exit, returns exactly
1 when startup fails, and `--wait` returns the application's exit status.
Positive and negative NDK, command, supervisor, QEMU, and physical-DE25 tests
cover those contracts. On the board, a missing application returned 1 while
`InterfaceGallery.app` returned 0 and remained alive; the latter's direct RFB
evidence frame is
`635dfc703072e2ad4d5cd56a4748d8b49d8699ac0d8fb1cf5da1083faee917cb`.
Launch origin is carried
in the canonical startup record and is available through
`astra_startup_launch_source()`; applications require no launch-origin switch.
The QEMU terminal gate rebuilds its workspace ROM before every run and has a
negative regression proving that it refuses to test when that refresh fails,
so a stale boot image cannot masquerade as current source.
The production `COMMANDS:` surface contains exactly 21 single-purpose
programs; `posix`, `hello`, and `cxx` remain test fixtures and are absent from
the installed command namespace.
The direct Mac-to-DE25 RFB gate authenticated at `192.168.1.52:5900`, captured
a 1920x1080 RGB frame, opened Terminal with the pointer, and preserved a fast
mixed-case and shifted-punctuation burst exactly. The confirming CLI frame
SHA-256 is
`a86a4ce4204e90efb2935a1ed446fcc174cefd6147b2b3d931f99cdf828446a3`.
The final release's authenticated desktop capture is
`59f1aaecaa1e0d1e0a2f0223f088095ce502c995ace9648312604a667ee23638`;
anonymous access and an incorrect password were both rejected.
The server now describes its captured RGB24 bytes with the correct
little-endian R/G/B shifts, so the native and exact macOS 32-bit client
formats normalize to that same frame hash. It tracks changed pixels and sends
only their bounding rectangle instead of recompressing every 1920x1080 frame.
Pointer input uses LibVNCServer's eager event draining, coalesces motion before
the synchronous QMP handoff, and still sends every button edge immediately.
The physical gate delivered an exact `(700,500)` PointerPos update and drained
1,000 queued motion events to `(1099,399)` in 0.120 seconds.
Shifted RFB keysyms now hold the synthetic Shift key before the base key enters
the reconciliation pass; positive shifted-symbol and negative unshifted-symbol
tests prevent the prior base-key use-after-free ordering from recurring.
This release advances the GUI protocol to version 12. Every ordinary window
receives one complete state snapshot for active, inactive, minimized,
maximized, restored, and geometry transitions, plus a distinct resize event
only when its client extent changes. All four edges and four corners resize;
the hardware pointer selects horizontal, vertical, or diagonal feedback, and
interactive resize notification is committed once on button release. Terminal
consumes that state directly:
its cursor blinks only while active, remains visible while inactive, and its
underline is aligned to the font box rather than the inter-line gap. The
desktop uses the 13-pixel title face for a measured, centered application label
and the Terminal bundle now generates the toolbar-free icon from the approved
desktop design. Physical captures 600 ms apart were byte-identical while the
Terminal was inactive
(`ca9d8919a34be268bbae5ec8fad5bd77f9e22565a35d48db7565725108f83290`)
and alternated between
`cd4e8278a46c1837fe27e8aba294d477131e4ef1ae321238af7c3b52044b1443`
and `6a71a7e7b7597acda363adf74b652d2dd772f6c362a64920b11b79e6de8fbffd`
after activating it. Maximizing Interface Gallery produced a full-screen,
correctly reflowed frame
(`eb93ccf209bf77a959a6983e4c59a1c23afe6201cf55e5a1e5e2fa2d480cd582`),
exercising both its state and resize notifications on the DE25.
The complete remote-desktop lifecycle also passes dynamic definition
create/enable/disable/delete, automatic and manual activation, restart,
broker loss/recovery, events, and shutdown. The storage allocator derives its
production arena from cache classes plus measured transient filesystem demand.
lwext4 rename atomically replaces compatible destinations, preserves both
operands on every refusal, rejects cross-mount moves and directory-descendant
cycles, and passes raw, partitioned, full-volume, power-cut, journal-failure,
ASan/UBSan, TSan, analyzer, and independent `e2fsck` gates. Kernel, NDK, tools,
every userspace suite, shared-library contracts, analyzers, and sanitizers pass
on Beast.

The library-loading audit is cleared in immutable DE25 release
`354a01b26f2458642761ad24658841b4525c8d31cb102cb965702e8ed14613e5`.
The screenshot showing `filesystem.library.2` and `config.library` load
failures was the board still executing stale release `62db2ee8...`, not the
fresh Beast/QEMU candidate. The production selector and live QEMU process now
both identify release `354a01b2...`; `astra.service` is active with zero
restarts, every boot service including `ntpd` reports status 0, and the initial
image reaches stage 8. A physical Terminal run reports `POSIX RAW PASS: /CWD`,
and bare `which status` resolves `/commands/status`. The release ROM, writable
storage seed, and QEMU executable SHA-256 values are respectively
`ad733d4d830c179f479971391b9ba42ace15718e0cf6aef35ccd2ddf8255c6c2`,
`8e38f96b5732d3305e522d49ccccd98b47fc8401f069e15b6c76a70e3f44bdc7`, and
`3553fca8f5d13f0cb6bb931ef5c301928cdbbf65a9710e2aa76bbce1e25f3e35`.
The launcher and physical-terminal probe now use the production
`/var/lib/astra` and `/run/astra/qmp.sock` defaults; positive assertions and
negative stale-path regressions cover both defaults. The probe also submits
each key's press and release atomically and rejects the interactive `posix`
command before opening QMP, preventing duplicate characters and repeated raw
mode failures from contaminating a live Terminal.

The release gate previously advanced the verified `current` selector without
restarting `astra.service`; the board therefore kept executing the preceding
release even though publication reported success. The deployer now restarts
the service and refuses success until `/proc` identifies the selected
release's exact QEMU executable and its QMP socket is live. Only then may it
prune old releases. Application bundle packaging likewise always asks each
producer makefile for its current binary, so an existing bundle cannot hide a
newer source or service image.

Immutable release
`6f59f47361d5e3d920b0798268dd77a6ed4f19a9c2c617d14c5935107ba9ea91`
is selected and byte-verified on the DE25. Its QEMU, ROM, storage, host display
helper, and launcher SHA-256 values are respectively
`dae7d602590fac78c50b4e2abead269c1b2ffdea135b3b96c896bcaa47961342`,
`3257fbefb898e4a992f6e8f9de73a61379a6789006959770fb734ef007c0e61d`,
`1e16134170631aea7e0b2772ee82163cf4b78b19d74a73f446f404de2f738c9e`,
`ca281d6d6d2d37fb566c02a415f1d54dc7e04192f2adaf492a4ee4ab817edf65`,
and `1e76f48511636d1cc9c479c3f7ae90412322691b9af6c3b33a026d454b7b6092`.
It reached stage 8 at 69.037 MHz and remained active with zero restarts. The
physical process table contained five Terminal processes at PIDs 12, 14, 16,
18, and 20 and five zsh processes at PIDs 13, 15, 17, 19, and 21. Port counters
reported configured capacity 261, peak occupancy 15, and zero allocation,
quota, discard, or handle-discard failures. The retained ring is
`/tmp/astra-five-after-fix.bin`, SHA-256
`209220945685a18641db988b7fbb735917de8df5762a38f0f47b3064872ae79a`.
The Cam Link proof is `/private/tmp/astra-five-terminals-fixed.png`, SHA-256
`f7b3d2538f77564678a3fdc8014cbe4724e321ee6e7187a45bbcfe7e3cad5174`.

The exact routed shell passed cold boot, JTAG design identity, native splash
readback/presentation, the complete renderer suite, live 320x200-to-1080p 5x
integer presentation, the complete 64-sprite variable-geometry sweep, copper,
48 kHz HDMI audio, and POST status updates. The physical sprite run reported
zero AXI and deadline errors, and Linux remained healthy. The prior fatal HPS
SError was eliminated at its root: recoverable scene validation failures now
increment the existing commit-error counter and return AXI `OKAY`, while true
transport faults retain AXI error responses. The normal immutable runtime is
restored and reaches stage 8 at 70.192 MHz effective with the correct wall
clock. Requested clock values, generated PLL clocks, Platform Designer
metadata, RTL timing constants, TimeQuest constraints, and physical behavior
must agree; none may substitute for another as release evidence.

Immutable release
`00cb50981461fabb6b10ddc6a99b6b38a9cdc1b94df400f5cd4117aa635e2095`
corrects the POST/panic console's stale 14-pixel character spacing. The host
renderer now consumes the NDK's 8-pixel Astra Mono cell advance and has
SHA-256
`8b03e3b59136d8cd389f39b0888aaf07490d6352a4295e321afebcaa228673aa`;
its ROM and storage inputs are byte-identical to the preceding accepted
release. A physical Cam Link specimen measured repeated `I` glyph starts at
x=616, 624, 632, and 640 and repeated `W` glyph starts at x=655, 663, 671,
and 679: exact 8-pixel advances. The capture is
`/private/tmp/astra-post-spacing-v1-proof.png`, SHA-256
`b4862be2f89ae28da4c7831708c35af0ee193a39f24d22c1c7501323fb82bfc2`.
The restored runtime reached stage 8 at 70.275 MHz effective; the board
service is active with zero restarts, and the live renderer hash matches the
release. The DE25 production publisher now owns all software inputs: it cleans
its producers, builds from the current mirrored source, records that source
manifest in the immutable release, and rejects any source mutation during the
build. It does not clean or rebuild the independently qualified FPGA route.
Caller-supplied binaries are confined to the lower-level diagnostic creator
and cannot enter the production publication path.

Retained interface-performance release
`1c12a8d59120f00181cdda7171330922febd623590bd24adb7f61db25505f0e1`
established the current flat-layout baseline. Its storage, ROM, and QEMU
SHA-256 values are respectively
`01c7a3ce8a99ccec41687eb401d780852f77047564382d6fd2ba1878bbffc3f4`,
`750be675b149b2355d6ddd1d5aa22264e63ee420ad44694b00ea307d8e64e2a6`,
and `4c5606a57402fff5c404da0d0033eed975ed3f6454b2244f9fe32de12ea3df5e`.
Its interface library and gallery executable hashes are
`c51cdb243fad5f2326c7c7da1296b22165b176320669ec7503862930b5af7405`
and `095bfd474644bfe6efc9b1c2fced252b995e2963600e683163a6d7a44ffbdeee`.
It launches Interface Gallery against `interface.library` ABI 1.4 with Label,
Button, Checkbox, Radio, Switch, Slider, and Progress controls. A physical Cam
Link capture shows the shared five-segment checkbox mark and outlined off-state
switch thumb. A second capture taken while the slider button was still held
shows its label changed from `62%` to `93%`; the update is not deferred until
release. The retained Beast captures are
`/tmp/astra-switch-off-clear-1c12.png` and
`/tmp/astra-slider-held-1c12.png`, with SHA-256 values
`c8e08a974787014d9f1a9683ecff0b93f2df5ff7216ac30ed7b39a1d848d62aa`
and `a5a84e3fa070f6f54940f73ac05c407e5f44abca8cb9fec1e86b0a9a4c80cdf3`.

The retained integer flex engine measured 7.126, 6.962, and
7.295 microseconds per control: 85.511 microseconds/reflow for 12 controls,
445.550 for 64, and 1.867 milliseconds for 256. The 10-microsecond/control
automated gate retains at least 27% headroom over this run. The retained Beast
trace is `/tmp/astra-interface-ring-6a512f28.bin`, SHA-256
`a6bdb60a1bf3f633823883d8705d64d478f974c1e216e4ea673f7e6458951813`.

Immutable release
`06f28a5787a70c38d032b04a66242349b0c4f975a99613670715e3bf9c544c61`
is the byte-verified runtime selected by `/var/lib/astra/current`. Its display
service, Interface Gallery, and storage image SHA-256 values are respectively
`5a4b6b04c71f4b9620d932dd28d481d1e543d8677132fa16f4cdb2ecb9c3aec9`,
`baab9ded2e39b4410e87cb3267386064c4e0b76f295131dcfe2cdba726467a0f`,
and `d13a6322d7bf3c988df5044f1b9b1df9c4e2a0d99c427b0f834a7b3b5e201d2e`.
The physical DE25 service is active with zero restarts. A Cam Link capture
shows Interface Gallery alive and completely rendered after launch, while the
kernel, supervisor, resident services, and desktop remain alive. The failure
that formerly killed the system was not a kernel fault: the manifest marked an
ordinary application `required`, its window-open render failed, and the
supervisor exited. Manifest parsing now rejects `required` on applications.

The shared render builder now treats a fully clipped primitive as a successful
no-op, while an exhausted command ring remains a distinct failure. This fixed
the Gallery window-open failure at its common rendering boundary and retains
strict errors for invalid destinations, descriptors, surfaces, and storage.
Focused graphics and display tests pass normally, under ASan/UBSan, and through
the MC68040 cross-build. The physical capture is retained locally as
`/private/tmp/astra-interface-fix-v6-resume.png`.

The builder no longer exposes a glyph-count or fixed descriptor-table ceiling.
Descriptors, glyph records, and source data share the workspace allocator;
text longer than Astraea's 4,096-glyph per-command encoding is split into
consecutive commands with its 26.6 pen position preserved. Callers may provide
any batch storage size from the protocol minimum through the physical 8 MiB
workspace, and allocation fails only when that supplied storage is exhausted.

Immutable DE25 release
`321a882c34ac27f7bf287bc3129eaf1c1fae59aa0384e9c5a8fa8aafde84ae60`
physically accepts Interface Kit 5.1 disclosure geometry and the nonblocking
input path. A collapsed disclosure points right and an expanded disclosure
points down; retained Cam Link frames are
`/private/tmp/astra-v52-disclosure-collapsed-clear.png` and
`/private/tmp/astra-v52-disclosure-expanded.png`, with SHA-256 values
`0631be6fb91731933d4c9dfede60c83c9c491535f953821b7eed4169bc90e362`
and `f5ab52090861331a89ea178af6970a6d5f2a2b8dd3ebdb92681afcc26cf83dc2`.
The input service no longer waits on a saturated client: its existing loss,
state-reset, and latest-motion recovery runs from writable client handles in
the normal wait set. The Gallery drains every already-queued event, preserves
ordered state transitions, accumulates damage, and presents once.

The deployed physical motion ceiling remains the active display blocker. A 60-sample,
one-second large-window drag produced 15 render batches during input and one
more after settling. The same splitter workload produced nine render batches
during input and one more after settling, versus three catch-up renders in the
preceding build. Event batching removes stale replay but cannot raise the
measured 15 fps window and 9 fps control presentation ceilings. The retained
window-scene architecture is now implemented, regression-tested, and routed in
the complete production shell. Clean deployment and physical measurement are
the active acceptance gate; further control or cursor micro-optimization is
not an acceptance path.
One profiled move frame completed 37 commands in 56.539 ms of hardware time:
one 951,808-pixel background fill, one 951,808-pixel desktop restore, one
915,200-pixel Gallery copy, and 34 negligible rounded-edge blits. Release
artifact SHA-256 values are `24b08c602529275991796c75b7826180f4dc350fc7f97ac89153c1ab92c1bd7d`
for storage, `abbc0720a0d9bd23c02260486e8f9f3db5838b59be55c4fd700e67d85655876d`
for the display helper, `f04adb18fe13b147f88e26e11a64070222c9bc9dc8b925868593a98ea6db2418`
for QEMU, and `174c4eee1cc20d28ca45ca3b4280b07dd5763e29079a0efd01d897faf10a5942`
for the ROM.

The NDK now owns the sole HID-to-key translation source used by Terminal and
the input service. Their prior independent implementations were consolidated
without changing terminal control-key or input text-event behavior. Terminal
and input builds also include dependency files only for their declared object
graphs, so a removed or renamed source can no longer remain build-authoritative
through a stale wildcard-imported `.d` file. Interface Kit packaging derives
its ABI and version payload paths from its manifest rather than repeating a
second version literal.

A physical southeast resize advanced renderer batches from 2 to 4 and
submissions/completions from 3/3 to 7/7. Both snapshots remained unchanged one
second later. The pre-fix build advanced by another 20 batches and 240 glyphs
in the same interval because every same-size frame notification redamaged the
UI; `interface.library` now reflows and damages only when the parent extent
actually changes, and a focused regression test retains that contract.

This release retains the corrected compositor layout. Across 463 captured
physical HDMI frames, the former corruption band at y=580..781 remained
constant while the cursor visibly blinked; after a window drag, another 300
frames had zero black-band frames. The pre-fix band was an exact software
overlap: the 3,855,360-byte desktop surface occupied a 2 MiB slot, so the
following Terminal slot overwrote precisely those rows.

Current source removes the MC68040's bounded visible-region
subtraction pass. The display service now emits every damaged window in
bottom-to-top order, clipped to the frame damage, and Astraea's ordered blits
resolve overlap. The former eight-region overflow fallback is gone. Display
tests prove the complete damaged stack is submitted, and pass normally,
under ASan/UBSan, GCC analysis, and in the MC68040 target build. The display
service now also uses page-backed dynamic window storage and exact Media RAM
allocations. Host tests grow the table beyond its first committed page,
compose five simultaneous windows, verify every live chrome/content extent is
disjoint, prove freed-range reuse, and reject allocation only after the
physical arena is exhausted. A validated batched hardware scene descriptor
remains the next compositor step.

Immutable release
`69c97c7381e7daffad96064ae41a1410b7d31bb01f1a469d3263bd6c5e355a7b`
advances Graphics, Font, and Interface Kits to
ABI 2.0. Interface controls now support caller-owned nested flex containers
with parent-before-child validation, reverse-order intrinsic measurement, and
iterative descendant reflow without heap allocation, recursion, or a child or
depth cap. Ancestor clipping governs hit testing, damage, and painting; draw
list ABI 1.1 carries the clip into Astraea's hardware command clip. Graphics
and Interface host, sanitizer, analyzer, and MC68040 gates pass. The physical
70.324 MHz MC68040 nested-layout measurements are 92.449 microseconds per
12-control reflow, 325.825 microseconds for 64 controls, and 1.480 milliseconds
for 256 controls: respectively 7.704, 5.091, and 5.783 microseconds per control,
all inside the 10-microsecond gate. Full-window frame damage now suppresses
per-control damage-union work that cannot enlarge the dirty region; compared
with the immediately preceding physical build this reduced the three workloads
by 28.4%, 35.2%, and 11.7% without changing layout or damage results.

The release's storage image, Graphics library, Interface library, Gallery, and
trace SHA-256 values are respectively
`472897d5732523e4c455e44d89729afb45b267ec1564781a8448761b7b317085`,
`739ed3a46abad7d81b33aae51d612b2112148d4a69a84d542459cbdd4b1d42dd`,
`7612e7460fd1d91d9e54e02760894c9e20adee2b29e40fb25a1a46a2c18a0b8c`,
`d72a1cb3eb63cda20554acb886440ef396139ad846263a4425e6adddaa8d239f`,
and `80957f5c117953b03db7b2dcb09635244e17ff3c1a7d3458427b4f2bb217061c`.
The trace is retained on Beast as
`/tmp/astra-interface-abi2-v3-ring.bin`. Cam Link captures show the Gallery
alive and Terminal launched above it after executing `echo terminal-ok`; their
local paths are `/private/tmp/astra-interface-abi2-v3-gallery.png` and
`/private/tmp/astra-interface-abi2-v3-terminal.png`, with SHA-256 values
`73014e5614e9eaf849d24f15e87fc872cbc21c520b9fb8ee1b86986c0b9ed61a`
and `4bc4930544dceedd546ba0c32f5da3f685bac6725beb2150ec9c1e53c99dff1b`.
The service remains active at stage 8 with zero restarts.

The launch failure in the preceding candidate was a stale
`graphics_shared_surface.o`: Graphics' explicit loadable-library recipes did
not generate header dependency files, so ABI-1 surface layout code was linked
into an ABI-2 library. Graphics and Interface library recipes now generate and
include dependency files, and their objects depend on their owning Makefiles
so the corrected recipes bootstrap themselves. An executable boundary test
retains both build-graph contracts.

Current source defines UTF-8 as Astra OS's sole text encoding and moves
validation, scalar encoding/decoding, and scalar-boundary traversal into the
NDK's single `astra/utf8.h` implementation. NDK calls, launch arguments and
environment values, config documents, VFS paths and returned names, display
IPC, window titles, and interface labels reject malformed complete spans;
terminal byte streams replace malformed input with U+FFFD. Editing-key
sentinels now live above U+10FFFF and cannot collide with Unicode characters.

AFNT 0.2 and `font.library` 2.3 now carry baseline-relative
ascent/descent/line-gap, cap-height, x-height, maximum advance, underline, and
strikeout metrics. Built-in bold and italic strikes are prepared from AFNT at
build time; the draw-list renderer positions underline/strikeout from metrics;
glyph expansion remains hardware work. `interface.library` 2.2 adds the shared
TextSurface fixed-grid renderer, and Terminal now consumes it for styled runs,
logical colors, carets, and hardware-blit scrolling instead of owning a private
painter. The component now also performs grid hit testing and renders
normalized, half-open selections without storing selection state in its ABI
object; Terminal uses both for pointer-drag selection. Current source advances
this to `interface.library` 2.4: capacity-padded grid rows use an explicit
selection stride, typed multi-representation clipboard documents are immutable
shared areas owned by a protected clipboard service, and Terminal maps the
input service's normalized Meta-C/Meta-V to copy/paste without claiming
Ctrl-C. Immutable release
`07be64339b3bdb58eef906734ed3b857e701856d607f17e6804a4e26a6993ee8`
booted that service graph on the physical DE25, reached stage 8 at 70.092 MHz
effective, and remained active with zero service restarts. The end-to-end QEMU
gate then selected a known Terminal row through pointer input, copied it with
Meta-C, pasted it with Meta-V, executed it in zsh, and observed the exact
`CLIPBOARD-PASTE-PASS` result. Scrollback, find, wide cells, and code/flow
layout remain pending. The current
AFNT bitmap/Amiga importer remains operational; the
documented scalable TTF/OTF/WOFF pipeline and persistent synthetic-strike cache
are not yet implemented.

`interface.library` is now ABI 2.5 and supplies the shared per-document
undo/redo manager. It retains named, typed, serializable actions in a
caller-owned growable arena, applies groups transactionally with compensation,
tracks save-point dirty state, invalidates redo branches after a new edit, and
rejects callback reentrancy. Normal, sanitizer, analyzer, and MC68040 builds
pass. On the physical 69.874 MHz MC68040, 10,000 groups measured 7.546
microseconds per apply-and-record, 3.958 per undo, and 3.292 per redo; the
retained gate is 12.5 microseconds per phase.

The ABI 3.0 release retained the 2.6
allocation-free UTF-8 piece-table model and single-line Field control, and now
gives every animated control its own state and active-list membership. The
display service delivers a coalescing per-window vblank event; the window's UI
thread calls the shared Interface Kit once per pulse, and only active controls
are visited. Focused carets and indeterminate progress indicators therefore
need no application timer, polling loop, worker thread, or cross-thread UI
mutation. The retained text model owns scalar-boundary selection, an indexed
line map, exact capacity preflight, atomic replacement failure, arena
relocation and compaction, and full invariant validation. Field adds caret and
pointer/Shift selection, horizontal viewport tracking, UTF-8 editing,
read-only and fault states, semantic copy/cut/paste actions, and one shared
replacement boundary for typed, programmatic, and pasted text. Interface
Gallery and the NDK example consume the public API rather than private text
buffers. ABI 3.0 deliberately enlarges caller-owned `AstraControl` and
`AstraWindow` so controls directly own animation membership and phase and
windows directly own their coalescing vblank wait handle. The obsolete timer
tick and special vblank-window constructor were deleted rather than retained
as compatibility paths. GUI protocol version 8 returns the control and vblank
handles atomically, and every in-tree producer and consumer uses that single
contract.

Immutable physical DE25 release
`29b7d36f7687b4fdf577cc6e13a8fdf3064be2947905a190155edfef11d313ca`
boots Interface Kit ABI 3.0 and GUI protocol 8, reaches stage 8 at 69.865 MHz
effective, and remains active with zero service restarts. Its storage image,
ROM, Interface library, Gallery, and display service SHA-256 values are
respectively
`519a0817fec8a5dfdaea4ca9d8687d2aa4a22d3ec5a3ddce102628d896c4ece0`,
`2588923622c2a83adc586bf7629d95670307154bbb6bbe3713e39f1da40120e6`,
`a593564cfad3d962b65ada457eab79cf0597508c6f6bfe10668038ad0e02c34d`,
`86f43e9431c60b99dda1ccb5daf1e8d6e126510d214f6f0be7c95e47e20d25d4`,
and `4dccc638e287690de291f5ebd08fe7760d5334212db66460b67652501bb2d3ae`.
The extracted ext4 volume passes read-only `e2fsck` and contains only the 3.0
Interface Kit provider. Twelve independent Cam Link samples of the
indeterminate progress region produced ten distinct frames. The retained frame
is `/private/tmp/astra-interface-v3.png`, SHA-256
`e738c85fcd3f281b319f7e11daab32161dd0a928c9917b18683d60e691e24371`.

Immutable release
`e3d871bf579f732e58d5755414ca0f21c1445004ca4cadad480009bab18e15e7`
was the previous byte-verified physical DE25 runtime. It carried Interface Kit
ABI 3.1 and the Gallery's shared segmented selector, reached stage 8 at
21:48:54 on 2026-09-12, and remains active with zero service restarts. Its
clean storage image, ROM, Interface library, and Gallery SHA-256 values are
respectively
`e8ed59be274941a28a71e082d7695ded45b83a989d59754554047feb41fb676c`,
`4c019911b1c6c500a045fc3cf29bcab7225e9fee014d33bbeba78009b2cb9ee5`,
`8aca7e8064579dd911036c3e8b6753ac68d0cd713ed2a7a1e878be053dacc113`,
and
`6bccd169bfbb59a0228df44565fe416f12c92a5a9ccf6f9107b91f1a3335957c`.
The extracted ext4 volume passed read-only `e2fsck` and contains only the 3.1
provider. Segmented visual/input/timing acceptance is still outstanding.

Current source deliberately advances Interface Kit to ABI 4.0 for the numeric
stepper. The ABI break changes the caller-owned UI context and makes vblank
return the same semantic action used by ordinary input, so pointer-held repeat
does not introduce polling or a second callback route. The stepper supports
signed decimal entry, range snapping, Arrow/Home/End, immediate pointer steps,
a 400-millisecond hold delay, and muted bound affordances through shared range,
layout, focus, damage, and rendering code. Host, sanitizer, analyzer, MC68040,
library-contract, and SONAME checks pass. Diagnostic immutable DE25 release
`1543f41e976943999bfc5447e26a9aa7f62a8f49e7966acaa6540515c44b4bff`
reached stage 8 at 69.416 MHz with zero restarts and rendered the control
through the production draw-list and FPGA path. Ten thousand segmented and
stepper action transitions measured 7.003 and 8.926 microseconds each; both
now share a measured 12.5-microsecond regression gate. The Gallery capture is
`/private/tmp/astra-interface-v4-gallery.png`, SHA-256
`2da4732dc0c5b42ed5763ab8e11b8f9d8df0b361199b3002e8325595c1a438d0`.
Its image, Interface library, and Gallery SHA-256 values are respectively
`3233d5ab123633a06cf713b949650660134e5bc68b896f6f9bdb02bab948380c`,
`f3958b93f0dac80a35db19407e91930c28b0e72446d3e25ba7d222359282f199`,
and `9032d4788dcaa01ce16adcdcad5104972d4ac6dcdeaf3c46ef3ca8ede1a8cf54`.
Physical pointer and hold-repeat acceptance remains pending.

The same capture exposes a font integration gap rather than a UTF-8 decoder
failure: the Field preserves the `世界` scalars, but its embedded mono strike
lacks those glyphs and displays the replacement glyph twice. Interface Kit
must consume `font.library`'s resolved layout and fallback chain; it must not
grow a parallel embedded-font fallback implementation. `世界` remains the
target regression specimen.

Immutable release
`196ed3afe0a803db5760749fbd32a9ca32d6545453343b2056cd1be4efdaa8aa`
is the current normal-desktop DE25 runtime. It carries Interface Kit ABI 4.0,
reached stage 8 at 69.685 MHz on 2026-09-12, and remains active with zero
service restarts. Its clean image, ROM, QEMU, Interface library, Gallery, and
display-helper SHA-256 values are respectively
`bc6b6c86c6e199132ce5894ef5329ffb77c51fd8eefcafd718f4bb4adbcffd05`,
`4c019911b1c6c500a045fc3cf29bcab7225e9fee014d33bbeba78009b2cb9ee5`,
`036b25e47f4cdbbf44ee99d6d83eb61b02c61eec0f9738e51b6b262522607fec`,
`f3958b93f0dac80a35db19407e91930c28b0e72446d3e25ba7d222359282f199`,
`9032d4788dcaa01ce16adcdcad5104972d4ac6dcdeaf3c46ef3ca8ede1a8cf54`,
and `ca281d6d6d2d37fb566c02a415f1d54dc7e04192f2adaf492a4ee4ab817edf65`.
The extracted ext4 volume passed read-only `e2fsck` and contains only the ABI
4 provider.

Immutable release
`f01ed24223639e6b6881fed5372a66728388e8ea1f146a04de48277212e6c8ad`
is selected and running on the physical DE25 with Interface Kit ABI 4.1. The
Gallery's tabbed root is a column: the tab strip and active page now share the
same x-axis, and the page begins below the strip. The former right-edge
overflow was an obsolete `ASTRA_FLEX_BREAK_AFTER` retained on those root
children; in a column it correctly began another column. The constraints were
removed rather than weakening the shared flex semantics, and a geometry
regression retains the vertical relationship. The service is active with zero
restarts. Its clean image, ROM, QEMU, Interface library, Gallery, and display
helper SHA-256 values are respectively
`001a051a179f8f7e2d59471c3a5a368c3345b03808d3041a6dfab894d1c80652`,
`9fea35c45eff803e26ee66c2c9bf81d156c5d77a2df7fca550e0503b829d6530`,
`036b25e47f4cdbbf44ee99d6d83eb61b02c61eec0f9738e51b6b262522607fec`,
`67b7ed5ca577bd6e04f626a34bcbe172cb58fcd976526225a415ea4e7190a527`,
`e6b0c11bf40008c2151fdeed610eb451e89ef5709082af2883c77fc0fec77183`,
and `ca281d6d6d2d37fb566c02a415f1d54dc7e04192f2adaf492a4ee4ab817edf65`.
The extracted ext4 volume passed read-only `e2fsck`; its installed Gallery and
Interface-library bytes match the target build exactly. The Cam Link proof is
`/private/tmp/astra-gallery-tabs-fixed.png`, SHA-256
`591628e3b33320ee583d8b1e7ab4315e8631149df2cab6c97ea493db88f54fdf`.

Immutable release
`92aa26670bd04ed73c22dad072fb082d08fffccf5bfd8a185fd07df4aff33e43`
is selected and running on the physical DE25 with Interface Kit ABI 4.3.
ScrollModel, ScrollView, and Scrollbar pass host, sanitizer, analyzer, NDK,
and MC68040 gates; the release volume passes read-only `e2fsck`. A physical
wheel notch completed in one presentation and reduced the render batch from
the normal 153 commands to 53. Command zero copies retained pixels from
`(24,159)` to `(24,143)`, `260x104`, then only the exposed 16-pixel strip and
bound scrollbar repaint. The Cam Link specimen is
`/private/tmp/astra-gallery-retained-scroll.png`, SHA-256
`313ffb1e26c45b11c97b83c75ff3216da540e3a927f27899aad2d48af48787c7`;
the captured render mailbox is
`/private/tmp/astra-scroll-retained-mailbox.bin`, SHA-256
`56600bba31a6910b4ad78be24b79cdf0f26c0d59e20722fb0539d05a76efeafa`.

Interface Kit ABI 4.4 adds a splitter as the only new primitive required for
split views. The panes remain ordinary flex containers, so nesting, intrinsic
measurement, min/max constraints, damage, actions, and resizing retain one
implementation. The divider supports captured live pointer dragging and full
keyboard adjustment. Its validation is folded into the existing sibling
layout walk; interfaces without a splitter do not pay a second control-array
traversal. All host, sanitizer, analyzer, MC68040, NDK, and benchmark-parser
gates pass.

Physical acceptance release
`b83a032916fe6e0bf9fe6704fbc3855923f97e65fb12cb2143ef28e011456cce`
measured the complete three-control pane/divider/pane reflow at 26.953
microseconds per action across 10,000 live changes, inside its
30-microsecond gate. Its trace has SHA-256
`34bf6843d15a8f470f93dd66e65eabc832fcf5e4b28e399597ee3cb3c55e6e0d`.
The physical drag specimen is
`/private/tmp/astra-splitter-final-dragged.png`, SHA-256
`31ce41e37184a26c46090fd8b70706cf8c94b8803117cfa907a7fe9d9646eeb3`.
The normal desktop release
`3160fef3de25828a981d2cf9eaf133b0d6ccbb55cd3faef23fa1e4e9994337ff`
is selected and running at stage 8 with zero service restarts. Its clean ext4
image, Interface library, and Gallery SHA-256 values are respectively
`6ba4abc6f121379b6fa9051e69a1d8449c1c95c0332540ea3bc043bd5e2f27fe`,
`75409503f55837b9f29f6bbb18c50612cf1d999ae17d6341862600a68d3c6417`,
and `49c06d849e2d28d288a7c9d73e395a5547d45e6d24bb8a15547656152daff331`;
the extracted volume passed read-only `e2fsck`.

Current source advances the logical input service to protocol 2, the GUI
service to protocol 9/window-event ABI 4, and the public pointer observer to
event ABI 2. Normalized modifier state is captured on every logical input
event and survives input motion coalescing, display routing, window-local
delivery, and screen-space pointer observation. This is the shared prerequisite
for ScrollView Option-click/Shift-drag behavior, modified drag-and-drop, and
other pointer gestures; receivers reject unknown modifier bits at their trust
boundaries.

The preceding ABI 2.6 physical DE25 release
`ee84a8a336d68cc6469252690e3ac778790225bceda79ea7e7ecd1d425944ebc`
booted this slice at 70.038 MHz, reached stage 8 with the correct wall clock,
and remains active with zero service restarts. Its storage image, Graphics
library, Font library, Interface library, and Gallery SHA-256 values are
respectively
`51f0b89637c151b452feff64c1b8d5dcadfb58f005b990705cd033990f0efb05`,
`f1ed71716748d288bc1972b1cd10f5733ee413344cf4ad779effccbe25a1e345`,
`8381f37774653b834b7a83a12f627a411f5aa8b766399303ceb55d9742168859`,
`8ef9ab735ad969749c1230217b2d8df2b41ee3fe0cd9fd0b322f5c30058e13b9`,
and `851bf2c321e0b3321ab1db6d5ca35ebb33d0baa56096e8f14e941963e962ffed`.
The target measured 7.320 microseconds per contiguous append and 656.958
microseconds per deliberately maximally fragmented insertion across 4,096
edits; automated ceilings are 10 and 800 microseconds respectively. The
retained trace is `/tmp/astra-interface-ee84a8a3-ring.bin` on Beast, SHA-256
`1428bfb135d319866e9666301f06f22a23bb35b20f4a96c221251ea5d792635b`.
The Cam Link specimen is
`/private/tmp/astra-interface-field-ee84a8a3.png`, SHA-256
`6c9cf5236a4f3a20941b55d8727b3627990f75a11cb833a85a81ca160f73aafd`.

The canonical rescue font's encoded U+FFFD glyph was blank. The shared AFNT
importer now requires a visible replacement glyph and chooses visible U+FFFD,
`?`, or the source default in that order; surface and importer tests retain
that invariant. Unsupported scalars are therefore visible instead of silently
disappearing. The rescue face remains Spleen. The intended system stack is
Atkinson Hyperlegible Next for UI, JetBrains Mono for terminal and code, and
Noto fallback faces for installed-script coverage; these require the shared
A8 outline-import and font-service resource path rather than per-application
font copies. Current uncommitted work separates the Spleen rescue AFNT from the
normal desktop stack, imports the complete Atkinson Hyperlegible Next and
JetBrains Mono Unicode cmaps into A8 strikes, and extends both the hardware
draw-list lowering path and its RGB565 software oracle to consume A8 coverage.
The Fonts Settings contract now treats a multi-font drop as one durable catalog
transaction: format handlers report per-face progress, and any conversion,
validation, source, or storage failure publishes none of the batch.

The Astra OS and NDK version are one authoritative value,
`0.1.0-dev`, exported by `astra/version.h` and consumed by the kernel, boot
image, manuals, metadata, and package name. Beast certification produced the
deterministic relocatable archive
`astra68-ndk-0.1.0-dev.tar.xz` (2.8 MiB, SHA-256
`ef2ff330baf08f47b7c9c9742f26637ba0129d71a67994f9c83ff26dbb3e9a9e`).
Its self-check rejects unsafe or nondeterministic tar entries, validates its
internal checksums and shared version, extracts it, and links native C, POSIX
C, and POSIX C++ consumers using only archive contents plus the cross compiler.
The complete certification also runs every owned host, sanitizer, analyzer,
shared-library ABI, and link contract against the OS library implementations.
Every shipped public header must opt into the documentation gate; undocumented
public declarations, structures, members, callbacks, parameters, return
contracts, and unresolved references fail the build. The current strict
Doxygen warning log is empty, and both the HTML manual and 175-page, 574 KiB
PDF build successfully. Documentation artifacts are dependency-tracked so an
unchanged archive build does not regenerate them.

The shared-library architecture has been reduced to one policy path. Package
construction is the sole compatible-version selector and emits one canonical
provider record for every registered shared-library product. The VFS owns the
single exact provider resolver used by both supervisor bootstrap and the
process interpreter; the former Kit-directory fallback, its fixed scan tables,
and the kernel's unused "latest compatible" selection policy are gone. The
runtime loader alone owns `DT_NEEDED` traversal, symbols, relocations, TLS,
RELRO, and initializer order. The kernel accepts an exact library identity and
only validates, maps, shares, accounts, and reclaims pages. This boundary was
cross-checked against Haiku's user runtime-loader/kernel image split while
retaining Astra's narrower streamed-image cache because Astra VFS is a user
service rather than a kernel file cache. VFS, supervisor, kernel process, image
provider, and Kit provider-coverage tests pass on Beast; full image boot
validation remains the release gate for this uncommitted cut.

Commit `b36a784` is physically accepted for this fixed-grid cutover in immutable
DE25 release
`5b00754844ed0714b8ecce98fe8042a9d130031ba2014bfd1d49207818fecabb`.
The board completed POST, reached initial-image stage 8, reported an effective
70,196,000 Hz MC68040 and remained active with zero service restarts. Opening
Terminal and executing `echo textsurface-ok` produced the expected command,
output, and prompt on HDMI. The first output appeared in 108.041 ms and settled
in 205.463 ms through two display batches, two glyph commands, 189 renderer
commands, 29 fills, 158 blits, and two completed submissions. The retained
Cam Link frame is `/private/tmp/astra-textsurface-terminal-5b007548.png`,
SHA-256 `30f43a3f234208ad570ce0b22cfefc647b293a7f8f85c9a2cfea44d883babf23`.
Release artifact SHA-256 values are
`472897d5732523e4c455e44d89729afb45b267ec1564781a8448761b7b317085`
for `storage-terminal.img`,
`4c5606a57402fff5c404da0d0033eed975ed3f6454b2244f9fe32de12ea3df5e`
for QEMU, `a84456a3d4026e6fdca0d02fb0c949fa5e48040971b7781a7e2d75ddd86f47e9`
for the boot ROM, and
`c109cadab97c18500ee7bfb2bb4c4701542342b6effc48d6204832b0578f5bb1`
for the terminal display renderer.

A forced kernel rebuild exposed a stale 1080p contract: the advertised native
RGB565 frame is 4,147,200 bytes but a transfer slot held only 2 MiB. Current
source derives a transfer slot from Astraea's complete 8 MiB render workspace;
the old independent mapping constant and duplicate public DMA quotas are gone.
The anonymous private window correspondingly spans 480 MiB; allocation remains
demand-paged and owner-charged.

## Service tiers and user service policy

Resident services are `critical` (storage, display: death halts the machine
through the PID-1-only `ASTRA_SYSCALL_SYSTEM_FAIL`, which panics naming the
service), `required` (restarted always; includes the desktop) or user
services (media, anything added with `service add`). Critical and system
services refuse every service-manager operation with ACCESS. Users choose
"starts at boot" (`service enable`/`disable`) and the restart policy
(`service set NAME restart never|on-fault|always`, NDK
`astra_service_set_restart`, service-manager protocol v5); a manifest
service's choice persists in `/config/services/NAME/policy.conf`. Automatic
restarts back off (immediate, then 0.2 s doubling to 10 s, reset after a
stable minute). `docs/USERSPACE_ARCHITECTURE.md` §5 is the contract;
`emu/qemu/test-service-policy.py` (with the `fault-probe` fixture image
profiles) is the gate.

Restarting a service re-grants PID 1's retained device and IRQ handles,
resets devices the service alone holds, and masks and drains its IRQ
endpoints; the input service drops dead clients before judging a seat-owner
connect. No gate currently kills a service that holds an exclusive device
(display, the old test path, is now critical); that path was proven by
perturbation on 2026-09-26 and on the DE25 before display became critical.

Block completions reach their waiter only through the owner's completion
interrupt and its own collect loop: kernel maintenance drains the transport
only for a dead owner's revoked requests, and a storage lane collects once
after it becomes visible. Before this, a process exit could strand a storage
thread holding the ext4 mount lock (found on the DE25, 2026-09-26).

## Active: graphics performance

Target: 60 fps with ten TestDraw2 windows, the 040 idle, vblank gating only
the change of the screen. State, measurements, board overrides and the plan:
`docs/HANDOVER_2026-09-27_GRAPHICS_PERFORMANCE.md`.

## MC68040 contract

Every Astra target build uses `-m68040`; user software reserves A4 for the
thread pointer and uses software floating point. Axiom uses the MC68040's native
SRP/URP registers, three-level `7/7/6/12` translation, 4 KiB pages, format-7
access-error frames, address-selective ATC invalidation, and explicit cache
maintenance.

Only MC68040 exception formats 0, 1, 2, 3, and 7 are accepted. The maximum
frame is 60 bytes. Formats 9, A, and B and their older SSW interpretation have
been removed. The old private QEMU MMU/frame implementation and the unreferenced
standalone core-test ROM have also been removed.

The architectural authority is Motorola's MC68040 manual. Current normative
contracts are `KERNEL_ARCHITECTURE.md`, `MEMORY_MAP_AND_PMMU.md`, `ABI.md`,
`LOCKING_AND_PREEMPTION.md`, and `RESOURCE_OWNERSHIP_AND_FAILURES.md`.

## Kernel certification

Axiom's MC68040 kernel certification gate is complete on the current source.
Filesystem performance work may resume after the remaining Astra OS cold-boot
release gate.

Completed evidence on the current source:

- clean GCC release and debug builds;
- the complete 30-test host kernel suite;
- Clang ASan plus UBSan;
- GCC 13 `-fanalyzer`;
- Clang 18 build/tests and Static Analyzer;
- exact cppcheck warning, performance, and portability pass with zero findings;
- 80.1% aggregate host-test line coverage (15,563 / 19,418 lines);
- generated MC68040 inspection of exception entry, interrupt entry, scheduling,
  address-space switching, user copies, syscall dispatch, and port operations;
- allocation-site injection, pool exhaustion, rollback, emergency-reserve, and
  zero-free cleanup tests;
- three physical DE25 executions through the complete K1-K10 workload;
- 18,000 consecutive physical fault/create/reap soak cycles with constant free
  pages and no panic;
- a retained allocation-free panic report reproduced byte for byte;
- a stopped production snapshot after every resident service launched.

The exception-family cutover was developed test-first. The old implementation
failed focused format, fault-classification, context, and user-copy tests; the
MC68040-only decoder and fixtures now pass the complete clean host suite.

The retained physical `-Os` qualification image has SHA-256
`4f596cc90409a7e90bb8ac382e1191adb96d21a850f5e2fc36b51a17c71967a6`,
a 178,576-byte kernel, and a 344,084-byte ROM. Three deterministic runs on CPU2
reported 62.362, 61.714, and 62.411 MHz effective. The first two produced exact
path measurements: syscall 3,382 cycles; timer 631; user fault 22,991;
scheduler pick 34; same-space switch 65; cross-space switch 70; wait block 74;
wake 111; port send 327; and port receive 779. The third differed only by one
guest tick in a few paths. The longest interrupts-disabled interval remained
inside its explicit 125,000-cycle gate.

These are guest timebase cycles, not literal physical CPU clocks. The board
run is physical throughput evidence; Beast measurements are development-only.
The 18,000-cycle physical soak used ROM SHA-256
`a56d7f35d59cf0f3966507d596a6aef9cffe0573a251915369e2127aa60f379b`.
All 21 checkpoints passed, free pages remained exactly 30,945, and 36,032
context switches had completed at cycle 18,000. The retained Beast copy of the
physical log is `/tmp/astra-k1-040-soak-20260907.log`, SHA-256
`9e0c62cb381c0ed64cb83926bb90edc508dfad664ba968c022b3e726b1b452b0`.

Removing process-launch scratch arrays from the common syscall stack reduced
the generated dispatcher frame from 4,292 to 712 bytes in qualification and
716 bytes in production. The corrected physical K1 poison scan reported a
3,244 / 8,192-byte high-water mark. A GDB poison scan of the uninstrumented
production kernel after storage, hostfs, network, and ntpd launched found the
worst thread at 4,268 / 8,192 bytes, leaving 3,924 bytes. That snapshot contains
9 live processes, 14 live threads, and 52,941 completed syscalls; its retained
log is `/tmp/astra-prod-kernel-stack-20260907.log` on Beast, SHA-256
`fce2a82f581fd2a995d419b3dc959f6abd21613cfe39e81920d157743a7852c1`.

The retained panic test used ROM SHA-256
`698ac6b255517b5af36b5a78b2a359da9b63449f784bfe668c79166dc587d0ba`.
The launcher's retained report and `panic-latest.log` are byte-identical,
3,377-byte records with SHA-256
`312e4b9e2f7418b138a3e3dfaad579b801a078d984f6a4bc07f1a866eaf1376`.

An `-O2` experiment expanded the kernel to 247,200 bytes and did not complete
the same physical qualification workload after the deliberate user fault. It
is rejected pending root-cause data; size is not the rejection reason.

The complete evidence matrix and disposition are in
`KERNEL_CERTIFICATION.md`. The remaining release work is outside the kernel
gate: cold-boot the selected immutable release through the native NTP barrier,
stage 8, the real display, the terminal command gate, and repeated boots.

`astra.service` is enabled and the board had cold-booted immutable release
`8786083b44a9b91d3e5a8123093cc855953497824ee1239ab318c9c0b898bf0f`
through native time synchronization, journal recovery, storage, hostfs,
network, ntpd, and the real display path. Immutable observability release
`9a8505ab47df89008a529437cedb56e507363050c23eca5d8f46bb444607e97e`
is now selected, byte-verified, and running. A physical console SysRq sequence
synced storage, remounted it read-only, and reset the board. The following cold
boot passed the native NTP barrier, reported the correct 2026 wall clock,
verified and recovered ext4, launched the metrics-enabled hostfs, network, and
ntpd services, and reached stage 8 through the real systemd/display path.

## Runtime and storage boundaries

Linux owns physical storage and networking. Axiom exposes mechanisms; protected
services own filesystems, paths, configuration, networking, graphics, input,
audio, and launch policy. VFS remains filesystem-agnostic.

Filesystem Phase 1 is active on the `fs-phase1` correctness checkpoint,
including Task 5 review fixes, Tasks 6–12 and final review fixes.
HostFS owns grants and provider sessions;
clients perform file operations through their own kernel-bound scopes, and
QEMU enforces confinement. Storage protocol 36, Filesystem library ABI 12.0
(Filesystem.kit 9.0.0),
libc.library 4.0, runtime.library 1.16, and syscall ABI `0x00010043` are the current branch
contracts. Task 5's rights-mask fix rejects transfers with no usable right
after import filtering and rolls back invalid replies without moving handles.
Derived grants are retired before the next control request after their deriving
session ends, including process death and cascading child grants.
The resumed checkpoint passed the kernel and userspace suites, all 30 t9
integration gates, and NDK contract/documentation checks on Beast. New
five-pair stat/read comparisons failed the unchanged latency budget:
5,000 operations/run gave read median 1.114x and excess switches; a longer
40,000-operation diagnostic gave stat p95 1.227x. HostFS CPU was zero
ns/op and syscall counts unchanged. Performance remains an open release
qualification issue. The user's 2026-10-10 direction is to finish correctness
first, then qualify performance; it does not block the remaining semantic
work. Tasks 6–7 now pass their correctness checks: ordered path resolution,
provider identity, explicit metadata validity, and typed file/directory removal.
The target build, host suites and sanitizers, all 18 guest semantic cases,
111 terminal commands, provider image checks, NDK checks and provider coverage
pass. The final t9 run passed 30 of 31 gates; its parallel SDL timer reported
a 167 ms delay for a 1 ms request. Three isolated reruns passed unchanged
thresholds (1.043–1.048 ms periods and 1,000 ms one-second delays); the failed
parallel checkpoint remains recorded. Task 12 integration-1 later passes the
same parallel timer thresholds. The reply carries metadata
beside the continuation, and every valid 191-byte native pathname now fits.
That Task 7 checkpoint requires ABI 8. Sources and artifacts are verified in
`fs-phase1-task7-20261010/attempt-5` on Beast.

The continuation is `docs/HANDOVER_2026-10-10_FILESYSTEM.md`.
Task 9's provider checkpoint `fs-phase1-task9-20261011/provider-3` passes
raw directory PATH rename/removal/confinement and the existing HostFS gates.
Host version 0x00010012/metrics 5 and native CWD pass the complete semantic
checkpoint `native-6`: host/sanitizer suites, MC68040 build, raw provider,
NDK headers/coverage and HTML/PDF, all 22 FSSEM cases, lane (509 commands for
500 stats, three idle) and provider fsck. `semantics-2.log` records the correct
fixture; the first guest invocation failed before boot. Process VFS owns CWD,
shared by native facades and POSIX, with fork adoption and exec state.
Filesystem Kit ABI 10.0 and STOR 33 are qualified. An explicit four-byte CWD
mutex alignment fixes native-4's shell stall; native-6 checks its target
address. Exact source and artifact evidence is retained on Beast. Local
Task 10 recovery changes are staged after that archive, with Host version
0x00010013 and STOR 34/minimum 34, retaining Filesystem Kit ABI 10.0. HELLO
resumption preserves the control session and derived grants while replacing
the scope. Disconnected descriptions can be inherited without reopening a
path; recovery verifies the original grant root's volume, inode and generation.
Supervisor records delivered namespace dependencies and keeps publication
clients at stable addresses. `native-3` passes hosts/sanitizers, target build,
raw provider loss (12 resets), NDK/docs, all 22 guest cases and lane. Its
provider-reset fork failed ENOSYS on an unused revoked scope. Native-4 fixes
that and passes the loss probe, but the existing shell's next fork exposed
SHARE skipping the refresh guard. The common guard regression starts red
and the VFS suite now passes. Native-5 passes host/sanitizer/build/raw and
NDK HTML/PDF gates, all 22 cases, lane and provider reset, but its HostFS-death
completion marker timed out. Native-6 passes isolated HostFS death and fsck;
native-7 passes the full sequential 22-case, lane, reset, HostFS-death,
managed-holder restart, fresh Terminal and fsck gate with exact source and
artifact hashes. Task 10 correctness is qualified; the earlier timeout is
retained. Native-16 and integration-1 qualify the combined Tasks 5–12 source.

Task 8 host VFS/POSIX suites, the full MC68040 build, raw provider gate,
all 21 guest semantic cases, lane gate and provider fsck pass in
`fs-phase1-task8-20261010/native-12`. Sanitizers and public-header documentation
pass in `native-11`; the directory-path helper also passes host/sanitizer
checks in `native-12`. Shared descriptions own positions,
directory cursors, append and nonblocking flags. Fork uses one-use grant-bound
receipts and waits for child adoption before returning; exec also retains
child wait handles and accounting. Process client records now keep stable
addresses through namespace-cache growth. Fork rebind preserves its negotiated
features. Directory batches hold the provider lock for the whole transaction,
read its current cursor, and roll back entries that do not fit. The new guest
fork/exec/append regressions and NONBLOCK/CLOEXEC checks pass. The final broad
integration passes in integration-1; Task 7's failed timer checkpoint is retained.

Tasks 5–12 correctness is qualified by native-16, integration-1 and
`de25-calibration-3`. Integration-1 passes all 31 gates plus desktop applications,
provider index and events, NDK contract/documentation and provider coverage.
Its 111-command Terminal, six shutdown/restart modes, remote desktop, all 26
semantics, root-helper 20 checks and 1600-operation stress pass without relaxed
thresholds. The parallel SDL timer passes at 1.051856 ms with callbacks
99/299/64 and a 1000 ms one-second delay. All 164 source hashes remain unchanged
through the run; source archive, gate logs and artifact hashes are retained in
`fs-phase1-task12-20261011/integration-1` on Beast. DE25 provider qualification
of the atomic write-bound correction passes in `de25-calibration-3`.
Launcher admission defaults are measured;
its termination, lock and power-action regression gate passes.
The warm-command fast path (Task 12b)
and final DE25 performance qualification (Task 13) follow correctness.
Task 11 kernel/VFS hosts and VFS ASan/UBSan pass: cache first-use concurrency,
facade lifetime, 96 scopes, allocation rollback, 16 DMA buffers and hole reuse.
Scope records use reusable metadata pages in place of the old fixed pool.
Native-4/5 pass the full MC68040/QEMU build, raw quotas/rollback/twelve resets
and NDK checks. Native-6 passes all 25 guest cases, 16 simultaneous lanes,
owner session/file exhaustion, memory holds, full-quota reset with old
identities lost, shell/desktop, lane509/500 idle0, provider reset after585,
HostFS death after703 with managed restart/fresh Terminal, and provider fsck.
Evidence: `/mnt/Documents/astra68/builds/fs-phase1-task11-20261011/` on Beast.
Native-5's nonexistent `/dev/null` flood fixture failed and remains retained.
File/DIR holds return to 23 host FDs, five provider descriptors and one DIR;
control sessions add no provider descriptor. Whole-QEMU RSS growth includes
workload memory and is not a DE25 admission calibration. Host version
0x00010014/configuration4 stamps scope quotas; HostFS CONFIG supplies positive
file/session/queue limits. A thread's channel still carries eight scopes,
with revoked-slot reuse tested.

Task 12 adds optional SYSTEM volume2 without creating it when absent. A new
pre-service Supervisor probe uses the canonical HostFS transport/backend,
compares the exact lwext4 manifest, logs match/mismatch/absence, and releases
file/session/claim and DMA/channel resources. Cleanup failure stops startup;
absence or mismatch remains nonfatal. Host cleanup/short-read/failure tests,
settings negative tests and sanitizers, target build, raw optional-system and
claim-handover tests, and NDK HTML/PDF pass. Task 12 native-3 also passes all
25 semantic cases, budgets/flood/lane/reset/HostFS death, fsck, exact/absent/
mismatched bootstrap gates, and the DE25 cross-build. The probe must compare
before the manifest parser mutates its input; native-2's false mismatch is
retained. The later native-16/integration-1 checkpoints qualify the combined
source. Isolated AArch64 calibration
passes all raw provider gates as UID/GID 65534 without touching the live release.
Its 1000-file/200-DIR holds return FD/description/DIR counters to baseline;
DIR RSS growth is 30.72 KiB each. File RSS reused prior allocations, so zero
observed growth is not zero allocation. Evidence is under Task 12
`de25-calibration-2`. Conservative launcher defaults are staged at 2048 objects/
descriptions and 1024 DIRs: 8 KiB per description/object plus 40 KiB extra per
DIR totals 56 MiB, below the combined 64 MiB allowance. QEMU also reserves
256 host FDs regardless of explicit environment limits. The new 26th semantic
case failed in `append-before` without identifying its failing operation.
Native-12's staged diagnostic found signal-handler installation failed before
any write. The independent code audit found finite RLIMIT_FSIZE used a cached
position before append and refused zero writes at the limit; its correction is
staged in Filesystem ABI 11/STOR 35/Host 0x00010015.
Every native caller, inline/bulk/direct transport and backend now carries one
generic max_end bound, enforced with the live write position under the existing
provider lock. Native-16 passes the complete kernel suite, VFS/POSIX/ext4/
HostFS-settings/Supervisor host and sanitizer checks, MC68040/QEMU build, raw
provider gates and NDK contracts/documentation. All 26 guest cases pass,
including independent append descriptions, positioned/zero writes, SIGXFSZ
delivery, bounded PROC control refusal and unchanged size after refused
ftruncate. Budgets, full-quota reset, shell/desktop recovery, provider reset,
HostFS death/managed restart and provider fsck pass. Bootstrap match, absence
and mismatch and the DE25 cross-build pass. `de25-calibration-3` passes the
current raw bounds/races/confinement/quotas/twelve-reset/bootstrap gate on
AArch64 as UID/GID 65534. Its 1000-file/200-DIR holds return FD, description
and DIR counters to baseline; DIR RSS growth is 30.08 KiB each. The raw qtest
write diagnostic includes Python/socket/doorbell overhead and does not qualify
native performance. Integration-1 passes the final broad gate. Native-16
finishes exit zero with all 164 source
hashes unchanged through the run.
Evidence: `fs-phase1-task12-20261011/native-16` on Beast.

The signal regression exposed three native fork defects. Signal configuration
now uses the existing writable-user-buffer preparation to resolve copy-on-write
signal stacks. Shared thread retirement accepts suspended READY threads without
removing them from a queue they already left. Copy-on-write replacement now
uses the existing ownership-transfer step when the original owner writes,
preserving the child's original page when the parent is reaped. Kernel tests
cover handler reconfiguration after fork, paused-child termination, both writer
directions, surviving data and full memory recovery. Earlier failed checkpoints
and the reaper diagnosis remain retained. POSIX limit signals now use the
existing native-backed raise() path instead of bounded PROC control writes.
The launcher termination test also exposed and fixed Bash EXIT-trap bare
return inheriting an interrupted status, which skipped helper termination. Read-only inspection found its currently running unrelated
release `1d7137c1adfdec99518e65d1651e99846970ae0c3296852ab4cff93957032b1c`
running QEMU as root; isolated gates use an unprivileged caller. Production
privilege/drop and permissions must be corrected before a new release is
accepted. No release has been published or deployed by this continuation.

The DE25 production storage image is immutable per release and copied to a
writable per-release state image. The host filesystem root must exist before
QEMU starts. Astra startup waits without a deadline for the native
`systemd-time-wait-sync.service`; a guest never starts with an invalid host
date.

The DE25 root filesystem boots with the explicit kernel option
`rootflags=commit=5`. A physical boot after `saveenv` verified the exact kernel
command line, synchronized NTP, and normal `astra.service` startup. The prior
`commit=1` policy is rejected: ext4 now uses its normal
five-second journal interval while ordered journaling, write barriers, and
explicit `fsync`/`fdatasync` durability remain intact. The live root is
`rw,relatime`; `findmnt` omits `commit=5` because it is the ext4 default.

The retained filesystem baseline release carries QEMU SHA-256
`c06b6fab7e4b88d4e0e921d906801316a2da7a8d6002e39be8dd0c3840c14316`,
production ROM `a8e34319e74ac9efc60f1056fc452cba5566a462a997b0c60e36a69a7784b685`,
and storage base
`3d05392f0cf20c09af186845620c1d11f8115311ad0fc253b8f2415b122d9c2f`.
The display and launcher hashes are respectively
`fe62b9c45b45aa493e6eb4f3fd0554d95f54f0c2b168ac1140238f7c7a78fb87`
and `1862251c1c8c6c18df55f17189c9daf4a1136c50f5c46919594136c2822909b6`.
A fresh physical host-channel run completed 901,136 commands with 2,048
requests in flight and peaked at 645,753 requests/s at depth 1,024. This proves
the cross-boundary worker machinery and four-core placement; it is not a VFS
or durable-filesystem throughput claim.

The retained Cortex-A76 QEMU experiment improved the physical no-poll baseline
from 1,110 to 1,183 operations/s in RAM and from 667 to 725 operations/s on
SD-backed ext4. The retained shared C completion window then reached 1,665
operations/s in RAM and 795 operations/s on ext4, with exact accounting and
clean independent `e2fsck`. An assembly version was rejected after paired
physical medians regressed from 1,618 to 1,288 operations/s by reaching the
sleep path sooner.

Operation-specific physical measurements locate the durable mutation cost:
reads reached 3,992 operations/s, renames 2,682, repeated syncs 4,543, and
open/truncate/write/close reached 701. Host timing attributed 98.508 of 133.2
aggregate seconds in that write run to open/truncate. After removing
`commit=1`, the existing native SD durability benchmark completed and verified
4,096 records: one `fdatasync` per record reached 183 operations/s, four
records per flush reached 835, and eight reached 1,685. This proves that the
remaining >=1,000-operations/s design needs safe group commit; disabling
barriers or acknowledging uncommitted data is not an acceptable route.

The complete 524,288-operation, eight-worker guest/VFS stress run on the
physical DE25 and SD-backed ext4 `commit=5` path passed exact accounting at
733 operations/s and 1,191,513 bytes/s, followed by a clean independent
`e2fsck`. It completed 1,341,528 host commands with 2,717,776,160,647 ns of
host execution and 80,284,785,733 ns of queue time. The retained log is
`/tmp/astra-fs-commit5-20260907.log` on Beast, SHA-256
`1bcee433a7cb906b3d6eaf13f65f8e53abd6c3fac9b6e6497af78eb6b3cd0791`.
This is the current durable end-to-end baseline; it does not yet meet the
1,000-operations/s target.

## System log, phase 1

Branch `system-log` builds phase 1 of `docs/SYSTEM_LOG.md` (through
`a3f666f7`). Every program's lines go through its own lock-free ring to the
events service, which writes one time-ordered file: `/events/syslog` on the
boot volume and a live host copy at `WORK:.astra/syslog`. Reading it is in
`docs/DEBUGGING.md` section 2; the ABI is in `docs/ABI.md`, "Time page" and
"Log ring". All 16 QEMU gates, `test-system-log.py` with and without
`--no-sweep`, `test-events.py`, `test-terminal.py` and
`test-desktop-apps.py` pass on the branch at `a3f666f7`, and again, with
the final-review fixes (fork and quiet checks added to
`test-system-log.py`), at `986ef6f3`.

DE25 release
`1c6898197a3811d0d12e64b369be92bb4ad3915b520fc1fbfa3552797bbde985`, built
from `a3f666f7` by `emu/qemu/publish-de25-release.sh`, was the first
installed. The QEMU the service runs is that release's
`qemu/bin/qemu-system-m68k-astra` (from `/proc/<pid>/exe`), and it carries
the block device model (two `Astra68 storage image` strings). Doom opened
over the remote desktop wrote its banner to
`/var/lib/astra/hostfs/work/.astra/syslog`, in two boots after the install,
for example:

```
2026-10-07T06:25:48.865+00:00 notice chocolate-doom[00000011] stdout: V_Init: allocate screens.
2026-10-07T06:25:49.028+00:00 notice chocolate-doom[00000011] stdout: W_Init: Init WADfiles.
```

`de25-ab.sh -n 2 -s 10` against the previous release
(`c023fdc6c10bc5c964030b788c80d431e1531598d36a549c89aaf83fe0c1fe66`) with
Doom running. This compares the whole branch against the previous release,
not the time page alone, with n=2 rounds:

| Window | Previous: vCPU thread | Previous: guest idle | Branch: vCPU thread | Branch: guest idle |
|---|---|---|---|---|
| idle | 82.0%, 80.1% | 21.6%, 22.9% | 82.9%, 81.5% | 20.1%, 20.2% |
| motion (125 Hz) | 88.0%, 85.2% | 8.3%, 7.5% | 86.0%, 84.2% | 6.4%, 7.3% |
| idle after | 81.1%, 80.6% | 21.8%, 22.2% | 81.8%, 83.0% | 20.5%, 19.0% |

Doom presented 35.1-35.3 frames/s idle on both, with no audio gaps.

The ~2 points and the ~35 extra cross-space switches/s were not the time
page. They were a stuck kernel-trace cursor (`ded44dc0`):
`kernel_trace_drain_user` advanced its cursor only on a returned user
record, so once Doom's start-up page faults wrapped the ring past every user
record (program text now goes to log rings, so nothing refreshes it), every
1 s sweep reported the same ~2,000-slot gap again, and the events service
counted it as loss and rewrote `/events/store.N` through the storage
service. The cursor now means "examined through" and a gap is reported once.
`.superpowers/sdd/system-log-switch-investigation.md` is the diagnosis.

DE25 release
`701e4c0fb2102d34d24157c381d523ea613b0b3dd86d336a16e9122753b9913c`, built
from `986ef6f3`, is installed and current. `de25-ab.sh -n 2 -s 20` against
`1c689819` with Doom running (per-process figures from the same astra-top
windows; the second `1c689819` round stopped in its idle-after window
because the sampler stopped writing, and one more `701e4c0f` round was taken
with `-n 1`):

| Window | `1c689819` cross-space/s | `701e4c0f` cross-space/s | `1c689819` guest idle | `701e4c0f` guest idle |
|---|---|---|---|---|
| idle | 282, 284 | 257, 250 | 22.4%, 21.9% | 23.8%, 22.2% |
| motion (125 Hz) | 401, 393 | 388, 382 | 9.8%, 9.6% | 10.9%, 9.3% |
| idle after | 287 | 259, 254 | 18.2% | 20.1%, 18.1% |

- events: 8.0-8.8 runs/s (19 syscalls/s) idle before, 1.2-1.3 runs/s
  after; storage: 10.3-10.8 runs/s (123-125 syscalls/s) before, 0.3 runs/s
  (under 1 syscall/s) after. Doom's own runs/s are unchanged.
- Cross-space switches are back to main's ~255/s (main measured 252-257).
- Over 74 s of Doom on `701e4c0f`, both store banks read from the board's
  storage image (copied, journal replayed) held generation 0xE and 0xF and
  `lost_in_transport` 0 at both ends; QEMU's `write_bytes` grew about
  4.6 KB/s (hostfs sample and syslog included), against about 34 KB/s of
  store rewrites before. Whether the trace ring wrapped in that boot was
  not checked (the QMP socket is the input helper's).
- Guest idle on the idle windows overlaps main's range (main 21.6-22.9%),
  with n=2.
- Open: on `1c689819` the sampler twice stopped writing
  `WORK:.astra/sample` while QEMU kept running Doom: at 15:26 board time
  with Doom long up, before this A/B, and about a minute into its second
  `1c689819` round. Not seen on `701e4c0f`
  in its three rounds; not yet explained.

## Developer observability

`PROC:` is the supervisor-rendered, capability-gated process view used by `ps`.
Its source is Axiom's complete live-process registry, not the supervisor's
launch bookkeeping, so services, desktop applications, Terminal children,
forked processes, and `ps` itself follow one visibility rule. Only the initial
supervisor can take the complete fixed-slot snapshot; commands receive only the
read-only `PROC:` mount. Records include measured CPU runtime, elapsed lifetime,
resident memory, state, scheduling, syscall, handle, and fault information.
System and service counters remain separate through the read-only `METRICS:`
capability. Hostfs owns that view because it already owns the host-device
channel and its VFS service; neither underlying authority is delegated to
commands.

`METRICS:snapshot` contains versioned fixed records for block traffic and
durability transitions, host-channel submissions/completions/execution and
in-flight depth, hostfs VFS protocol/session/file/resource accounting, and
calls plus execution time for every filesystem operation. The `metrics`
command renders those records as `group.name value`. A current-source QEMU
boot passed the complete 71-command terminal gate, including ordinary-process
reads of `host.channel.commands`, `hostfs.vfs.requests`, and
`host.fs.open.calls`; the command completed in 0.27 seconds in that gate. The
same gate verified PID 1 as `ROM:supervisor`, the top-level `PROC:` layout,
resident library accounting, and installed library versions, ABIs, binary
sizes, and provider paths. Its ROM and clean storage-image SHA-256 values are
`f67e96036f3434ffbc31fe5a7cc43b400c6a1f21ee1798e858503def6f6d8119` and
`9ccb9456ac04e9dea8932022bf9800878469503061062696e8703efe68b1c887`;
the host QEMU SHA-256 is
`b7e5cf2ca611fe35fff492527051fe439f9bfde352476728dcb766d1793a3cb0`.
The installed release then passed the physical cold-boot gate described above.
A focused terminal driver running the production AArch64 QEMU directly on the
DE25 also launched `metrics`, found the three independent host-channel, VFS,
and filesystem-operation groups, and completed in 1.12 seconds while the
production instance remained active. Its retained Beast log is
`/tmp/astra-metrics-physical-20260907.log`, SHA-256
`68b78603a69181a5e4490550480de6fdb9ae556ac62d335b9be3791947b6cf6a`.
The complete 71-command gate used the
same ROM and storage content under the current-source host QEMU so that its QMP
driver could inject input and inspect the trace without granting physical-board
users control of the production QMP socket.

The last complete terminal gate passed 84 commands, including upstream
zsh arithmetic, PATH lookup, exit status, nested command substitutions,
external pipelines, stdin, stdout/stderr redirection, and Lua execution. The
intermittent `lua: wait failed` / `POSIX VFS:16` class was measured as
`ASTRA_SYSCALL_CANCELLED` interrupting a VFS reply wait after its request had
already been sent; treating that as peer death closed a healthy storage
session. Internal protocol and synchronization waits now share the runtime's
restart-after-signal contract, while application-facing POSIX waits remain
interruptible. A regression injects cancellation between request and reply.
That exact clean Beast QEMU gate used image SHA-256
`3bb30aac26ef298c794130b99950ee11008c9b485ebce76ba305cbddcaf98f60`,
ROM SHA-256
`6b55f9e5b786705031aa51fbc2d7b7452e9c2595b7b396ed85820a9b828c70b4`,
and QEMU SHA-256
`1302f467d8e63ff3af38d94a57b5cdb7d37e75fe764714fe40f484ea35d71999`.
The image's extracted ext4 volume passed read-only `e2fsck`. Physical DE25
cold-boot qualification of this source remains outstanding.

A subsequent clean focused gate proved the shared process view and zsh's
interactive contract on the MC68040 target. Its 14 scripting cases passed in
0.34--0.77 seconds each, then a second Terminal's `ps` listed both Terminal
processes, the live interactive `zsh`, and the observer `ps`. ZLE editing,
history replay, clean exit, and Terminal session recovery passed. The clean
image SHA-256 was
`b9cf7b7eb83e874623a9da423b033bd9159121b1004053c879fe785be55f0152`,
the ROM SHA-256 was
`769968541d13d860bf4b835aab4af23893dac3be23ae065b4e6d5a4088cb5e11`,
and QEMU SHA-256 remained
`1302f467d8e63ff3af38d94a57b5cdb7d37e75fe764714fe40f484ea35d71999`.
The extracted ext4 volume passed read-only `e2fsck`; physical DE25 cold-boot
qualification of this source remains outstanding.

Immutable 512 MiB release
`b05969f66fe1a4ce0b70d1fe595c4a0ea40dc32a8bbb0ad5aa079955649a11c4`
is selected, byte-verified, and running on the DE25. Its AArch64 QEMU, ROM,
clean storage base, display helper, and launcher SHA-256 values are
`09f177837ef3c0d0a2dc0c482bc98e9935e9c2919e1387269f782bf15bd604bd`,
`96dcfa24ca2f39ba163e1c36c7122d94e5f9d65e012b1e69dc9e8f311331f30f`,
`9bb24543fbf2f69c008b8c0d946eeb1173cbdead9224dd781c35e712406c2c42`,
`ce47c8af92de431758739cbc4c95fe0d7996ff232000330f849bdffcc18f9743`,
and `b413bf10b7ddb11990f510aa5ff2f5f099e7d09fa7115f87ba027c065d632e74`.
The extracted ext4 volume passed read-only `e2fsck`. A physical service start
reported 512 MiB, passed full-range POST, exposed 131,072 pages, recovered and
verified storage, launched every protected service, and reached stage 8 at
70.117 MHz effective. QEMU settled at 553,940 KiB RSS; the systemd unit was
active with zero restarts and the terminal display resident.

The 512 MiB Media RAM window passed stuck-address, random-value, XOR, subtract,
multiply, divide, OR, AND, sequential-increment, and all 64 solid-bit patterns
with zero mismatches. The following block-sequential phase was stopped at the
operator-selected acceptance boundary rather than represented as completed.
The exact shell also passed calibration/readiness, splash CRC32 `611029ee`, and
the 512 MiB arena identity check.

All window-content scrolling uses the shared draw-list copy operation, which
the renderer lowers to an overlap-safe FPGA BLIT. The boot console now uses the
same renderer and AFNT font, retains exact state for both scanouts, repairs any
coalesced changes around a measured-cost scroll candidate, and presents only
complete inactive scanouts at vblank. It no longer writes a live framebuffer.
Render batches carry their exact live high-water mark, and the host copies only
the protocol regions the hardware may read rather than reserved ring gaps.

Physical DE25 counters measured the resulting steady window-scroll frames at
102528 bytes, 37--38 commands, 0.28--0.34 ms of graphics-memory transfer,
1.08--1.15 ms in the FPGA renderer, and 2.30--2.49 ms total render handling.
Presentation then waits for the next 60 Hz vblank, so sustained visible
scrolling is refresh-limited rather than renderer-limited. Before the retained
transport change, equivalent frames copied 139264 bytes in 7.6--7.9 ms and
took 9.6--10.0 ms of render handling. Coalesced boot-console scrolling fell
from 103--110 ms hardware repaints to roughly 19 ms hardware batches, with
about 2.2 ms of transfer. A physical 121-line zsh scroll workload completed
without corruption or a service restart.

Immutable release
`7a7abd27067563523838288cb8aeb00fcb6acd56acd50d97666d8243bae2daea`
is the current byte-verified DE25 runtime. It reached stage 8 with zero service
restarts. Its predecessor
`2e05b83e3bb8feada63d017e473829ad011095b04ee94e4c52c356c3a71a25a8`
is the automatic rollback release; these are the only unpinned release and
state generations retained on the board. The root filesystem has 18 GiB free
and is 35% used.

The apparent recurrent zsh crash was an old capture predating the active
release. The actual zsh failure was nevertheless fixed at its shared process
boundary: ncurses reserves 65,552 bytes in `_nc_read_tic_entry`, exceeding the
former 60 KiB usable stack ceiling. User threads now receive an 8 MiB virtual
stack reservation with a guard page and demand-paged physical backing; stack
page accounting is wide enough for the reservation. The unchanged zsh binary
starts, executes `echo zsh-ok`, and returns to its prompt on the physical DE25.

Render-batch ABI 1.1 carries the hardware-pointer state with the scene, so a
window move submits and commits one scene-plus-pointer transaction at one
vblank. A 60-sample physical drag produced 30 submissions, 30 render batches,
and 29 completions while input was active, then 34/34/34 after settling; the
former path submitted a separate cursor request for the same input sample.
This removes split-frame state but does not yet meet the 60 Hz movement target.
Cycle measurements identify the remaining limit without inference: a moved
Terminal frame spends about 25--27 ms in three large hardware passes--a
434,928-pixel background fill, a 434,928-pixel desktop restore, and a
403,440-pixel Terminal blit. Hardware-managed window planes/z-order, or an
equivalently measured compositor design, is the next display architecture
work; cursor-path micro-optimization cannot solve this bottleneck.

Immutable release
`803e2052cb2a4904c3b68160e9fc6e41c20566c014cd4a5cb8f1ceb2568eaa3f`
is selected and running on the physical DE25. Its QEMU, ROM, clean storage,
display helper, and launcher SHA-256 values are respectively
`fc95b55c8f71762f7df9f0a6576329dc79f89db2b4fbeed14cb3d7827fb6e15a`,
`e3edf0a710f59f8007473f16b6f444623b8cd13b890ac74af7c4efb6f7e42ebd`,
`050ed6034110483de8f79e1285cee29b74b9823b18765fb7a524ce19a5662fe9`,
`ca281d6d6d2d37fb566c02a415f1d54dc7e04192f2adaf492a4ee4ab817edf65`,
and
`1e76f48511636d1cc9c479c3f7ae90412322691b9af6c3b33a026d454b7b6092`.
The extracted ext4 volume passed read-only `e2fsck`; the runtime reached stage
8 at 70.014 MHz effective with the renderer resident and display submissions
and completions at 2/2.

The preceding visible freeze was not a kernel deadlock. All 11 processes and
15 threads remained alive and blocked on their expected wait objects while the
MC68040 idled. The guest render builder allocated every record at four-byte
alignment, but Astraea requires 32-byte surface descriptors and 16-byte glyph
arrays. A second source descriptor at `0x0081a310` therefore reached hardware
misaligned, was rejected as `ASTRA_RENDER_STATUS_BAD_DESCRIPTOR`, and the host
renderer exited while the display service waited for a completion. The shared
builder now aligns each record to its protocol requirement, and the host
validator enforces the same contract before submission.

The launcher now supervises both host display and input helpers instead of
waiting only for QEMU. A dead helper is reaped and replaced while the guest
continues. A physical SIGKILL of renderer PID 19446 produced replacement PID
31193; QEMU and the Astra service retained their PIDs, systemd recorded zero
restarts, the renderer returned ready, and the last accepted display pair
remained complete at 2/2. The launcher regression also forces one renderer
exit with status 42 and proves that its replacement runs before QEMU exits.

Interface Kit ABI 4.5, GUI protocol 12, render-batch ABI 1.2, and display
mailbox 1.5 now carry semantic and custom pointer images end to end. The shared
NDK window API selects the built-in arrow, horizontal resize, vertical resize,
text I-beam, or wait image and copies application RGBA pixels plus hotspot into
an immutable window-server-owned area for a custom image. Interface Kit maps
field and splitter hover/capture state to the same identifiers; applications
do not access display MMIO or reimplement pointer policy. Its one update entry
point accepts `ASTRA_POINTER_SHAPE_AUTOMATIC` for those control semantics or an
explicit built-in/custom shape as a deliberate window-wide override;
automatic hover changes cannot replace a wait or custom override.

Custom replacement remains transactional until the hardware accepts the new
image, and image, hotspot, position, visibility, and scene commit at vblank.
All NDK, Interface Kit, display-service, graphics, kernel, MC68040, AArch64
renderer, and QEMU build gates pass. Temporary physical Gallery release
`ab8de7c48ef6059ec25f89711cbfb6e5e42ef5e3acff4fe5556a174d2cc30f02`
reached stage 8 with zero restarts and visibly produced the default arrow,
I-beam, horizontal resize, and vertical resize cursors. The normal desktop
release `c5238d4b0f76e5d4a15ae80494c52ede9993c3eb65bb996914b34706083f5abd`
is selected and byte-verified on the DE25. It reached stage 8 at 68.464 MHz
effective with the renderer resident and zero service restarts. Its QEMU, ROM,
clean storage image, display helper, Interface library, and Gallery SHA-256
values are respectively
`8a7b8be1707379b8b40c49a19fe4df73d7a1091c37dfdaecaa5e792066fc9540`,
`53c4b0a9c757d2d730f2bfce414e282611abedbb95ab9c22cd6b015ad177762c`,
`6fff5cfe0124777fd906a489e045c800f957a1b352b52353dcbb053894d295f7`,
`abbc0720a0d9bd23c02260486e8f9f3db5838b59be55c4fd700e67d85655876d`,
`9534e426200ea517d98d29f789f554806d0d59763b79fbfc8e5adb8b638ae958`, and
`b609dc01d9bd556e1923210a376de0d40b51f1a4fbe106dba3d5cd52ef2cc29b`.

Graphics Kit ABI 2.1 and Interface Kit ABI 4.6 add the shared clipped line
primitive and retained Dial control. Dial provides vertical drag, Alt fine
motion, bipolar zero detent, double-click reset, keyboard adjustment, and live
value actions. It draws its indicator through the one Graphics Kit line path;
software clipping and draw-list lowering share regression coverage. Consumers
now declare the exact append-only Graphics export-table extent they use, so a
new minor export does not force unrelated 2.0 clients to rebuild.

Temporary Gallery release
`cc87a710f8b003090d122c4bb7d374ebe1160dbd3423bf20c1a809b5c8623442`
reached stage 8 at 69.238 MHz with zero service restarts. A physical drag moved
the Dial from 0 to 100 and updated its value label, focus ring, and indicator
in the same frame. The MC68040 completed 10,000 alternating Dial key actions
in 50,315,360 ns: 5.032 microseconds/action, or approximately 198,746
actions/second. Its retained Beast trace is
`/tmp/astra-dial-ring.bin`, SHA-256
`3c84e7ad3332e07eb435497131873804339a767471a656053d0f8782d605ae90`.
Cam Link captures before and after the drag are
`/private/tmp/astra-interface-dial-v46-value.png` and
`/private/tmp/astra-interface-dial-v46-dragged.png`, SHA-256
`95419357a11c05eb1568b8a5baf649b08b148a09e5f0ab0e8cdbedee21f3ddcd`
and
`5d26004a9d74484f15283e38b9e477885605ddb6aa6f2148844cb5c5d1425665`.

Normal desktop release
`35ca43caa2bec604f267d67ca1138259b28ac19b9324f0034fc776c45185bab3`
is selected and byte-verified on the physical DE25. Its QEMU, ROM, clean
storage image, display helper, launcher, Graphics library, Interface library,
and Gallery SHA-256 values are respectively
`8a7b8be1707379b8b40c49a19fe4df73d7a1091c37dfdaecaa5e792066fc9540`,
`53c4b0a9c757d2d730f2bfce414e282611abedbb95ab9c22cd6b015ad177762c`,
`1e5e6414393c5a5f201ad8e38711e8c4b48d757b36ca9fbf0edbe64c6f1107cd`,
`abbc0720a0d9bd23c02260486e8f9f3db5838b59be55c4fd700e67d85655876d`,
`1e76f48511636d1cc9c479c3f7ae90412322691b9af6c3b33a026d454b7b6092`,
`53d5f184c4cb672e6561e5835caa84806a649d4c4bab68534d8bdaa6583d689d`,
`263c44454c61ef47d400f4466fea974f26fff53918df8695029fad031f8187e3`,
and
`3fe986523ac245be2727976bfacfdd8c54cb230f77c051f9790f6b2d85659292`.
Its extracted ext4 volume passed read-only `e2fsck`; the runtime reached stage
8 at 69.295 MHz effective with zero service restarts.

Immutable release
`80a5eb9d0899e24e17d2e629e16adde13f393520c930a05c3397c0d531177cb2`
is selected and byte-verified on the physical DE25. It was produced by the
canonical publisher after clean, all-core builds of every owned software
input. Its source manifest is part of the immutable release, and publication
would have failed if any non-ignored source changed during the build. The
release directory and files are read-only; `astra.service` is active with zero
restarts, and the live QEMU command names this release's QEMU, ROM, and
release-keyed writable storage explicitly. The release's QEMU, ROM, clean
storage image, display helper, launcher, and source-manifest SHA-256 values are
respectively
`a5b8e918d35eab433773f7268e76a06f5a825597d90bcb9699f8560ee56cd9ec`,
`0c040408756d3da305b0a273940dfc8e37915f61060e61a02e509be22a464a68`,
`8c0fe7cf9b1aece128c4246a46aa4c88dc61d24fb26c25da6cb7198c512aa4d0`,
`8196e83f575a90c3f1db9dd78a705859c55885169799019c4256303a5bd232b7`,
`1e76f48511636d1cc9c479c3f7ae90412322691b9af6c3b33a026d454b7b6092`,
and
`1a2eff8bc42a5bd0d61440104078c488e9f89ef53108afc5d9dfca4e381a66da`.

## Active remote-display capture

The routed DE25 shell now contains a demand-driven RGB888 capture writer on
the final physical pixel stream. Its input is after framebuffer/window, tile,
sprite, Copper, boot-overlay, scanline-replay, and native-pointer composition;
remote display therefore sees the same complete image sent to HDMI. Capture is
disabled unless armed. One exact 6,220,800-byte frame at the top of the 512 MiB
Media RAM arena is excluded from the display allocator; render records,
surfaces, and capture storage cannot overlap.

Immutable release
`48903a19e61b15bd2b0d09556e7cf57d9f2c94cdd25b3398b448b10b78d4bc39`
is selected, byte-verified, and running at stage 8 with zero service restarts.
The display helper now commits an empty scene before reusing Media RAM, so an
abruptly killed showcase cannot leave the FPGA scanning the render workspace
while its successor writes command records there. A fresh direct Cam Link
frame is `/private/tmp/astra-display-handoff-fixed.png`, SHA-256
`5093d751b893498d4b5eb0256c759b6f5feb9103c06c106aa0f1c700b157682b`.

The existing DE25 DesignWare AXI DMA controllers provide the no-RTL readback
path. The exact running Linux source is Altera commit
`d7d192a9ddd955fe206aa350afed8f82e368a74c`; enabling its upstream
`dw_axi_dmac_platform` module exposes eight physical channels. Verified
single-channel `dmatest` copied 1,601 aligned 65,504-byte blocks with zero
failures at 156,206 KiB/s. Four channels on one controller moved the real
6,220,800-byte captured frame in 15,081,176 ns during the probe and
14,570,053 ns through the final read-only mmap device.

`fpga/de25/linux/astra_display_capture.ko` is the production on-demand
boundary. An exclusive open allocates one page-rounded coherent frame and
claims up to the four physical channels on one controller; close returns every
resource. Each ioctl captures a complete physical frame, maps Media RAM only
for the subsequent DMA ownership interval, stripes the transfer, and returns
the hardware generation, capture cycles, channel count, and measured times.
Writable mappings are rejected. The final physical gate reported generation
7, 2,639,808 capture cycles, 28,842,070 ns acquisition wall time, and
14,570,053 ns DMA time. Its RGB payload is
`/private/tmp/astra-production-capture-fixed.rgb`, SHA-256
`38e690b4f69e57fead7bdce21142d9f5f1420c7e95ce1d8dcd0fd0c4791de476`;
the lossless PNG is
`/private/tmp/astra-production-capture-fixed.png`, SHA-256
`6636321c89dd50b0470bbfcdbde75ab29b115d6dbd53dfe0432ead9a4b9dff87`.
The RGB payload is byte-identical to direct uncached readback.

Load `dw_axi_dmac_platform` once per boot and retain it until shutdown. The
upstream 6.12.11 remove path omits `dma_async_device_unregister()`, so unloading
it leaves stale DMA class registrations and reinsertion fails with `EEXIST`.
The Astra capture module remains unload-safe while closed and consumes no DMA
channels or coherent frame while unopened.

Immutable release
`a796b95f0839e141160b24cc2812683a11b1533a2cdb54be11b43118f73987a0`
exposes the standard RFB/VNC service directly on the DE25 Linux host at
`192.168.1.52:5900` and requires standard VNC password authentication. The
broker refuses a non-loopback listener when the required password is absent
or invalid. The Mac connects straight to that address; Beast is not part of
the runtime path. A dedicated
`/run/astra/remote-desktop-qmp.sock` keeps input injection independent of the
diagnostic `/run/astra/qmp.sock`. The physical RFB gate received the exact
1920x1080 frame and delivered pointer and keyboard events into Astra. Its
final-release 6,220,800-byte desktop RGB payload SHA-256 is
`bf530945d726f22e8c04fd0097438082ed3350638686403f9ba3e6ec80597345`.
Anonymous and wrong-password clients were rejected; the authenticated Mac
also opened Terminal and executed `echo direct-keyboard-pass`, producing the
evidence frame
`0cba29ae38139fc858d31e9def31567800c0c4e39b0011c5decfee0f00efd75c`.
Both runtime services remained active with zero restarts.

LibVNCServer's default software cursor path attempted to paint into the
capture device's intentionally read-only mapping and crashed on the first
frame. The service now disables that redundant software cursor with
`rfbSetCursor(screen, NULL)` because the final composed capture already
contains Astra's native pointer. The service is packaged in the immutable
release, installed atomically by the deployer, hardened to read only the
capture device, and remains disabled by default. Explicit physical start and
RFB input/frame tests passed with both `astra.service` and
`astra-remote-desktop.service` at zero restarts.

The matching `astra_display_capture.ko` and `dw-axi-dmac-platform.ko` are
installed under the running kernel's `extra/` module directory with SHA-256
values
`2bfd74ff2fe096307b75f915e4a7e19876ac78b675ee33da290ea975c3be2b57`
(2026-09-30: judges each capture by its own counters, returns EAGAIN for a
frame lost to FIFO overflow; the previous module is kept on the board as
`/root/astra_display_capture.ko.pre-sticky-fix`)
and
`d254cdea3f4b5f0b57ac99cf6a1cebde41c2f4996c38bc2f0f69627355e42300`.
`/etc/modules-load.d/astra-display-capture.conf` loads the capture module while
its soft dependency orders the DesignWare provider first. A controlled cold
boot loaded both automatically, created `/dev/astra-display-capture`, reached
stage 8, and left the opt-in remote service inactive until explicitly started.

The board runs release `323ab243` (from `36a4dae3`) on bitstream `a13c8ed0` (`rtl-scale`, 2026-09-30; entry in
`fpga/de25/TIMING_CLOSURE.md`): nearest-neighbour scaled BLITs no narrower
than their source run in the burst mover's pixel mode. Its rollback bundle is
`/var/lib/astra/boot-incoming-noerr` (`68ce7bb2`). `astra-render-certify`
(`/data/cert/astra-render-certify.scale`) passes 10/10, and the remote-desktop
RFB test passes.

## Active dynamic-linking work

The executable/startup ABI is version 7. A streamed process-load transaction
can now validate and map one `ET_EXEC` program plus its supervisor-resolved
`loader.library.1` interpreter atomically. The child starts at the relocated
interpreter entry while its startup block preserves the program entry and the
exact interpreter base, span, and entry. Direct execution of a dynamic program
without that interpreter is rejected. Focused kernel and runtime tests prove
both images' bytes, entry selection, source routing, release-once behavior,
rollback, and unchanged static launch behavior.

The source-tree conversion is complete for all 21 programs installed in
`COMMANDS:`. Every shipped artifact is an `ET_EXEC` dynamic executable with
`loader.library.1` as its interpreter, exact ordered direct dependencies,
immediate binding, GNU RELRO, and no text relocations. Independent validation
passes for both unstripped and shipped images. Small commands are 9,144..26,588
bytes; Vim and zsh are 4,351,768 and 1,637,232 bytes. A stable all-command
no-op takes 3.18 seconds with zero compile, configure, link, ABI-generation, or
audit commands; the stable zsh-only no-op takes 1.55 seconds. This source state
is published as physical DE25 release `354a01b2...` described above.

Shared libraries use one page-streamed kernel load transaction and a growable
resident cache packed into the dynamic arena; the old whole-image syscall,
fifteen-entry cache, fixed 16 MiB slots, and 4 MiB VFS staging ceiling are gone.
The eager process loader uses the same positioned immutable source contract for
every dependency, so a sparse library consumes pages for its mapped extent
rather than a second contiguous copy of its file.

Executable source transfers are no longer artificially split into one syscall
per page. The kernel requests the remaining contiguous file-backed part of a
segment, each source returns the largest prefix it can lend, and the kernel
still validates, copies, and publishes that prefix page by page. A shorter
non-final prefix must be page aligned; zero-length progress and malformed
partial transfers are rejected. Positive tests cover multi-page requests and
the VFS bulk-area path, while negative tests cover zero progress and unaligned
partial replies. On physical DE25 release
`b2b8ab98fffb7b956e3ebef6f8a146cd7fd1bb198a9df9cc4a69a87bfcd5d812`, five
zsh launches measured 33,350,048, 16,812,737, 17,101,010, 19,093,552, and
17,694,696 guest cycles from Terminal's launch event to shell-ready. The warm
median is 17,397,853 cycles, down 38.5% from the preceding 28,291,172-cycle
baseline. The warm launch-to-loader-entry median fell from 12,540,509 to
6,895,199 cycles. Three physical `printf BATCH-OK` checks completed with zero
block reads and a 152.646 ms median visible completion time.

The executable publisher now copies each page directly from the validated user
source into its newly allocated destination frame instead of bouncing it
through the kernel scratch page. A negative process test supplies an invalid
user address and proves both `BAD_ADDRESS` and complete frame rollback; the
existing positive multi-page test verifies the published bytes. Generated
MC68040 code calls `kernel_copy_from_user` directly on the destination frame.
On physical DE25 release
`225bbf6ecd874c2172196e81387fdab70742c3029f509850ce065068fad09c5b`, three
cold boots supplied ten warm zsh launches with a 17,188,700-cycle aggregate
median. That is a repeatable 1.2% end-to-end improvement over the batched-copy
release, while the launch-to-loader-entry phase improved 4.0%, from 6,895,199
to 6,620,005 cycles.

Shared-library segment transfers now use that same batched contract instead of
one load-write syscall per 4 KiB page. The kernel requests the remaining
contiguous file-backed segment, accepts page-aligned prefixes from smaller
sources, and copies each supplied page directly into its cache frame. Tests
prove multi-page progress and valid publication, reject unaligned prefixes and
bad userspace addresses, and verify that rejected header and segment copies
leak no frames. Three controlled physical boots of candidate release
`0a0dadf871759671553c7df86f1f1d4201e22cb13ac15dc41d858bc41b1d6ca6`
measured a 27,020,544-cycle cold zsh median, 17.1% below the preceding
32,608,877-cycle baseline. Twelve equivalent warm launches measured a
17,023,010-cycle median, 1.0% below the 17,188,700-cycle baseline. Corrected
release `db67d323dd12f4a6cf6d9fee5e9968f90633abf5eaecd19ab75ecffe7571f559`
is selected and byte-verified on the DE25 with `astra.service` active and zero
restarts; its final one-cold/four-warm check measured 26,702,800 cold cycles and
a 16,994,464.5-cycle warm median.

Inactive applications are not consuming the recovered launch time. Two
physical `ps` snapshots 15 seconds apart with five Terminal/zsh pairs showed
each inactive zsh advancing by less than the 10 ms reporting resolution and
each inactive Terminal by no more than 10 ms. The active Terminal advanced by
70 ms while rendering both snapshots, as expected.

The PC sampler now records D0 and A7, can read a selected user-stack word only
at an exact PC, and retains the existing MMU-root and executable-signature
checks so samples cannot be attributed to the wrong process. Positive parser,
register, stack-read, and exact-PC tests pass, as do negative malformed-register
and absent-mapping tests. A physical launch profile places the remaining warm
time in two broad regions: about 6.87 million cycles before the dynamic loader
enters and about 8.93 million cycles from program handoff to the ready shell.
The post-entry loader stages are individually small; startup allocation,
syscall, scheduling, and page materialization are the remaining shared costs.

The next shared-path profile identified storage's ext4 block-cache semaphore
as the largest syscall source during zsh launch: 19,556 lock waits and 19,556
signals across five launches. Storage and hostfs now use the existing
userspace-fast-path Astra mutex for VFS and ext4 locks; the journal timer
remains a semaphore. The VFS state-wait callback now receives the mutex word it
actually expects instead of a semaphore handle. MC68040 globals and the VFS,
allocator, and POSIX futex-bearing types require four-byte alignment: an
initial physical run caught a contended storage lock at `0x0063bb5a` failing
with `INVALID_ARGUMENT`, and a subsequent guarded run exposed the overlooked
host-direct VFS client lock. The public mutex rejects misaligned addresses
immediately. Positive lock/wait and two-thread contention tests, negative
misalignment and callback-wiring tests, target layout checks, the complete
userspace suite, and a final five-launch DE25 capture pass. Syscall probes
measured `WAIT_ONE` falling from 23,696 to 1,021 and `SIGNAL` from 22,922 to
253 per five launches; guest instructions fell from 28,727,948 to 18,928,224
per launch. The final five-launch DE25 capture measured a 732.597 ms profiler
interval median and a 686.963 ms command-completion median for `zsh -fc exit`.
This is a shared ext4/VFS change, not a zsh patch.

`tools/astra-prof` replaces manual QMP sampling for whole-system hotspot work.
One command builds the pinned DE25 profiling QEMU, runs an isolated physical
capture, records exact MC68040 translated-block and optional memory-access
counts, samples Cortex-A76 cycles with Linux perf, dumps the Axiom trace ring,
restores the production services, and symbolizes the kernel, ROM, and matching
userspace debug ELFs. `de25-command` opens Terminal through the desktop, uses
the real OSC-133 shell-ready event for exact controlled interval boundaries,
performs requested warmups/repetitions, and optionally runs a caller-supplied
cleanup command. It also
compares repeated captures and emits source-interleaved MC68040 disassembly.
The retained physical gate captured 131,662,389 guest instructions, 2,000+
host cycle samples with zero losses, and 81 trace events with zero wraps or
drops. Replacing the plugin's execution callback with QEMU's inline per-vCPU
counter removed a measured 17.11% profiler hotspot; the retained plugin
translation work is 0.13% of host samples. An earlier mixed-artifact attempt
paired the current ROM with an older production storage image and degraded in
the storage service. The tool now requires a coherent ROM/kernel/storage/ELF
set, and its default path builds all four together. The retained coherent
physical gate reached stage 8 with every service at status 0 and captured
516,027,520 guest instructions, 14,173 host samples, and the complete trace.
Production `astra.service` and remote desktop were restored active with zero
restarts after every capture.
Host `perf` capture now requests a clean QMP exit and waits for its recorder to
flush before reading the data file; an empty report is rejected rather than
treated as a successful profile. The corrected physical capture returned
21,000+ cycle samples with zero losses. Its largest named QEMU host symbols
were translation-block hash lookup (4.08%) and lookup helper (3.26%); the
profiling counter itself accounted for 3.50%. These samples cover the whole
boot, not only the command intervals, so they are not a basis for claiming a
command-specific host hotspot.

The first exact targeted profile also paid for itself. Five current-image zsh
launches had a 79,722,189-instruction median and showed 38,485,713 aggregate
instructions in the storage executable's byte tail of `memcpy`: multi-megabyte
filesystem transfers had source and destination pointers with different
alignment phases. The MC68040 primitive now aligns the destination and uses
the processor's unaligned longword support for the bulk. Generated target code
contains the longword loop for this path; exhaustive source/destination-offset,
length, and guard-byte tests pass. Three independent candidate boots measured
70,844,943, 70,908,116, and 70,745,751 median instructions, a 0.23% candidate
spread and an 11.1% reduction against the matched baseline. The matched first
boot's profiler-observed interval fell 8.22%, while visible median completion
fell from 2707.353 to 2523.117 ms; host elapsed time is secondary evidence
because profiling deliberately changes execution speed.

Stage timing then proved that rebuilding the coherent 128 MiB filesystem cost
107.4 seconds. The profiler now caches it by SHA-256 over every published build
input and the image recipe. A subsequent unchanged physical capture hit cache
identity `46fd49e7ab91`, eliminating that entire step while retaining the
current-build and artifact-coherence checks. A transient display queue during
the third boot also exposed and fixed a profiler settle bug: it now waits for
submitted work to complete instead of rejecting a temporarily busy queue, with
positive drained and negative pending regression tests.

QEMU exception logging provides an independent materialization count. After a
10-second post-boot settle, three unchanged launches each produced exactly 98
access faults; the idle control produced none. The allocator formerly threaded
every future block into a free list when it created a run, touching pages before
an allocation needed them. Release
`b0161bcd40c04b87b640cd0c5ef11e7c55e2371ea8d9feec047d9ec37d3099b2`
hands out untouched blocks incrementally and keeps the free list only for
returned blocks. The same settled physical launch now produces 85 faults.
Focused tests prove both untouched sequential allocation and reuse of a freed
block; the full runtime suite, generated MC68040-code inspection, immutable
publisher, installed-release verification, and a physical `heapbench` run pass.
Equivalent repeated warm sequences measured 17,426,464 and 17,399,895-cycle
medians, a noise-level 0.15% difference, so no end-to-end warm-speed claim is
made. Four cold candidate launches measured a 27,161,305.5-cycle median, also
within the established cold-start variance. The retained gains are the exact
13-fault reduction and avoiding commitment of unused run pages.

Library publication is now a reversible map/relocate/commit transaction.
Aborting before commit unmaps every segment and reclaims idle cache pages.
Commit validates and seals the kernel-retained GNU RELRO range with an
all-or-nothing VM rights reduction before dropping rollback authority. Every
cached dependency attach uses the same transaction and cannot bypass RELRO or
rollback. The complete kernel, VM, ELF, runtime, streams, and VFS host gates
pass this contract.

`loader.library.1` owns exact dependency resolution, combined per-thread TLS,
eager versioned relocation, main-program finalization, constructor/destructor
order, and loader-channel teardown. The kernel validates and maps bytes;
filesystem and symbol policy remain in the one user loader. There is no second
mapper or fixed dependency count.

The versioned NDK archive is now dynamic by default and carries the dynamic
CRT, all 16 registry-owned versioned `.library` files, the closed
`loader.library.1` interpreter, static self-contained archives, and the
executable-contract checker. All library owners, including the interpreter,
pass their full ABI contracts before packaging. The NDK make fragments are the
sole executable ABI and dynamic-link policy owner for archive consumers,
source-tree ports, all 21 commands, and the generated Vim and zsh vendor
builds. The source-tree
POSIX kit now supplies locations only, and a regression rejects duplicated
ABI, sysroot, PIE, binding, RELRO, or garbage-collection policy in either
adapter or command build. The 10,601,784-byte `0.1.0-dev` archive passed its
relocatability, normalized-metadata, and checksum gate. Using only its
extracted contents, native C, POSIX C, and POSIX C++ smoke programs linked to
9,684, 10,288, and 23,808 bytes and passed their exact dynamic contracts. It
also builds static smoke images of 13,792, 841,632, and 2,328,660 bytes,
proving the explicit self-contained mode from the extracted NDK. Its SHA-256 is
`28a80605da706bd1bdb048e6c7c4b8fb7588cabde2317a5df18b747ea0c3905a`.
The NDK's HTML documentation and generated PDF also pass the
warnings-as-errors public-API documentation gate. Static linking remains an
explicitly named, self-contained developer choice; dynamic linking is the
default, and Astra's production tree selects static mode only for Supervisor
and Storage because they bootstrap the loader and system volume.

Runtime, shared-library-loader, VFS-library-record, and POSIX allocations now
share one process heap. The previous loader/VFS side reservations and fixed
record slabs are gone, so POSIX cannot consume the process address space while
leaving unusable free memory outside its heap. A fresh 128 MiB filesystem image
passed the complete QEMU zsh integration gate with cached library attachment;
the former `filesystem.library cached attach:11` failure did not recur.

An initial supervisor exit is now a contained userspace failure rather than a
kernel panic. Process teardown records one retained `PROCESS_EXIT` event with
the PID, normalized status, exit reason, and initial-image flag, revokes the
owner's resources through the normal path, and presents a degraded boot-control
diagnosis. A QEMU fault-injection boot with `/startup/system` removed reported
PID 1 status `2`, reason `1`, and initial flag `1`; all five IRQ leases and all
owned devices were then revoked/reset, QEMU remained alive, and no panic was
raised. Panic remains reserved for corrupted kernel state or an invariant whose
violation makes safe continuation impossible.

The boot tail now applies that same boundary before userspace starts. A missing
or rejected initial image enters the existing interrupt-driven kernel idle
state; an unavailable AstraHost block controller, storage link, or input
controller removes only that capability and records one retained
`SYSTEM_DEGRADED` event. None of those external failures calls panic. A normal
prepared-image zsh boot passed, and a current-ROM QEMU fault injection with
`/startup/system` removed reported the supervisor failure and remained alive.
The MC68040 kernel host suite and `all verify` pass; the current kernel payload
is 186,352 bytes.

The supervisor now returns its existing block-lease failure status when no
usable block capability is granted instead of silently yielding forever.
The kernel's degraded-boot path remains the owner of the failure display;
normal manifest and desktop startup do not proceed without the system volume.

The current simplification audit has removed the coupled fixed global process
and thread pools. Process records, handle tables, executable-load records,
thread records, supervisor stacks, futex records, and the
deadline heap now consume memory on demand. Visible PID and thread identity are
limited only by their 16-bit ABI namespaces; allocation failure reports real
memory exhaustion rather than a deployment-sized slot ceiling. A target
MC68040 build and the complete kernel host suite pass with 40 simultaneous
processes and with allocation-failure rollback at each new metadata boundary.
The remaining high-value userspace consolidation is the supervisor loader's
parallel per-process bookkeeping arrays; the correct fix is one growable,
tested per-process record rather than another raised constant.

Kernel memory admission now has three explicit resource tiers. Ordinary
owners cannot consume the protected service-recovery reserve, protected
firmware-selected owners cannot consume the core cleanup reserve, and only
kernel-owned metadata may enter the latter. The scheduler gives protected
system processes priority over ordinary applications. An end-to-end regression
fills every ordinary page with a runaway child, proves that the child cannot
promote itself, wakes the sleeping controller at its deadline, immediately
preempts the spinning child, terminates it, and returns every frame to the
baseline. The negative reserve tests prove both ordinary process and ordinary
kernel-frame allocations are denied before either recovery tier is consumed.
The storage/ext4 fixed arena remains intentionally separate because it provides
precharged, deterministic allocation failure during recovery.

The active CLI migration stages 42 unmodified sbase utilities for target
qualification, without publishing them as the primary `/commands` names yet. Shared VFS
operations for directory-relative open/stat/unlink/chmod, live-handle stat,
and filesystem capacity are backend-neutral; ext4 and hostfs implement the
applicable operations. An ext4 `rmdir` data-loss defect is fixed by rejecting
nonempty directories without recursive removal. Raw and partitioned ext4
images, journal recovery, `e2fsck`, VFS host tests, POSIX host tests, and the
QEMU host-filesystem protocol test pass. Live file-handle metadata now supplies
Filesystem Kit `file_info` and `SEEK_END`. Stable volume/node IDs and
directory-handle symlink traversal are not yet implemented. A fresh-image QEMU
terminal gate now passes 92 commands, including POSIX root listing, rejection
of root creation with `EROFS`, creation beneath `/home`, previous-boot event
history, Lua, and zsh. The supervisor now provisions each requested private
STORE backing directory before granting the protected namespace entry; an
events service can no longer create its own mount root. Primary `ls` and
`mkdir` now build from unmodified sbase sources. Handle-based directory
enumeration was repaired in EVENTS and PROC, and target checks cover nested
directories in both. This is not final `ls` qualification: VFS has no stable
device/inode IDs yet, so `ls -i` and recursive cycle detection need the shared
metadata work before release.

Historical checkpoint: the following root-link and RAM-service receipts belong
to the earlier `b8cbff25` generation. The native provider namespace above
supersedes that layout; retain these measurements as history.

The fixed internal SD volume is exposed as
`/dh0`; `/system` and disk-backed conveniences including `/apps`, `/commands`,
`/home`, `/libs`, `/local`, `/work`, and `/tmp` are protected,
capability-backed root links. `/tmp` currently uses persistent SD storage.
Filesystem Kit `lstat`/`readlink` and root enumeration report the links, while
native unlink/rename and CLI `rm` cannot remove them. POSIX `lstat` follows
the resolved directory for terminal `.` and `..` paths. The 92-case QEMU
terminal gate covers `ls -l` link targets, writable `/tmp`, and negative
root `rm`/`rmdir` checks. Device inventory, hotplug,
and the remaining filesystem edge-case gates in
`docs/DEVICE_AND_VOLUME_PLAN.md` remain incomplete. The internal SD card is
the fixed Astra system disk; no new Astra bootloader is planned. Immutable
release `b8cbff2519f1e649aa05709b8e61bf42365d61cc9a2847d36d7173454489352a`
is selected and running on the DE25 with `astra.service` and remote desktop
active, zero Astra restarts, and boot stage 8. A physical RFB Terminal frame
shows the protected root links and only `/dh0`, `/events`, `/metrics`, and
`/proc` as root directories. The physical Terminal writes, reads, and removes
a `/tmp` file; attempts to remove `/tmp` and `/home` fail with read-only
namespace errors. `/config`, `/work`, and `/cwd` display their actual
process-scoped grant targets, which need not match a global directory of the
same name. The logical `ramfs` service now publishes `/ram` through the normal
VFS service/assign path. Its private `max_bytes` setting defaults to 64 MiB,
charges file data and metadata on demand, and leaves `/tmp` SD-backed. The
VFS ext4, host, and RAM backends live under `src/backends/`. Host VFS tests,
ASan/UBSan, the ext4 native `open_at` mount gate, and a fresh-image QEMU
Terminal gate pass 96 commands including RAM create/read/remove and protected
root removal. This RAM change has not yet been deployed to the DE25.

## Build and artifact rules

Build, QEMU, FPGA, and physical work run through Beast. Source transfer is an
exact checksum mirror that excludes every generated `build/` directory and
deletes removed remote sources. Finished artifacts move separately. Remote
process completion and artifact identity are both checked; a wrapper exit alone
is not evidence.

Generated products never enter source control. Immutable releases are selected
by verified content identity, and physical acceptance requires the exact ROM,
QEMU, service configuration, storage base, and FPGA shell intended for release.
Activation first validates the installed release and its `current` selector,
then rotates the previous valid `current` release into `previous` atomically.
Only after the newly selected `current` release verifies by content identity
does deployment prune release and writable-state generations unreachable from
`current`, `previous`, or explicit `by-boot` selectors. This is the permanent
retention policy; an unbounded release archive on the board is not permitted.

Linker export/version definitions are source and use the tracked `.exports`
suffix. Generated linker reports may use `.map`; the repository does not
globally ignore that suffix, because doing so previously allowed an essential
ABI input to exist only as an untracked stale file. Shared-library builds and
their documentation gates depend directly on the tracked export definition.
