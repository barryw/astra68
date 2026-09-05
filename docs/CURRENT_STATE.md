# Astra 68 current engineering state

This is the short continuation map for the active machine. It records decisions
and validated boundaries that are easy to lose across long sessions. Detailed
contracts remain authoritative in the linked documents; historical handovers
and old resource tables are not current status.

The platform is **Astra 68**, its kernel is **Axiom**, and the complete
user-facing system is **Astra OS**. The Astra NDK is the stable developer
surface; Axiom's internal interfaces are not a module ABI.

## DE25-Nano migration checkpoint (2026-09-05)

The DE25-Nano attached to Beast is the active Astra machine; the Arty Z7-20 is
the rollback platform. The active DE25 build pins and checks the exact USB/JTAG
identity, Agilex 5 device, Quartus Pro 26.1.1 tool and license, vendor GHRD
source, restored QSF, and HPS SPL input before every build.

`fpga/de25/build_vendor_baseline.sh` cleanly restores and upgrades the vendor
25.3.1 design, compiles it in staging, requires successful fit and fully
constrained setup/hold timing, hashes and verifies both programming files, and
publishes only after every gate passes. The retained build uses 9,160 / 46,800
ALMs and 131 / 358 RAM blocks, with +1.662 ns setup slack, 0.000 ns hold slack,
and zero TNS. Its exact HPS SOF programmed successfully and cold-booted the
physical board through 1 GiB LPDDR4A calibration, SD, all four ARM64 cores,
ext4 recovery, networking, SSH, and the serial login prompt. Detailed source,
artifact, and JTAG identities are in `fpga/de25/TIMING_CLOSURE.md`.

The existing Astra QEMU/runtime stack now also builds for the DE25's AArch64
HPS against an Ubuntu 22.04 sysroot instead of Beast's newer host ABI. The
resulting QEMU 9.2.4 binary has SHA-256
`52ed189975d15019d696f99635081016499d989f549f4d9b55804ea172c5744e`,
requires no glibc symbol newer than `GLIBC_2.34`, and executes on the board's
glibc 2.35 userspace. Its build directory is keyed by the build contract as
well as the patched QEMU source identity, so a changed profile cannot silently
reuse an incompatible configured tree.

The retained production shell enables Platform Designer's native
out-of-order support on every HPS/graphics master connected to LPDDR4B. It uses
41,860 / 46,800 ALMs, 3,818,968 / 7,331,840 block-memory bits, 290 / 358 RAM
blocks, 58 / 376 DSP blocks, and 5 / 11 PLLs. Every production clock is fully
constrained and routed; setup, hold, recovery, removal, and minimum-pulse slack
are respectively +0.671 ns, 0.000 ns, +2.661 ns, +0.024 ns, and +0.220 ns.
The final Design Assistant report has zero high-severity violations. Exact
artifact hashes are RBF
`2486427470514318a8f669f61dbeb766b2f18501eb6569b0fa13562cda0ce8c4`,
HPS JIC
`c61af281078482027fdbd0c0a94130bee83e11daba8acf1abe5814ae344c18df`,
full SOF
`633a7bfa76139900cba5e9a1bc4c490fcef3170fe636558b0a5c8be18353d562`,
and HPS SOF
`c102e120e4278269d32f4666cf2f32bf02e25a9ba0b86b75b8a100a7c9a26417`.

The exact RBF and boot script were installed atomically into the SD boot
chain. `verify_running_shell.sh` now verifies the complete retained build
manifest and uses System Console `design_link` against the physical JTAG
device, so an old programmed image cannot masquerade as the candidate. That
gate passed again after a physical power cycle. The board then cold-booted
LPDDR4A, the Astra fabric, SD Linux, all four ARM64 cores, ext3 recovery,
1-Gbps Ethernet, SSH, and the Astra runtime normally.

The former concurrent HPS access failure is closed. Fifty-four concurrent and
alternating AArch64 workloads transferred 99,532,800 bytes through the shared
LPDDR4B arena with zero hangs. Intel PMON counted exactly 12,441,600 response
beats, equal to 54 times the expected 230,400 beats, with no overflow. This is
the measured effect of the native out-of-order setting; no custom ordering
shim was retained.

The first normal runtime exposed a separate Linux-helper fault: glibc's AArch64
`memset` selected `DC ZVA` for the device-mapped framebuffer and raised
`SIGBUS`. A captured core located the exact instruction. All device-memory
fills and copies now route through the shared graphics hardware library, whose
generated code contains only explicit volatile byte and 64-bit loads/stores.
The fixed terminal display SHA-256 is
`fe62b9c45b45aa493e6eb4f3fd0554d95f54f0c2b168ac1140238f7c7a78fb87`.
It remains resident through POST, storage recovery, hostfs, network, NTP, and
stage 8. The first fixed-display release was intentionally superseded after a
cold boot proved that systemd's `time-sync.target` only waited for the NTP
daemon to start: Astra inherited the board's RTC about 28 minutes before the
first network synchronization. The DE25 unit now pulls in systemd's native
`systemd-time-wait-sync.service`, whose start timeout is infinite, and orders
Astra after both it and `time-sync.target`. This avoids the sticky
`NTPSynchronized` property that a live negative test proved unsuitable as a
custom polling interface. Current immutable runtime release
`d7b5b035315219fcdc47bd34fa1c2726685904c9663e68a06c09533f01204344`
remains byte-verified. The final cold boot started the native wait service with
the board's stale 2023 RTC, held Astra until NTP synchronized at
`2026-09-05T16:11:25Z`, completed the wait barrier, and started Astra in that
same second. The guest inherited `2026-09-05T16:11:29Z` and reached stage 8.

On the physical DE25, that exact release passed the complete 70-command
terminal gate covering filesystem mutation and durability, POSIX networking,
Lua, process reporting, events, redirects, assigns, and the 2026 host clock.
Three consecutive hardware sweeps passed full splash transfer/readback, every
64-sprite phase, the complete renderer/blitter/geometry/AFNT/flood/compositor
suite, dual-bank copper, and 48-kHz HDMI audio with zero AXI errors and zero
renderer backpressure. A second power cycle again loaded the exact FPGA shell
but exposed the NTP-ordering defect above; the following cold boot passed the
native synchronization barrier and exact-shell gate. The live display mailbox
completed the desktop's render request at graphics generation 4. The live
graphics registers reported the expected device identity and capabilities,
the 1280x720 scanout at `0x40200000`, zero commit errors, zero response stalls,
and zero renderer failures or backpressure. Requesting the HDMI link made the
hardware report both requested and transmitter-ready; removing the request
returned it to idle. The exact-shell, cold-boot, runtime, scanout, and HDMI-link
gates are therefore closed without requiring an external capture device.

Direct hardware testing established the boot-order requirement. Loading the
HPS-bearing Astra SOF stops a running HPS, while loading its RBF preserves the
HPS but disables the HPS-to-FPGA bridges; a status read at `0xff420000` then
raises `SIGBUS`. The SD U-Boot script enables those bridges before Linux.
Production must therefore load the Astra RBF in the boot chain before `bridge
enable`, not reconfigure the fabric after Linux starts.

The DE25 Linux image no longer carries its previous Ultimate128 runtime. The
input daemon and every installed binary variant, state directory, Avahi
advertisement, hostname, DTB reservation, memory-limit argument, and matching
boot backup were removed. A physical recovery boot reached an `astra68` login
with only the vendor service-buffer reservation, no retired service, the full
expected 934 MiB Linux memory pool, networking, and SSH. A Linux warm reboot
previously stalled after the HPS reset request and required a fabric/HPS cold
start. The current instrumented SD image completed a warm reboot successfully;
cold-boot repetition remains required before that older limitation can be
retired from the release gate.

DE25 parity is complete. The next CPU architecture project is a measured
MC68040 migration. It remains separate from the board move so PMMU, exception,
cache, and performance changes can be attributed and qualified independently.

## Physical host-storage media checkpoint (2026-09-01)

Production `/data` remains ext4 with `fast_commit` on SD partition
`/dev/mmcblk0p3`; the restored production selection matches all 348 source
file hashes and its active-release symlink. Equal-size SD partition
`/dev/mmcblk0p4` retains the F2FS test filesystem. A complete 44,512,127,488
byte pre-test archive is held on Beast with SHA-256
`f0897ba3e7a9cd35e9ca4512254b44200d633ac0984c5473a94524986d4ab833`.

Three 4,096-operation physical Arty runs used the same verified native
durability binary and fresh directories. Median rewrite-fdatasync,
32-thread group-commit-fdatasync, and 32-thread group-commit-syncfs rates
were respectively 219, 4,096, and 1,290 operations/s for SD ext4; 439,
3,963, and 2,478 operations/s for SD F2FS with strict fsync; and 561, 7,634,
and 1,620 operations/s for ext4 on the attached 2 TB WD My Passport over
USB 2.0. Every run verified all generated records.

Each filesystem also survived a hardware reset during active durable writes.
SD ext4 and F2FS each retained 242 completed records plus all 81 prior files;
USB ext4 retained 808 completed records plus all 243 prior files. Sizes,
contents, and pre-reset SHA-256 manifests matched after journal/checkpoint
recovery, and offline `e2fsck -fn`/`fsck.f2fs -f` checks passed. The USB disk
is a validated faster optional tier, but production was not moved to removable
storage implicitly.

## Host filesystem boot dependency (2026-09-01)

The host filesystem manager is the QEMU AstraHost backend, not a second Linux
daemon. The Arty launcher creates `/data/astra/hostfs` and exports it as
`ASTRA_HOSTFS_ROOT`; QEMU opens and pins that directory during machine
construction before it creates or resets the MC68030. A missing root aborts
QEMU before guest execution. Inside Astra, the startup manifest launches the
block-backed storage service first and `hostfs` second as a required service;
the supervisor waits without a deadline for its ready publication before it
launches network, NTP, or the desktop.

Regression gates now require the Arty launcher to create and pass the hostfs
root before invoking QEMU, require QEMU to reject an unavailable root, and pin
the shipped manifest's `storage`/`hostfs` order. The exact QEMU hostfs protocol
test and the complete Arty Linux host suite pass on Beast.

The same physical boot exposed two older host-init violations. Yocto's native
volatile-directory population now runs during provisioning, sets
`ROOTFS_READ_ONLY=yes`, and verifies the exact `/tmp`, `/var/{log,lock,run,tmp}`
and `/etc/resolv.conf` symlinks before the root returns read-only. Firstboot now
waits indefinitely for `/data`; after a failed early DHCP attempt it raises
the link, waits for real carrier, and only then runs the configured `ifup`
path. It no longer repeatedly resets the PHY while waiting for DHCP.

The default physical Arty boot acquired `192.168.1.188`, synchronized NTP to
`2026-09-01T03:50:11Z`, and logged required launch order `storage`, `hostfs`
ready in 49,785 microseconds, `network`, `ntpd`, then stage 8. Runtime release
identity is
`c32332e03fa51312ab60d191b8553217ce7ef9b44e9672e29dad57a864074599`;
its ARM QEMU SHA-256 is
`c11f06b1e8d49f831a2fb0a54a41e4b182b27dbeb912cff301bd3dcd0fd55c97`.
Linux `/` is read-only, SD ext4 `/dev/mmcblk0p3` is read-write at `/data`, the
FAT boot volume is mounted, and the USB disk remains unmounted.

## Active execution boundary

The sole MC68030 implementation is the Astra QEMU TCG backend running on the
DE25-Nano AArch64 HPS. The FPGA fabric implements graphics and peripherals; it
does not contain the CPU. The Arty Z7-20 is the rollback platform. CPU RTL,
alternate emulators, obsolete board SoCs, and their private verification
harnesses were removed on 2026-08-27 so the repository cannot present a
retired execution path as current. Shared FIFO, front-panel, and font assets
live under their active owners.

The DE25 has separate 1 GiB LPDDR4 banks: LPDDR4A belongs to the HPS and
LPDDR4B is the graphics arena at `0x40000000..0x7fffffff`. Linux exposes
934 MiB of the HPS bank as normal System RAM, and QEMU preallocates Astra's
128 MiB guest RAM from that cached memory. Graphics control registers occupy
the separate lightweight-bridge aperture beginning at `0x20100000`.

## Immutable Arty releases (2026-08-30)

Loose files under `/data/astra/{bin,qemu,rom}` are no longer deployment or
launch authority. `tools/astra_release.py` creates releases only from explicit
inputs and verifies exact files, directories, SHA-256 content, executable
intent, and installed read-only modes. Installation moves a verified tree into
`releases/<manifest-identity>` and atomically replaces a symlink with Python
`os.replace`; the regression specifically replaces an existing symlink to a
directory, which BusyBox `mv` previously followed instead of replacing.

`astra_image.py --create` formats a new partitioned volume with the storage
subsystem's one ext4 profile, clean-builds all userspace, populates that new
volume, resets its journal, and atomically publishes it. Existing images are
never release inputs. The image publisher also recursively clears every
publisher-owned root, consumes the exact command manifest, rejects stale
services, and QEMU gates boot private image copies. Runtime launch verifies the
selected release before QEMU starts and copies its immutable base disk into
`/data/astra/state/<release-identity>`; QEMU never writes the release.

Graphics selection is derived from the SHA-256 of the `BOOT.BIN` actually
running: `/data/astra/graphics/by-boot/<boot-hash>` resolves to one verified
graphics release. Firstboot refuses a missing, writable, byte-stale,
path-stale, or mode-stale release before NTP, splash, or Astra startup. This
also removed the prior boot-hash-only staging identity that allowed helpers or
the splash to come from another generation.

The physical cold-boot gate passed after two caught-and-fixed gate defects:
executable mode was absent from the first manifest format, and BusyBox `mv`
followed a directory symlink. Active runtime release
`8ccc2b296f5ae94635a2425579341835d08c7bf15e75f6675a6adfb3315f747b`
and boot-selected graphics release
`886e959ec75a4666887930a24828a96a476766142806d84c1c316ccfb443bdbd`
both pass installed verification. `/proc/*/exe` resolves QEMU and the terminal
display inside the runtime release, the freshly truncated console reached
stage 8, the base image remains
`4e55fc00faf8ebd470bbc9a1e613768135af12897e3a1e7bc8c6c443f3f2898a`,
writable state is a separate 64 MiB file, Linux `/` is read-only, and the
NTP-synchronized board reported `2026-08-30T18:19:24Z`.

## Per-thread hosted VFS lanes (2026-08-29)

VFS protocol v20 keeps one process namespace/session and gives every calling
thread its own reply channel, transaction identity, shared transfer area,
direct-transfer scratch, and host DMA channel. Client teardown changes the
lifecycle to draining before it waits for active lanes, so it does not hold a
mutex across I/O and cannot unmap storage still borrowed by another thread.
The host regression queues two requests from one shared client before either
reply is released; concurrent lazy first use also proves one session with two
lanes rather than two raced sessions.

The retained 2026-08-30 Arty adapter checkpoint used the clean C submit path
from image SHA-256
`1ae1c739f2a2bdd507a4bd93d6052128a8e6b086017367a75a72eb4c6d1d8e1c`.
It measured 11,771 transport STAT calls/s, 20,050 backend STAT calls/s,
13,208 direct-VFS STAT calls/s, 3,066 backend open/close pairs/s, and 2,683
direct-VFS open/close pairs/s. The adapter's concurrency regression holds two
thread-owned lanes incomplete until both have published, then completes both;
the full VFS regression also passes one million forced-switch operations.
An MC68030 assembly submit replacement measured 561 mixed operations/s against
570 for the C control, so it was rejected and removed rather than retained as
dead alternative code. These measurements place the complete guest adapter
above the 1,000 operations/s target at every isolated public boundary.

## Host wall-clock reset invariant (2026-08-28)

The Astra QEMU machine now seeds Vesta's RTC from `QEMU_CLOCK_HOST` on every
machine reset. `QEMU_CLOCK_REALTIME` is host monotonic uptime and is not a Unix
epoch; using it produced a plausible but wrong 1970 date on the Arty. Reads
return the current host epoch plus any offset subsequently applied by Astra's
privileged `ntpd`, so Linux NTP corrections remain visible without removing
the existing Astra clock-set ABI.

The common Terminal startup gate checks both `RTC_VALID` and the latched
64-bit RTC value before boot interaction, rejecting a value more than ten
minutes from the test host. QEMU source identity
`81c56093f2bacaa9559e447a6951a0a872b5672f2e6902400d5498b9a4842e1b`
passes that invariant, the full clock/inode-timestamp gate with zero-second
rounded drift on Beast, and a focused filesystem run. Its exact Cortex-A9
build (`3a6ae18147f58c808ce14c36ab37638fe31d667df5b99838babe44de472c9b53`)
reported `2026-08-28T04:42:20Z` on the Arty; the guest stress operation passed
and the retained ext4 volume was clean after journal replay and `e2fsck -fn`.

That hardware run also exposed an orphaned older QEMU consuming a second
128 MiB guest allocation. A physical display mailbox now has exclusive QEMU
ownership: a second process fails at machine creation with the owning resource
named instead of racing requests and completions. The focused ownership gate
fails against the preceding binary and passes on both Beast and the Arty with
the source identity above.

## Retired-stack and hosted-memory cleanup (2026-08-27)

The TG68K, Musashi, ULX3S/ECP5, Lyra/ESP32, Harte, and retired FPGA-CPU stacks,
their private tests, tools, and historical handovers are gone. The active tree
has no references to those implementations. Shared Arty FIFO, front-panel,
and font assets were retained under active owners.
The vendored HDMI serializer now retains only the Arty's Xilinx synthesis path;
the complete graphics regression passes after that pruning, through screen
offset, blitter, geometry, flood, glyph, and command-processor completion.

BootInfo ABI 0.6 reports the real 128 MiB guest aperture and the host-backed
512 KiB ROM at `0xffe00000`. Firmware no longer reserves a fictitious 256 KiB
RAM backing store for ROM, so those guest frames return to the allocator. The
kernel host suites, exact MC68030 kernel/ROM build, QEMU input certification,
Arty audio RTL, and complete Arty graphics regression pass.

The VFS port transport now connects before its first bulk-area bind. Previously
a lazy client's first bulk request let the nested HELLO overwrite BIND_AREA,
silently binding a zero-byte transfer area; one shared regression starts with a
bulk path read and proves the area has its requested size. The complete
two-boot Terminal gate passes all 70 commands, including dynamic Terminal
launch, `ps` with the delegated `PROC:` tree, POSIX, durable I/O, redirection,
and stock Lua.

Event snapshots now issue the filesystem durability barrier before a bank is
declared committed. Closing a file is not an fsync contract; QEMU's hard power
cut exposed torn snapshot metadata that correctly failed recovery. The same
two-boot gate now proves a prior boot survives that cut. The full userspace
host suite and the raw, partitioned, full-volume, journal-failure, power-cut,
stale-journal, and pressure ext4 matrix pass with clean `e2fsck -fn` checks.

## Software ownership and reuse audit (2026-08-25)

The complete target-software ownership pass is recorded in
`docs/SOFTWARE_BOUNDARY_AUDIT.md`. Kernel-private headers no longer cross into
user mode; NDK-private headers stay in the NDK; GUI wire types are
protocol-owned; Terminal is independent of Supervisor implementation; and
production programs consume owner-built archives rather than recompiling
another library's source. A 26-rule executable architecture gate retains those
boundaries and the shared primitives extracted during the pass.

Program build ordering is now shared by commands, Supervisor, and all six
services. Each declares its library owners, those owners build first, and a
second make invocation restats and links the program. The clean userspace gate
had exposed the old accidental ordering when Terminal could not build its own
library from an empty tree; clean standalone command and service builds now
pass, including Lua and the 2,157,012-byte stripped Vim file.

The complete userspace behavior, sanitizer, analyzer, and MC68030 matrices;
NDK normal/PIC matrices; Axiom host and target verification; and firmware
build pass on Beast. The final kernel payload is 155,320 bytes. lwext4 patch
0013 also removes an extent-only unused local from the no-extents build. The
remaining ownership-audit work is structured catalogs for resident services
and real process integration gates for glue-only entry points.

## Interactive Vim and Lua gate (2026-08-26)

The terminal gate now drives stock Vim through its full-screen interface,
creates `WORK:vim-created.lua`, writes `print(6*7)`, exits Vim cleanly, and runs
the file with stock Lua, requiring exact output `42`. The focused gate and the
complete 68-command two-boot terminal gate pass on Beast.

The first full-screen launch exposed a stale ncurses object built for the old
four-byte `struct winsize`; `TIOCGWINSZ` correctly wrote the current eight-byte
ABI and overwrote ncurses' return address. The Vim build now rebuilds ncurses
when the installed terminal ABI headers change, the shared POSIX layer asserts
the ABI size, and the POSIX target test compares all four fields. A second
failure exposed Terminal dropping key-only Escape events; the shared dispatcher
now forwards Escape while Enter and Tab continue through their text events, so
they are not duplicated.

## Brokered IPv4/IPv6 networking (2026-08-26)

The complete contract and evidence are in `docs/NETWORKING.md`. Axiom now owns
a capability-restricted network lease, bounded DMA admission, and IRQ delivery;
the network service alone owns that lease and every host endpoint. Applications
use stable `network.library` ABI 1.1 through Network.kit, shared transfer slots,
ports, and readiness events. Host descriptors, host address layouts, errno, and
backend pointers never cross the ABI. `NETWORK_LISTEN` remains distinct from
ordinary `NETWORK` access.

Native IPv4/IPv6 TCP, UDP, asynchronous DNS, readiness, cancellation, and
fork/exec endpoint inheritance pass. The shared POSIX library provides sockets,
address conversion, resolver calls, socket options, scatter/gather messages,
and descriptor inheritance without private command implementations. Four final
source-identified QEMU network runs completed in 2.98--2.99 seconds; the full
73-command two-boot gate, Vim, Lua, sanitizer, analyzer, clean MC68030 build,
and generated-code inspection pass.

Physical Arty qualification now passes. After carrier was restored, the patched
firstboot path reacquired DHCP, synchronized NTP, and released Astra without
intervention. The exact ARM QEMU, ROM, and prepared image passed the complete
POSIX command including guest DNS, IPv4 UDP, TCP, descriptor inheritance, and
TCP fork+exec twice in 33.61 and 32.48 seconds. The QEMU and ROM were promoted
atomically with the prepared image; active boot reached stage 8 with the correct
host wall clock. The previous three artifacts and their hashes remain under
`/data/astra/deploy/network-1a8f8895868b/rollback`.

## Transactional streaming executable loading (2026-08-25)

The syscall ABI is `0x00010022`. File-backed launch no longer allocates or maps
an entire executable. A move-only kernel load handle accepts the ELF header,
requests exact program-header and segment-page ranges, prepares a non-runnable
child, and publishes it only on commit. Closing the handle at any earlier stage
rolls back the child, handles, mappings, and frames. The shared incremental ELF
parser is also the acceptance oracle for legacy in-memory launch and atomic
POSIX `execve`, so executable policy is not duplicated.

`libastrart` owns the transaction driver and source-release contract. The VFS
library owns the borrowed random-access file source. Terminal and Supervisor
use both owners; neither has a private loader or whole-image buffer. The
bootstrap source releases its file, unmounts ext4, and returns bootstrap DMA
before commit. The only transfer buffer is one 4 KiB VM page, matching the
kernel page contract; executable file size is bounded only by the ELF32
32-bit file-offset format and available charged resources. Terminal lost its
64 KiB fallback BSS and Supervisor lost its 4 MiB image BSS.

Rollback tests cover abort before and after child creation, an off-by-one
range, sparse program headers, a segment at a 5 MiB file offset, source read
and release failures, exact VFS close ownership, and successful commit. Kernel,
runtime, VFS, Supervisor, Terminal, sanitizer, analyzer, MC68030, ROM, and
source-identified QEMU gates pass on Beast. The complete two-boot terminal gate
passes 73 commands including Lua, POSIX, and the named-file Vim edit; Vim
completed in 4.23 seconds.

All generated kernel and ROM ELF/BIN/MAP files, the packaged legacy ROM, and
the generated splash payload now live under owner `build/` directories. The
mandatory source rsync excludes them, deletes removed remote sources, compares
source content by checksum, and does not preserve source mtimes. Changed source
therefore invalidates older remote objects, renamed or deleted source cannot
survive remotely, and a Mac product cannot overwrite a Beast product. A
retained proof hashed a clean Beast ROM, performed a full source sync, and
obtained the identical hash. The layout gate also pins the placeholder-free
Arty splash source and every primary firmware output to its canonical path.

## Arty hosted ROM aperture (2026-08-25)

The ROM is ordinary QEMU host memory on the Zynq ARM. The aperture is 512 KiB
with no PL BRAM or timing cost.

The exact Beast and installed Arty image is 263,532 bytes, leaving 260,756
bytes free, with SHA-256
`3a3a7305459122d39cc94217f62bafc2cde16d3afb3c58d08d1b1c685b733162`.
QEMU source identity
`19b577a4ef305eb9ca639b006c09ee03f9c7a0666b5cb77427b716d80ea1eeab`
passes the complete 73-command gate, including the named-file Vim edit.

## Filesystem read concurrency (2026-08-25)

The storage service now runs the device-derived worker count on its existing
receive port. VFS transport scratch is worker-owned; session/file/reply state
uses short reserve/pin/commit locks; ext4 backend file and directory-scan tables
own their locks. Writes, namespace mutation, journal state, and mount lifecycle
remain exclusive.

lwext4 Astra patch 0009 gives `ext4_fread` a shared mount side, protects cache
bookkeeping briefly, and serializes cache fills/direct reads through the one
lane advertised by the current depth-one block device. A miss pins its buffer
and releases the cache lock before waiting for the device. A same-block peer
sleeps, rechecks `BC_UPTODATE`, and reuses the completed fill.

The original serialized oracle held A in a physical read and kept cached B
blocked past 100 ms. The retained gate holds the same A, completes warm B before
A is released with zero device traffic, and keeps a second cold A asleep; after
release both A readers return identical bytes and the peer adds no physical
read. Raw and partitioned whole-library tests, ASan/UBSan, TSan with Beast ASLR
disabled, MC68030 lwext4, and the storage service build pass. Making the
independent `e2fsck` promise executable exposed an older indexed-directory
checksum bug: lwext4 checksummed the first leaf and then changed its inode
field. Patch 0010 fixes the shared initializer. Raw, partitioned, full-volume,
sanitizer, and fresh-remount images are now `e2fsck -fn` clean, including two
concurrent disjoint-writer files verified byte-for-byte after remount.

The filesystem block interface previously had no way to reach Astra's existing
device `FLUSH`, so journal commit and `fsync` could report success while data
was still volatile. Patch 0011 adds the shared durability callback, barriers
journal records before and after the commit record, and ends cache flush with a
device barrier. A volatile-media oracle persists only bytes exposed by those
barriers; three successive unclean exits recover the committed file in a fresh
process, pass byte verification, and remain `e2fsck -fn` clean.

Patch 0012 closes a shared read-cache bypass in `ext4_fread`: contiguous
full-block runs kept their one coalesced cold device request but were never
published into the coherent cache. Every external command launch therefore
reread the same executable blocks. The common block helper now serves a fully
warm run without device I/O and publishes a cold coalesced run while preserving
newer cached or dirty bytes. A 12 KiB cold/read-seek/reread oracle requires the
second read to issue zero physical I/O; raw, partitioned, ASan/UBSan, TSan, and
MC68030 builds pass.

The physical Arty checkpoint now passes. `fsphys/barrier.txt` was written and
read through the real Terminal, then the exact QEMU process was killed while
the volume was mounted. The 64 MiB dirty image has SHA-256
`6580c1fa178cb4df8e05bd568297c1a11b16643191814f295e9f50feadbc1362`.
Astra replayed its journal on the QEMU MC68030 and the physical framebuffer
returned the exact 19 bytes `durable-data-68030`. Independently, e2fsck replayed
a copy of that same journal, a second `e2fsck -fn` was clean, and debugfs read
the same bytes from `/work/fsphys/barrier.txt`.

That recovery exposed an unrelated 10-second supervisor readiness deadline:
the events service crossed it by about 20 ms after storage replay. Required
service readiness, VFS replies, and event-control replies now wait for success
or actual peer closure instead of guessing that elapsed time means peer death.
The lease block backend also no longer replaces a caller's explicit no-deadline
request with a private two-second timeout; it honors the caller's absolute
deadline or waits indefinitely when the caller supplied zero.

The exact 263,532-byte ROM (SHA-256
`3a3a7305459122d39cc94217f62bafc2cde16d3afb3c58d08d1b1c685b733162`)
passes the complete 73-command QEMU gate and is installed on Arty. Its physical
boot reports the correct host time, 512 KiB ROM aperture, POST PASS, journal
recovery, and stage 8. A cold `which status` issued two requests / 24 sectors;
its immediate warm launch issued none. The earlier reported warm `cat` median
is withdrawn: its named file was absent, so it measured the error path rather
than a successful file read.

Longer physical command stress then exposed a capacity-ordering bug. VFS could
retain sessions up to its process-derived session capacity while the storage
process exhausted its smaller kernel handle table first. Dead sessions were
reaped only at the unreachable session ceiling. The shared port transport now
reacts to the kernel's actual `RESOURCE_LIMIT`: it reaps every dead idle session
and retries the same queued receive; if nothing is reclaimable, the real limit
remains visible. The focused regression, complete VFS tests, ASan/UBSan,
analyzer, MC68030 storage build, and complete 73-command QEMU gate pass. The
qualified image SHA-256 is
`94954d8f25320ce1ce6ca96c4293b4b1a53e99e8ef0ca758b1cd400b52b6d292`;
physical pressure qualification is pending because Arty went offline before
the image transfer. No cache performance claim is accepted until that gate
passes with successful file output and a live storage service afterward.

## Arty cold-boot NTP and blank splash (2026-08-25)

An Arty power cycle exposed that the Linux host returned to its March 2018
build epoch. The launcher correctly rejected that stale clock, but the old
`S04astra-firstboot` path had already presented the splash and exited after
starting the one-shot launcher, leaving the machine apparently hung. The Arty
image also lacked an NTP client and `/etc/resolv.conf`.

The ARM runtime now includes a small SNTP client that validates the selected
server, NTP version/mode/stratum, leap state, and echoed request timestamp,
accounts for half the measured round trip, and handles the 2036 NTP era. Astra
firstboot moved from pre-network `rcS` to runlevel 5 immediately after
networking. It retries NTP indefinitely with a bounded one-record diagnostic
log and starts HDMI, the blank splash, and QEMU only after the Linux epoch is
valid. The release build now always produces the RGB565 splash from the
canonical text-free source, and deployment rejects any other splash hash.

The physical reboot gate forced Linux to `2018-03-10T00:00:00Z` before reset.
After reboot it synchronized from `pool.ntp.org` at epoch `1787628422`, started
all four HDMI/Terminal/QEMU processes without intervention, and Axiom reported
`2026-08-25T03:27:06Z, from the host`. The installed blank splash is SHA-256
`86eb30739db77b85f4deb1915fb9cb9263ab4755ae318ffb1b7a4a95b7017ba4`
(CRC32 `611029ee`), and the root filesystem returned read-only.

## Resizable Terminal storage and rounded-blit capacity (2026-08-25)

The display service already supported all eight window resize edges and
corners, but the Terminal model still had a compiled 128-by-64 cell array and
the integrated display gate exercised only maximize/restore. The shared cell
model now binds caller-owned, resource-accounted storage, preserves cells when
it is rebound, and has no compiled geometry ceiling. Terminal preallocates the
largest cell grid the physical display can expose, so an ordinary drag changes
logical rows and columns without allocating on each crossed cell boundary; a
larger future display can still grow through the same charged-area path.

The first real southeast-corner gate exposed a shared compositor failure at
940x540: two rounded blits allocated full-window one-bit masks and exhausted
the render batch data arena, killing the resident display service. The common
rounded blitter now groups equal corner scanlines into unmasked spans. It uses
zero batch data for rounded clipping and retains the same geometric contract.
Render-builder failures also retain a specific reason, so a future crash names
the exhausted arena, descriptor table, command/clip, destination, surface, or
glyph table instead of reporting only a service status.

Terminal, streams, supervisor, graphics, display, ASan/UBSan, GCC analyzer,
and MC68030 builds pass on Beast. The integrated QEMU display gate now drags
the live Terminal from 840x460 to 940x540, requires the terminal glyph redraw,
and continues through maximize, restore, input, concurrent launch, close, and
relaunch. The retained run passed with 28 render batches and a 13,058-cycle
resize render against a 250,000-cycle budget. Physical Arty validation remains
pending until the terminal escape/PTY checkpoint is ready for a board image.

## Resource isolation and POSIX/VFS capacity (2026-08-25)

A badly behaved application can no longer spend the machine's recovery
headroom. On the 128 MiB Arty profile, ordinary frame owners are refused before
the last 4 MiB of general RAM; the existing 512 KiB contiguous DMA zone and
32-page kernel emergency reserve remain separate. The 4 MiB protected floor is
one equal share of the 32-process machine, capped at one complete shared area,
so it follows the discovered RAM size instead of encoding another board limit.
Only the initial supervisor can mark resident services essential, and their
process and shared-area frame owners may enter that floor. Host exhaustion
tests consume every ordinary page up to the floor and prove a protected owner
can still allocate afterward.

CPU authority is enforced at launch. Ordinary processes have a priority
ceiling of normal (16), while firmware and supervisor-selected essential
services retain the existing 1..23 band. Timer preemption, the 16-thread
per-process budget and the 32-thread global scheduler budget remain unchanged; an
application can neither raise itself above services nor mint essential
children. Kernel syscall tests exercise both refusals.

Port delivery now records the authenticated sending process and returns it to
the receiver; a wire-supplied identity is never trusted. Existing global,
per-owner and per-port message/byte budgets continue to protect the static
kernel I/O tables, and tests prove one exhausted owner does not consume a
peer's capacity. VFS sessions and files are additionally charged to that
authenticated owner. Session capacity is not a guessed small number: every one
of the 32 processes may consume all 12 launch-grant views, for 384 service
sessions total and 12 per owner. Open-file tables grow lazily in a service-owned
4 MiB area and give each process an equal share of the actual resulting
capacity. Repeated HELLOs and multiple sessions cannot bypass either account.

At that checkpoint the syscall ABI was `0x0001001d`. Port-message limits came from one canonical
header in both the kernel and NDK (16 queued messages, 1,024 inline bytes), and
the NDK's shared-area contract matches the kernel's 4 MiB range. POSIX file and
directory tables and the VFS/ext4 open tables grow from caller/service-owned
storage rather than stopping at the former 8/16-entry constants. A real target
diagnostic holds 64 simultaneous files through POSIX, VFS and ext4.

The retained shared lwext4 fix makes `O_CREAT|O_EXCL` distinguish an inode
created by the current open from one that existed beforehand; `O_APPEND`,
`fsync`, and `ftruncate` use the same common backend. The whole-library mount,
partition, full-volume and cleanup tests pass. The Terminal gate now requires
an exact output line for every `echo $?`, preventing unrelated zero-bearing
trace text from producing a false pass.

Kernel, runtime, NDK, POSIX, VFS, sanitizer, GCC analyzer, whole-lwext4, full
MC68030 userspace and ROM builds pass on Beast. The stricter integrated QEMU
gate passes all 68 commands. The physical Arty candidate at
`/data/astra/deploy/resource-isolation-8a016904c297` has ROM SHA-256
`8a016904c2974646098e8b18bcb2fc5e496a60e9e5af46c0af770ca5713faf8b`
and pristine installed storage SHA-256
`f4ffc70142717d0dcc00e876b90646c6701b6ab8e5ab43aa020a0a1533af1ce5`.

The POSIX heap is now a clone-private anonymous reservation rather than a
shared area. Reserving the complete 504 MiB architectural window commits no
frames; data and 32-bit allocator metadata commit on first touch through the
ordinary owner quota, and decommit returns whole pages. The 2026-08-25 Beast
QEMU checkpoint passes all 68 terminal commands with POSIX at 2.74 seconds and
`heapbench` at the unchanged 1.37 footprint ratio in 0.67 seconds. The POSIX
diagnostic fell from 53,436 linked bytes (12,484 BSS) to 47,648 (6,380 BSS).
ROM SHA-256 is `557d1c14ea96ede30ea714142c5cffef8fe54eae7e91a9fea069a17f02f2a0fc`;
the stripped POSIX diagnostic is
`912aadc077b7814a25a22c3c146b2f8469cbe483d53f032d551689c4a178d436`.
The shared VM layer now publishes `PROCESS_CLONE` as one rollback-safe COW
transaction. It preserves exact clone-safe handle values, copies only the
calling thread record, resolves the first child write one page at a time, and
automatically transfers frame ownership when the original owner exits. Kernel
and POSIX host gates prove waitable exit, status translation, nested-child
cleanup, byte isolation, exact teardown, and no compiled child-count limit.
It passed POST and stage 8 with host time `2026-08-25T00:02:59Z`, then the
complete POSIX diagnostic with exact status zero, `ps` in 558.196 ms, and stock
`lua -v` in 1,217.591 ms. Display submissions drained and the retained trace
had zero wraps and zero drops. This candidate supersedes the earlier active Lua
directory recorded below; that directory remains the rollback image.

## POSIX startup, terminal readiness, and Vim baseline (2026-08-25)

Ordinary POSIX programs now enter through `main(argc, argv, envp)` while native
Astra programs retain `astra_main(startup)`. The same shell launch path packs
both forms. The QEMU terminal gate launches the standard-main POSIX diagnostic
as `posix -R +42 --cmd "set number" -- WORK:notes.txt`; the program verifies all
seven strings and the terminating vector before entering raw terminal mode.
The complete 68-command gate passes on Beast.

Stream protocol 3 supplies canonical/raw input, echo, control-character state,
EOF, and a transferable readable event used by `read`, `poll`, and `select`.
The first integrated raw-mode read failed because event and timer handles
advertised `TRANSFER` but had no retain callback. Event, semaphore, and timer
handles now use the existing synchronization-object retain/release contract;
the kernel regression closes the original handle and waits successfully on its
rights-reduced duplicate. All kernel suites pass, and ROM SHA-256
`681c93f1ec44231f8de09f45d11655517432b39756776a0faf391ab82ed2fd0d`
passes the terminal gate.

Upstream Vim 9.2.1001 is pinned at
`1c32cede0afdc9351d5887e2f25977d4fb964d9f` with no source changes. Its normal,
terminal-only configuration cross-compiles every object against Astra and the
cross-built ncurses 6.6 terminfo library. VFS protocol 14 and
filesystem.library ABI 1.3 now carry atomic file and directory creation modes
plus `chmod` and `readlink`. POSIX `umask` applies at creation rather than
racing a second metadata update. Raw and partitioned ext4 image tests read back
a 0600 file and 0710 directory; the full-volume ENOSPC test passes. Relinking
the unchanged Vim tree removes `umask`, `chmod`, and `readlink` from the
unresolved set. A shared POSIX `pipe` now uses the existing charged-area ring,
wait queue, readiness, and descriptor machinery in kernel-copy byte mode. Its
endpoints are clone-safe, several readers/writers remain serialized correctly,
and the 64 KiB POSIX allocation is a default rather than a kernel ceiling. The
target diagnostic proves transfer, partial reads, duplicated-writer lifetime,
last-writer EOF, and cleanup. The complete 68-command QEMU gate passes on
Beast, including the exact Vim-shaped argument vector and pipe path. The ROM
is SHA-256 `ffa4b3bcee0d3bedf64fa34c4571a29163bf88acf11589a1dbc051dde91083e6`;
the stripped POSIX diagnostic is
`dacef7f73da452330e0aecb839d312455656782f310789da55ecfe9a3c5457f1`.

Relinking unchanged Vim now has no unresolved Astra/POSIX symbol. Atomic
`execve` uses the same ELF acceptance and startup publisher as launch, prepares
the replacement address space before commit, and preserves POSIX descriptors,
duplicate open-description sharing, `FD_CLOEXEC`, VFS sessions, cwd, umask,
environment, and exact argv without any Vim-local replacement. `sigaction`, `sigpending`,
`sigprocmask`, `setitimer`, and `getitimer` now use a kernel-scheduled signal
upcall on a registered writable stack. Timer expiry wakes a blocked wait,
delivers `SIGALRM`, and `SIGNAL_RETURN` restores the exact interrupted context.
The full kernel, runtime, POSIX, sanitizer, and analyzer gates pass on Beast;
relinking unchanged Vim proves every signal/timer reference is resolved.

The retained Beast checkpoint links pristine Vim at 2,209,042 loaded bytes
(2,014,622 text, 137,308 data, 57,112 BSS) and installs a 2.1 MiB stripped
image. The 73-command QEMU gate invokes it as
`vim -Nu NONE -n -es -c "call setline(1,'after')" -c wq -- WORK:vim-args.txt`,
requires exit status zero, and reads the edited named file back as `after`.
The complete kernel suite, runtime and POSIX host gates, MC68030 cross-build,
two-boot terminal gate, and boot payload checks pass.

## Lua, host-time enforcement, and 4 MiB application transport (2026-08-24)

Stock Lua 5.5.1 is now a normal `COMMANDS:lua` program built from the unchanged
vendored upstream sources. The stripped MC68030 file is 238,272 bytes; its
text, data, and BSS occupy 248,324 bytes at load time (234,538 text, 328 data,
13,458 BSS), proving the retired 64 KiB executable ceiling is gone. It uses the shared Astra POSIX/VFS/runtime paths for
environment, files, rename, time, and `os.execute`; no Lua-private fast path or
compatibility layer exists.

At that checkpoint the syscall ABI was `0x0001001d`, startup ABI was 4, the complete environment was
packed into the actual 4 KiB startup page, and shared areas/VFS bulk transfers
are 4 MiB. Four MiB is one complete MC68030 page table and is the largest
atomic range handled by the current shared-map primitive. Process images are
otherwise charged page by page against their address space and physical RAM;
the old 8 MiB kernel image-policy ceiling is gone. The release kernel links
with `_kernel_tables_start=0x021b4000` and `_kernel_tables_end=0x02242180`,
leaving about 1 MiB in the reserved table region.

Correct host time is mandatory. Axiom panics before userspace when `RTC_VALID`
is absent, the QEMU terminal gate independently rejects a machine model without
the RTC contract, and the Arty launcher already rejects a stale Linux clock.
The source-identified x86 QEMU clock gate measured guest epoch `1787595485`
against host `1787595484` with zero-second rounded drift. The complete
68-command Terminal/POSIX/Lua gate passes, including Lua arithmetic,
environment inheritance, nested `os.execute`, UTC year 2026, and file
create/rename/read/remove.

An allocator experiment replaced eager per-run free-list construction with a
lazy virgin-block pointer. It passed the complete gate and did not change Lua's
238,324-byte image, but a clean ten-run physical Arty control measured
1,241.530 ms for `lua -v` against 1,251.390 ms for the experiment with the same
four render batches and 49 median render commands. The 0.79% regression was
rejected, the eager shared allocator was restored, and no Lua-private path was
added.

A second shared-allocator experiment retained one empty run, bounded to eight
pages and reclaimed before allocation failure. It reduced Lua shutdown from
four allocator PMMU faults to three, but added 116 text bytes and 4 BSS bytes
to programs using `malloc`. More importantly, the physical `lua -v` median
regressed from 1,241.530 to 1,397.826 ms (`+12.6%`) and lost the control's
alternating roughly 0.80-second child-run samples. It was rejected and the
fully decommitting allocator was restored; the remaining bimodal cost is in
shared scheduling/event cadence, not a missing allocator fast path.

The retained fix removes that event cadence at its source. The events service
still writes the same complete CRC-protected snapshot to alternating banks at
the same one-second maintenance deadline, but it no longer truncates and
reallocates a bank on every save. Each bank is truncated on its first write of
a boot or an actual shrink; otherwise the monotonically growing current-boot
snapshot overwrites or extends the existing file. The service remains 37,208
bytes and no command image changed. Focused event-store tests, ASan/UBSan, the
GCC analyzer, the complete 64-command QEMU gate, and a physical two-boot gate
pass.

On a clean physical Arty A/B, ten `lua -v` runs improved from 1,241.530 to
1,203.421 ms (`3.1%`) with the same four render batches and 49 median render
commands. The child run-stage median improved from 886,799 to 874,230 us and
lost the former roughly 0.80/1.00-second oscillation. The exact final source,
including its fail-closed serialization path, measured 1,209.641 ms (`2.57%`
faster than baseline) with the same render counts. An in-place second boot
recovered the ext4 journal and the prior event bank, rendered 27 glyph runs of
previous-boot records through `events --boot -1`, ran stock Lua, and reached
stage 8.

Process priority is now a real public control rather than scheduler-internal
state. `PROCESS_PRIORITY` changes the process default and all live threads in
the existing 1-23 user band, reordering both ready and blocked wait queues;
future threads inherit it. The NDK runtime exposes the syscall, the shared
POSIX library implements `nice`, `getpriority`, and `setpriority`, and `ps`
shows `PRI` plus the derived `NI`. Kernel queue, syscall, runtime, POSIX, full
MC68030, and 64-command QEMU gates pass on Beast. The `posix` target diagnostic
also performs a real `nice(1)`/query/restore round trip, so the QEMU and
physical gates exercise the control instead of merely linking it.

The final Lua correctness pass fixed two shared root causes. The userspace
linker script now places `.got.plt` before `.got`, preserving the MC68030 GOT
base expected by generated code; `print(1/2)` therefore prints `0.5` instead
of faulting. The VFS port transport reclaims retained reply sessions whose
client process died and gives duplicated reply handles wait rights. The
shared console shell also distinguishes a dead child from a dead I/O peer, so
real faults report the process, PC, address, and vector instead of the
misleading `wait failed`. An intentional physical fault now prints
`crash: crashed: pc 0x001000f0, address 0x60000000, vector 2`, returns
`ASTRA_STATUS_FAULTED` (`2147483649`) through `$?`, and leaves Terminal usable.

The complete 68-command x86 QEMU gate passes. Runtime, VFS, supervisor,
sanitizer, GCC analyzer, and full kernel test targets pass on Beast. On the
physical Arty, four stock Lua workloads completed a warmup and three measured
runs each with the expected output and status zero. Median Enter-to-quiet
times were 1,959.245 ms for a 10,000-iteration sum, 2,173.244 ms for recursive
`fib(20)`, 2,812.829 ms for table allocation/sort, and 2,343.624 ms for string
concatenation. The retained ring contains 506 records with zero wraps and zero
drops. A final clean `print(1/2)` run took 1,771.950 ms and its one-shot trace
contains `0.5` with zero wraps and drops.

Do not poll the retained ring while measuring the hosted Arty machine. A
diagnostic-only 20 Hz `pmemsave` loop starved hosted display completions and
caused display service status 36; Lua had already printed its correct result.
Performance runs use the normal QMP display counters and take at most one
trace snapshot after the command has finished.

The active physical Arty candidate is
`/data/astra/deploy/lua-final-dcccd0ba9121`: ROM SHA-256
`dcccd0ba91213c839c71d348e5a2c48d777a255c42cb5751d2c6d5b524f5f254`,
pristine installed storage
`4530bd86becad0aadf7c214562503c38400c28f0cac6bddb679b141126499fa5`,
and ARM QEMU
`34c901e50ee0be4b9a58c20ebcae95f5ee0992e13bac0f6cfd028c89887dc526`.
The clean candidate passed POST and stage 8 with wall clock
`2026-08-24T21:57:09Z, from the host`; it remains running from a fresh copy of
the pristine image. The installed Lua SHA-256 is
`9deb02795b7de072d513ee7da0a29abe55e9471f99f0d92257ce494a23a143df`.

The priority checkpoint remains at
`/data/astra/deploy/priority-370c2b702633`: ROM SHA-256
`370c2b70263327fae0a21a00973484c15480bfe9368c0b8c2df49d567ba9646e`
and pristine storage SHA-256
`0679e10b14cfb20ab38164cbb7abd0faae20d424c783da5d4cda1e85bc44e742`.
Its physical priority round-trip exited zero and decoded `ps` output showed
aligned `PRI`/`NI` values for every process.

The earlier Lua/4 MiB physical checkpoint is retained at
`/data/astra/deploy/lua-area4m-315908e63b2b`: ROM SHA-256
`315908e63b2b39c676a55e4619ce0a98d25f58cbadee1b54e9ab5d08f12656a3`,
pristine storage
`f25275e29d3b526051c22e6509948bfb6f2bb5dbf1cc56ec474e099995902337`,
and ARM QEMU
`34c901e50ee0be4b9a58c20ebcae95f5ee0992e13bac0f6cfd028c89887dc526`
from overlay identity
`3359ee1a25c8181dc5e4df31666c443a96f34407925d5d3b018674798327d1bc`.
Its boot reported `2026-08-24T18:24:47Z, from the host` and reached stage 8.
Decoded physical trace proves Lua 5.5.1, epoch `1787596639`, year `2026`,
environment `bar`, file result `ok`, arithmetic `42`, and complete aligned
`ps` output. Measured Enter-to-quiet times were 506 ms for `lua -v`, 1.65 s
for arithmetic including rendering, 1.82 s for the year, 1.78 s for file I/O,
and 592 ms for `ps`. That image was active for the checkpoint; an
attempted restore of the older reused ps-v7 image reproduced its documented
service-startup failure, so that dirty image was not left running.

## Complete process view and `ps` latency (2026-08-24)

`ps` now lists every live process visible through the granted `PROC:` view,
including `APPS:Terminal.app` and the running `COMMANDS:ps`. Its aligned
output reports PID, generation, thread state, priority, nice level, live-thread count,
resident KiB, lifetime CPU percentage, accumulated running time, dispatches,
syscalls, handle references, and command name. The kernel query now returns
the existing thread aggregates instead of incorrectly using boot progress as
the run count, and it accounts process user-dispatch cycles at the common
kernel-entry and user-resume boundaries. The wall clock remains host-seeded;
the retained physical boot reported `2026-08-24T14:04:59Z`.

The supervisor exposes one fixed-record binary `PROC:snapshot` generated from
live process handles. This replaces `ps`'s per-process status-file opens and
text parsing without adding another process registry. The snapshot ABI is a
96-byte `AstraProcSnapshot`: the 64-byte `AstraProcessInfo` plus a bounded
32-byte command name. `ps` reads the complete bounded process table in one
fused `READ_PATH` request and appends its own row because the supervisor cannot
track a child until after that child has finished launching. This path uses the
existing VFS client directly instead of attaching `filesystem.library` and
issuing separate open, read, and close requests.

The hardware profiler now records the quiet-completion timestamp before its
seven QMP counter reads and accepts a counter snapshot only after two complete
reads agree. The old profiler charged roughly 45--50 ms of observer work to
the command and could straddle a render update. With that bias removed and
both images cold-booted from pristine storage, the first binary-snapshot image
measured 504.475 ms, down from the 799.067 ms text implementation (`36.9%`).
The retained fused-snapshot image then measured 501.589 ms in a clean 20-run
A/B against 548.380 ms for the pristine prior image, an additional `8.5%`.
Both sides rendered exactly three presentation batches, 54 commands, and ten
glyph runs in that control, so neither comparison bought speed by omitting
output. The trace shows the `ps` child falling from 23 to 18 syscalls and a
228 ms median run stage instead of the prior typical 245--250 ms.

The retained `ps` is 12,952 bytes with 9,402 bytes of text and 6,760 bytes of
BSS: 20 file bytes, 1,952 text bytes, and 334 BSS bytes smaller than the prior
binary-snapshot image. The ROM is 255,648 of 262,144 bytes, leaving 6,496
bytes. Shared-path controls did not materially move: `ls COMMANDS:` measured
439.197 ms with exactly three batches, 66 render commands, and 16 glyph runs;
`echo x` measured 205.394 ms with three batches, 38 commands, and two glyph
runs.
All command images are now packaged with `--strip-all`; `ls` is 13,164 bytes,
not 49 KiB, and the ordinary command images range from 4,408 to 16,796 bytes.
The larger `heapbench` (20,896) and `posix` (37,356) images are diagnostic
workloads containing their tests, not shell primitives.

The retained pristine artifacts on Beast are
`ps-stats-v7/astra_boot-ps-stats-v7.bin` (SHA-256
`eec4cf0b1864ba143db494f00f70ca867252caac7c7da094382dbd23ba3012b4`)
and `ps-stats-v7/storage-ps-stats-v7.img` (SHA-256
`2882e4cb78b58e574931ae1b7271f0c472dfad14b468b20a14a706fbe3a9fa8b`).
The exact pair passes POST, stage 8, process/unit tests, and a decoded terminal
trace containing the header, supervisor, all services, Terminal, and the live
`ps` row; every measured invocation exits with status zero. The retained Arty
runner uses a fresh copy at `/data/astra/deploy/ps-stats-v7-retained`; a reused
v5 work copy that had seen interrupted test boots was excluded after failing
during service startup, while the pristine v5 control passed.

Three other measured alternatives were rejected. A text summary saved no measurable
time and added 304 supervisor text bytes. A shortened 64-bit divide loop passed
the arithmetic oracle but expanded its common MC68030 path from 148 to 210
text bytes, grew each linked command by 60--64 bytes, and left physical `ps`
latency unchanged. Directly copying executable pages from the launcher's user
mapping into their destination frames removed one logical copy, but moved the
physical spawn median only from 46.696 to 46.514 ms and `echo` from 203.895 to
203.548 ms while adding 52 kernel bytes; it was reverted. The generated code
therefore does not justify assembly or a larger launch fast path. The remaining
spread is dominated by shared scheduling and presentation, not a safe
command-local hot path to hand-code.

## Direct command paths and bounded Terminal input (2026-08-24)

`which` and `cat` now use the existing direct VFS client instead of attaching
`filesystem.library` for one path operation. On the physical Arty, `which`
improved from 613.865 to 516.374 ms (`15.9%`) and `cat` from 484.002 to
434.490 ms (`10.2%`). Their stripped MC68030 text sizes are 7,402 and 8,646
bytes. Shell-facing numeric `status N` diagnostics were removed; the numeric
result remains in `?`, while structured trace events retain internal status
values. The clean v9 ROM is SHA-256
`3aca9853c7ceb4dd3c4ee3fd7931987d1ac946bf55fe27f9b2b9cf992d214f57`;
its pristine storage image is
`ef909b22c0f875aa68dcd2ecd5268449d887b795665aab1177d684dc1b22e0da`.

The interactive command buffer remains 512 bytes: 511 characters plus its
terminating zero. This is the command-length limit; the smaller transport
queues are event buffers and do not reduce it. The limit was not enlarged.
Terminal now appends and erases at the end of a line in O(1) work instead of
redrawing the growing line, and its fallback repaint correctly handles wrapped
lines and erases the complete old tail. Keyboard repaint is coalesced for at
most 50 ms. The input service drains every available physical-input batch
before acknowledging the IRQ, so a burst cannot leave an asserted FIFO behind
after servicing only its first 16 records.

The exact v12 pair was cold-booted from pristine storage on Arty and Terminal
was launched through the canonical desktop double-click before input testing.
An exact 511-character `echo` line completed in 215.141 ms with two render
batches. A 600-character probe was bounded at 511, completed in 266.494 ms
with two batches, and the following `echo alive` completed in 267.253 ms.
The physical FIFO was empty afterward, submissions equaled completions, QEMU
remained live, and the console contained no kernel panic. Focused input-core
and supervisor host tests pass. The retained Terminal has 41,952 text bytes,
8 data bytes, and 106,448 BSS bytes; the input service has 7,109 text bytes.
The exact ROM is SHA-256
`e9c768974a42e7e27b5c333e8764bd340f33f96773adad15ec0ca5636c66e3ae`;
the pristine storage image is
`0c5440196cc099fe2ff72ebb08afd738d283cbc18468d3d27c841e6d073b3bf2`.

`mkdir` and `rm` now use that same direct VFS path rather than loading
`filesystem.library`. Their union-member selection is the existing VFS policy,
factored once and also used by the library instead of copied into each command.
The stripped images fell from 13,092 to 8,976 bytes for `mkdir` and from 13,080
to 8,968 bytes for `rm`; text fell by 2,016 and 1,722 bytes respectively, and
each lost 334 BSS bytes. A pristine five-run Arty control measured successful
v12 operations at 558.531 ms (`mkdir`) and 589.326 ms (`rm`). The v14 `mkdir`
median was 505.888 ms. `rm` wall time varied with journal service from 545.655
to 716.203 ms across two five-run boots, so no wall-time gain is claimed for
it; the decoded command trace nevertheless shows its image shrinking from
13,080 to 8,968 bytes and a successful run stage falling from 381,063 to
321,664 us in the matched status probe. Both images return zero for successful
create/delete, and a second delete reports `not found` and returns 2 through
`$?` without a numeric shell narration.

The complete VFS functional, ASan/UBSan, and GCC analyzer gates pass on Beast,
including the shared union-selection regression, and the local and Beast source
hashes match. The exact v14 ROM is SHA-256
`0ac51c452d52dca70d0ba090e7d493e8d0dcd073cfe2ff3e7932be7bd30bf3d3`;
its pristine storage image is
`f04c682a60a8d40b8aa25e8613ffbecaccd3be843dc3f5fbab39e1ab3551f8fb`.

Directory traversal now follows the same rule: `vfs_union` owns the one union
open/read/close implementation, `filesystem.library` adapts that state to its
stable ABI, and direct clients such as `ls` call it unchanged. The shared
reader continues to a later member when an earlier member returns a successful
empty batch; its focused regression and the complete VFS functional,
ASan/UBSan, and analyzer gates pass on Beast. No command has a private member
loop or an alternate fast-path policy.

The physical v16 gate also restores the union listing contract: rows remain
intentionally undeduplicated and `ls` prints the shared reader's `[member]`
result, so the two shadowed `devices` entries are visibly `[0]` and `[1]`.
Short and long listings, missing and empty directories, create/delete, and
their shell-visible return codes all pass. The stripped `ls` is 13,440 bytes
with 12,638 bytes of text and 9,964 bytes of BSS. Restoring the labels added
only four file bytes over the unlabelled shared-reader image. Its matched trace
changed the short-list run stage from 313,843 to 315,914 us (`+0.66%`) while
restoring the missing information. A ten-run physical gate measured a
504.533 ms median with exactly two presentation batches, 57 render commands,
and 16 glyph runs; persistent-event writes still create the known slower
samples. POST, full-range BIST, stage 8, and the host-seeded
`2026-08-24T16:43:08Z` wall clock pass. The exact v16 ROM is SHA-256
`31f8a9b27a98165030547e8852295fca4e93c5fcc28174ee4f289d054a0fd26f`;
its pristine storage image is
`97d9607282b61ddbdb095c72c61d648fc14dd7f9094554aedddf5949bbb96512`.

The integer-only `__d_snprintf` implementation used by picolibc's `strftime`
is now shared POSIX code rather than a private `date` implementation. A
date-only linker selection flag pulls that archive member before picolibc's
floating formatter; other commands pay nothing, and there is no wrapper or
second formatter. `date` remains exactly 16,740 file bytes with 11,118 text
bytes, 24 data bytes, and 96 BSS bytes. Its focused functional, ASan/UBSan, and
analyzer gates pass. The exact v18 image passed every supported physical form:
default, UTC, ISO date, ISO seconds, RFC 2822, custom `%H:%M`, and epoch
seconds; all exited zero and the trace had zero wraps or drops. The retained
ROM remains SHA-256
`31f8a9b27a98165030547e8852295fca4e93c5fcc28174ee4f289d054a0fd26f`;
the v18 pristine storage image is
`174b5d1ac0ccba95807835ce91860c6f666df38aa575c828a4e8c6188ef0e343`.

## Systemic command/filesystem latency fix (2026-08-20)

The former `ls` symptom was process-wide startup work, not directory rendering.
Every fresh filesystem client searched all Kit manifests for
`filesystem.library`, read and mapped the library again, and then paid a VFS
`HELLO` before its first real operation. The retained fix is shared by commands
and applications; `ls` itself is unchanged.

- `astra_image.py` derives bounded `LIBS:.providers/` records from validated
  Kit manifests and embedded library identities. Old images retain the manifest
  sweep fallback.
- Syscall ABI `0x00010012` adds `LIBRARY_ATTACH` (56). Axiom caches exact
  library identities and initial pages; later processes share R/RX pages and
  receive private copies of initial writable pages without filesystem lookup or
  library-file I/O.
- `STOR` v7 returns small `READ_PATH` files inline. `STOR` v8 fuses session
  creation with the first path operation; older services negotiate normally and
  receive the operation in a second exchange.
- At this historical checkpoint the common Terminal launcher used the
  supervisor's one-request whole-image read path. The current transactional
  streaming loader above supersedes that transport; there is still no
  command-output cache.

On `astra-arty`, the stock warm `filesystem.library` open was 794 ms. Cached
identity attachment reduced the best warm sample to 149 ms; the v8 fused first
operation reduced two consecutive no-output `ls -l` filesystem opens to 61 ms
and listings to 86/51 ms. The focused transport regression proves that lazy
connect plus the first `OPEN` is one service request. Repeated `which` and
`mkdir` runs exercise the same shared path. The disposable board candidate is
not the stock runtime; restore and final gate status are recorded in
`HANDOVER-launch-latency.md`.

The clean candidate ROM
`b5fabd384b1b5a8ab82aed8d064b22da0ea32b30a12cc94412a045898b39049a`
and pre-boot image
`ff7540bebc34bccd7a82a93426e1fd381c1359ea05dd3f56456b00fc673618f3`
passed POST, stage 8, and `ls`/`which`/`mkdir`/`rm`/missing-`cat` on the
physical board without panic. The board was then restored to the untouched
stock `astra_boot.bin` and `storage-terminal.img`; that pair also passed POST
and stage 8.

## Generic hardware framebuffer copy and text presentation (2026-08-21)

The renderer now has one generic overlap-safe 64-bit AXI block-copy path. It
uses legal AXI INCR bursts of at most 16 beats, splits every transaction at a
4 KiB boundary, and handles both copy directions. Unsupported formats and
alignments retain the established pixel blitter. The API is not Terminal
specific: Graphics Kit exposes `AstraTextBox` and
`astra_text_box_scroll()`, and Terminal is its first caller.

The exact 1280x644 RGB565 desktop copy improved from 4,401,758 to 824,550
renderer clocks (`5.34x`); the smaller identity copy improved from 5,822 to
1,254 clocks (`4.64x`). The complete graphics regression passes, including
the coordinate-unique 1,280-pixel screen-offset gate. A clean Vivado 2024.2
production route on Beast connects all 67,294 nets and passes the actual
187.5 MHz setup/hold gate at `+0.070/+0.011 ns`. It uses 32,548 LUTs, 39,402
registers, 12,253 slices, 129.5 BRAM tiles, and 81 DSPs.

The exact production bitstream is active on the Arty. Its 1280x644 overlapping
copy improved from 11,917,253 to 1,431,536 clocks (`8.32x`), or 7.63 ms at
187.5 MHz. Repeated cold boots, HDMI unplug/replug, the production-width offset
gate, framebuffer checks, and 48 kHz stereo audio all pass; the HDMI manager
reports `HDMI 720p60 audio=2ch-LPCM-48k-24bit`.

Terminal now coalesces child-output presentation for at most one 60 Hz frame.
It does not enlarge the stream queues, delay keyboard repaint, or change the
generic Graphics Kit/renderer path. On the physical board, a controlled
10-run A/B of `ls -l COMMANDS:` changed the median from 1,848.384 ms and eight
presentation batches to 1,594.976 ms and six batches (`13.7%` faster, `25%`
fewer presentations). A second 10-run candidate gate passed at 1,668.367 ms
and six batches. Twenty-five measured candidate runs completed without a new
panic. The measurement tool can now fail on a caller-selected maximum median
batch count; the retained hardware invocation uses six.

The installed matching pair is ROM SHA-256
`e07e648f347e2a522ce8297f67af213a2281ff4f6cecb504ec1ad19e7670b07e`
and pre-boot storage SHA-256
`b033561aeb0b3728301a6ada6fdf84aef7d32e499a1e848462ed79d197ab2352`.
A physical cold boot of this exact application pair passed full POST, stage 8,
and a fresh five-run `ls -l COMMANDS:` gate at 1,622.439 ms and six median
presentations. The framebuffer-copy and Terminal-presentation release gates
are closed.

### Lane-realignment acceleration (2026-08-22)

This is the active continuation point. Board profiling found that Terminal's
own 816x420 hardware scroll takes about 658,818 render clocks (3.51 ms), but
the compositor then copies the 816x440 content into its decorated cache about
six times per listing at 5.46--5.51 million clocks (about 29 ms) per copy. The
source pitch is 1,632 bytes on byte lane 0; the destination pitch is 1,640
bytes and the two-pixel content inset starts on byte lane 4. The released burst
mover accepts only equal source and destination byte lanes, so this ordinary
case falls back to one pixel per AXI transaction. The four-message Terminal
stream queue is not the bottleneck: a physical 16-message experiment changed
the `ls -l COMMANDS:` median only from 1.622 to 1.643 seconds and was reverted.

The uncommitted candidate fixes the shared BLIT path, not Terminal. It keeps
aligned, full-width 64-bit AXI bursts, captures each source chunk, realigns its
bytes in hardware, writes only valid edge lanes, splits at 4 KiB, and preserves
both overlap directions. The final focused simulation covers all 64
source/destination lane pairs, W-channel backpressure, outside-byte
preservation, overlap, and the exact compositor geometry. It passes at 1,254
clocks for the aligned identity copy, 423,950 clocks and 6,224 bursts for the
816x440 lane-mismatched copy, and 824,550 clocks for the desktop overlap. The
complete graphics regression passes with the same counts.

Four measured 200 MHz out-of-context revisions reduced the mover's realignment
cone from `-1.644 ns`, through `-0.328 ns` and `-0.300 ns`, to no failing mover
path. The retained revision inserts a 128-bit source-pair register between the
distributed-RAM word selection and byte shift. Its OOC route misses by only
`-0.005 ns` on the pre-existing blitter mask-cache-to-ARVALID path; the OOC
clock lacks final `HD.CLK_SRC`, so the five-picosecond estimate is not used as
an unrelated RTL-edit trigger.

Two clean full-feature Vivado 2024.2 implementations on Beast routed every net
without an incremental checkpoint, but the fail-closed 187.5 MHz release gate
rejected both and wrote no bitstream. Default `Performance_Explore` routed
68,062/68,062 nets and failed setup/hold at `-0.059/+0.046 ns`.
`Performance_ExplorePostRoutePhysOpt` routed the same 68,062 nets and improved
the gate to `-0.030/+0.013 ns`, with three failing setup endpoints and
`-0.066 ns` total violation. The worst path is
`scheduler_i/client_start_reg/C` to
`sprite_builder_i/prep_state_reg[1]/CE`: 4.930 ns data delay, four LUT levels,
and 80.7% routing. The other two failures end at sprite admission-position
clock enables. The mover is absent from all failing endpoints. Resources are
33,202 LUTs, 39,741 registers, 12,435 slices, 129.5 BRAM tiles, and 81 DSPs.

The local sprite-start pipeline breaks that exact route cone. A third clean
`Performance_ExplorePostRoutePhysOpt` implementation routes 68,015/68,015
nets with zero errors and passes the actual 187.5 MHz setup/hold gate at
`+0.022/+0.018 ns`; the worst setup path is now an existing glyph-input
register to DSP path. It uses 33,176 LUTs, 39,686 registers, 12,296 slices,
129.5 BRAM tiles, and 81 DSPs. Timing-clean candidate bitstream SHA-256 is
`baf8a6d9524125409ef0d0004272cb06dfa22d6144f1ad444e798b07c8e93b70`.

The candidate is based on `d27d6be762cd6335ae366a597b49c2092b6e1bd5`
and its implementation/test changes are:

- `fpga/arty/graphics/astra_render_blitter.sv`;
- `fpga/arty/graphics/astra_render_copy_burst.sv`;
- `fpga/arty/graphics/astra_sprite_line_builder.sv`; and
- `fpga/arty/graphics/sim/tb_astra_render_blitter.sv`;
- `fpga/arty/linux/astra_render_certify.c`.

The synchronized Beast snapshot is
`/mnt/Documents/astra68/work/buttery-scroll-20260821/repo`. The local
sprite-start pipeline is now implemented. All nine focused sprite modes pass,
including the 16x128 worst case in 1,562 clocks, and the complete graphics
suite passes with the lane-realignment counts above and all 5,120
production-width screen-offset pixels. The production route passes timing,
but physical application qualification now rejects the candidate.

The routed `.bit` was converted for Linux FPGA Manager and verified against
the known predecessor conversion. The exact manager binary is
`63b3a8e158ede638245be470fdacb6ef78ffab01700bf96af5e73268a89c42b9`.
The existing renderer certifier was extended rather than adding another tool;
source SHA-256 is
`b5b8f54f4eb63801ba533c787b4c92b8bfe8ff11d20e6906902b0f8065530f90`
and the static ARM binary is
`0f74fd2bf9a9a5d758dcd6bd93a748eac8f27b02fc11227188a4f41b4d45e828`.
It passes all 64 source/destination byte-lane pairs, exact neighbor-byte
preservation, the 1,280x644 screen-offset copy in 1,432,002 clocks, and the
exact 816x440 compositor copy in 705,006 clocks against a 3,125,000-clock
one-frame budget. The full renderer, sprite, Copper, and 48 kHz audio
certifiers pass; sprite hardware worst case is 1,564 clocks with zero AXI or
deadline errors. Two clean boots of ROM
`e07e648f347e2a522ce8297f67af213a2281ff4f6cecb504ec1ad19e7670b07e`
and pre-boot storage
`b033561aeb0b3728301a6ada6fdf84aef7d32e499a1e848462ed79d197ab2352`
each pass full POST, full-range SDRAM BIST, and initial-image stage 8 without a
panic.

The exact Terminal gate fails, so the candidate is not a release. After the
canonical desktop double-click, its five-run median is 1,612.009 ms and seven
presentation batches; a ten-run repeat is 1,562.989 ms and 6.5 batches. Both
exceed the retained maximum of six. A controlled ten-run reload of prior
production bitstream
`f3ccce904124714d77b3f936debdad195a29c5f089ffb0c0783c195397369bb4`
passes at 1,676.396 ms and six batches. The lane candidate is faster, but the
batch regression is repeatable and is not waived. The Arty is attached to
Beast; a Beast-local JTAG scan sees the Cortex-A9 and XC7Z020, and Beast reaches
`astra-arty` directly. Beast exposed no `/dev/video` capture device during this
checkpoint, so HDMI hot-plug was not rerun; the board correctly reported no
sink.

The final recovery ran from Beast, the Arty's physical host. A direct
FPGA-manager reload dropped the board's Ethernet before the certifiers could
run, so Beast restored the exact prior raw bitstream
`f3ccce904124714d77b3f936debdad195a29c5f089ffb0c0783c195397369bb4`
over the local Digilent JTAG link and reset the Zynq APU through XSDB. The
persistent production image booted, FPGA manager reports `operating` with
flags `0`, all three PL identities read correctly, and the clean ROM remains
installed. The writable storage image has the expected post-test hash
`2ac835b9887269b9ede05944e04e65eb05acca73243d2155c3d73841e8a36c9b`.
The next step is to remove the candidate's extra presentation batch without
weakening the one-frame Terminal contract, then repeat physical qualification
with an HDMI sink present. Candidate and control logs are retained under
`/mnt/Documents/astra68/work/buttery-scroll-20260821/lane-realign-sprite-start/hardware`.

### Combined lane-realignment and storage qualification (2026-08-23)

The application rejection above is superseded. Measurement isolated the
latency variance to the events service rewriting its persisted snapshot as one
cross-process storage request per 72-byte record. STOR protocol v9 adds one
bounded shared-area write operation, and the events service now serializes the
same alternating-bank snapshot into one static buffer before writing it. The
one-second persistence cadence and recovery format are unchanged. `ls` also
uses one existing stdio output buffer, and directory batches now reserve each
entry's actual name length instead of the 63-byte maximum.

On the physical Arty attached to Beast, a ten-run empty-`ls` probe with
persistence enabled completed nine runs in 214--229 ms; the remaining 367 ms
run coincided with a real snapshot write. The former slow mode was 578--709
ms. Packing actual directory-name lengths reduced the traced listing phase
from a 311.339 ms median to 267.310 ms (`-14.1%`). The exact final
`ls -l COMMANDS:` gate then completed 20/20 runs with exactly three
presentation batches, 66 commands, and 16 glyph commands; median latency was
779.731 ms. Evidence SHA-256 is
`1831c03c759864f4b1e04b8e3917410e0481018a71e43a4aea9f906d0cd52f49`.

Rebooting the exact final storage image preserved the previous boot's event
records, and `events --boot -1` recovered them. The clean pre-boot image is
`e6d6f7379bf53303065bf954f44c359e96ba62bf71affd741bf55c9c0bf5c3e2`;
the reboot ring and decoded trace are
`765995b39389ef4e0744e79686a118d5c0156a7a8cae2c4e18790ea334ad8491`
and
`7a594a3790e3a0e8971aef1aa5f92a226ca37882171f8e5922d6af319238396a`.
A measured 4-to-16 message/pump expansion was rejected: its 20-run median was
843.464 ms with the same three batches, so the original four-message limits
were restored. Rejected-run evidence is
`b93d1fd30d3d582d9fe4cf7ed48d4de277e0162705cb8e183a998fb42847670`.

The exact expanded renderer certifier binary
`0f74fd2bf9a9a5d758dcd6bd93a748eac8f27b02fc11227188a4f41b4d45e828`
passes the complete 29-command renderer test with 1,196,651 pixels and zero
backpressure, all 64 lane pairs, the 1280x644 offset copy in 1,431,179 clocks,
and the exact 816x440 compositor copy in 703,962 of the 3,125,000-clock frame
budget. Sprite, Copper, and 48 kHz audio certification also pass; sprite
hardware maximum remains 1,564 clocks with zero AXI or deadline errors.
Repeated POST, full-range SDRAM BIST, and stage 8 pass.

The timing-clean lane candidate is therefore the latest qualified capacity
authority. It is active only as a volatile FPGA-manager load; manager state is
`operating`, flags are `0`, and persistent boot files remain unchanged as the
rollback authority. The three-presentation result did not improve when queue
depth quadrupled, and the exact hardware compositor already finishes in
703,962 clocks. Remaining latency is bounded by the current serialized
process/storage path and the 60 Hz presentation contract rather than the lane
mover. Beast exposed no HDMI capture sink, so repeated HDMI hot-plug and
visual inspection are the sole remaining physical release gate.

## Shared retained-text control and rejected blitter stream (2026-08-20)

Graphics Kit draw lists now include a validated same-surface rectangular copy.
It reuses the ordinary overlap-safe Astraea BLIT and is available to any
retained text control, including editors and word processors. Terminal uses a
renderer-independent scroll callback, coalesces consecutive scrolls, copies
preserved rows in hardware, and redraws only exposed or damaged cells. Glyph
runs remain hardware commands; the MC68030 updates cell state and submits the
bounded draw list rather than rasterizing pixels.

On the physical Arty, clean `help` display latency fell from 616.034 to
400.276 ms (`-35.0%`), with render commands falling from 116 to 58 and glyph
runs from 43 to 12. Exact `ls -l COMMANDS:` latency fell from 2778.107 to
1898.428 ms (`-31.7%`); commands fell from 366 to 141 and glyph runs from 127
to 16. The candidate ROM/image pair is
`588b239643bc45685e11ac97ffad23ce9f2409ffd15a221fc54966a951dd648e`.

A follow-on cached-RGB565 blitter stream measured 5,822 to 3,470 cycles for
64x16 copy and 20,734 to 11,782 cycles for the real 1280-pixel compositor
width. It closed a complete production route at `+0.122/+0.048 ns`, but the
board displayed a mid-screen compositor wrap on the exact 1280x644 desktop
BLIT. That physical correctness failure rejects the RTL regardless of speed.
The 33-line stream was removed, the live PL and SD boot were restored to the
qualified front-panel release, and only the real-width simulation regression
was retained. Active BOOT.BIN is again
`545f0ccb259972bc7fc26c08f9080dc7033ef7627693ff1ff03085c98a9e3d9c`;
FIT remains `74838cdca1f45205bd2d69e6fba51f59b5fae43c2de39fde3e8f9cdc4ed4eb2d`.

The permanent screen-offset gate runs at the real 1,280-pixel width. Its
coordinate-unique pattern cannot hide the recurring 640-pixel wrap; it checks
production-width final pre-HDMI RGB lines and
separately verifies the exact 1280x644 overlapping compositor copy across the
whole 1280x720 surface in simulation and on the board. Qualified hardware
passed the board copy in 11,917,253 cycles; the final pre-HDMI gate passed all
5,120 pixels in four production-width lines. Six production final-pixel
signature forms fully routed but failed setup timing, so no checker RTL, MMIO
register, ABI bump, or resource cost is retained.

The kernel's normative implementation contracts are
`KERNEL_ARCHITECTURE.md`, `MEMORY_MAP_AND_PMMU.md`, `ABI.md`,
`LOCKING_AND_PREEMPTION.md`, `RESOURCE_OWNERSHIP_AND_FAILURES.md`,
`MEMORY_BUDGET.md`, `SHARED_AREAS_AND_BULK_RINGS.md`,
`K9_MEMORY_PRESSURE.md`, `K10_DEVICE_AND_OBSERVABILITY.md`, and
`TEST_AND_FAULT_INJECTION_PLAN.md`; `STATUS.md` separates implemented evidence
from planned work.

The product-level operating-system direction is `OS_VISION.md`. Focused
userspace design and the provisional kernel driver boundary live in
`USERSPACE_ARCHITECTURE.md`,
`DRIVER_AND_SERVICE_ARCHITECTURE.md`, `DESKTOP_AND_UI.md`, `TERMINAL_AND_POSIX.md`,
`APPLICATION_AND_KIT_MODEL.md`, `USERSPACE_BUDGET.md`, and
`RESOURCE_MODEL.md`. Runtime evidence is recorded in the dated sections above.

## Userspace bring-up line (2026-08-05)

`docs/FILESYSTEM_CONCURRENCY.md` records the userspace slice; the current
continuation is the Arty software-milestone
section below. Implemented and gated in that first slice: the
observability contract (`docs/OBSERVABILITY.md`), a bounded userspace
allocator, a QEMU Vesta block service that lets `sw/kernel/block.c` run in
emulation for the first time, a strict big-endian MC68030 ELF acceptance
profile with a transactional loader, and a capability-gated process-info
syscall at ABI `0x0001000e`.

**Astra runs a real user program at boot.** Boot ABI 0.3 carries
`user_image_base`/`user_image_size`; firmware embeds the linked supervisor ELF
in the ROM, copies it to `0x02004000`, verifies the copy, and reserves only the
pages it fills. The kernel loads it with `kernel_process_create_executable()`,
and the process validates its startup block, queries the syscall ABI, reads
`PROCESS_INFO` on its own handle, and exits with a tagged status the kernel
reports and gates on. Verified in QEMU end to end; 30 kernel suites, 6
userspace suites, sanitizers, `-fanalyzer`, both QEMU certifiers, and the
MC68030 kernel image all pass.

The initial image is 1,306 bytes of MC68030 text (6,468-byte ELF). The kernel
and that image now ship LZ4-compressed in ROM and are CRC-32 verified after
firmware decodes them into their load addresses, which took the ROM from 90.7%
to **72.1% used: 189,064 of 262,144, with 73,080 free**. `docs/MEMORY_MAP.md`
records the budget, the measured codec comparison, and the rule for what is
allowed to live in ROM — notably that lwext4 is not, because stage 0 reaches a
FAT boot volume in 2,020 bytes.

**Block admission is implemented and a user-mode service moves real data.**
Syscall ABI `0x0001000a` adds transfer memory (`DMA_CREATE`) and the three
block calls (`BLOCK_QUERY`, `BLOCK_SUBMIT`, `BLOCK_COLLECT`), all gated on a
device lease the initial image receives at launch. The service holds
process-owned, cache-inhibited, physically contiguous transfer memory, submits
a read naming a buffer handle rather than an address, and collects a completion
that distinguishes device errors, resets, and media changes. Every boot with
media attached proves the whole path:

```
Initial image ....... block round-trip verified, service resident
```

The block facade in `sw/userspace/storage` now has a lease-backed
`AstraBlockBackend`, so the initial image reads its boot sector through
`astra_block_read()` — the call a filesystem makes — over an interrupt-driven,
deadline-bounded transport rather than by driving syscalls itself.

The K1/K10 qualification pair is now `K1_QUALIFICATION=1`, not a boot workload;
that build remains the gate for the performance budget and the device-IRQ
report.

lwext4 is qualified big-endian behind three one-line upstream fixes but is
neither vendored nor adopted. **A VFS and an interactive terminal now exist**
— see "Software milestones on the Arty QEMU backend" beneath the override
below; this paragraph is otherwise unchanged from when neither did.

## Driver substrate candidate (2026-08-04)

The working tree contains a provisional Axiom device-lease substrate: an
8-entry sealed registry, 8 exclusive generation-tagged leases, a 2-lease
per-process limit, read/transfer/administer rights, trusted bootstrap grants,
query/reset/revoke and bounded input-read syscalls at ABI `0x00010007`, and
owner-death quiesce/reset before handle closure. It reuses handles, ports,
shared areas, rings, IRQ
endpoints, and user-copy; no generic I/O queue or class policy was added.
Focused device and process tests and the freestanding MC68030 link pass on
Beast. Full qualification and physical device registration remain pending.

## Input transport candidate (2026-08-04)

The working tree defines input ABI `0x00010001` in `sw/include/astra/input.h`.
It carries 20-byte big-endian physical keyboard and pointer records. Keyboard
values are USB HID Keyboard/Keypad Usage IDs; pointer records carry signed
relative/absolute axes or normalized button IDs. The Vesta queue has 32 slots,
31 usable records, independent 16-bit keyboard and pointer sequences, a host
generation, sticky overflow, and IRQ source 5. Axiom registers this controller
as exclusive physical device `0x494e0001` when present and supplies bounded
quiesce/reset/drain operations.

The exact QEMU 9.2.4 host build passes `emu/qemu/test-input.py` on Beast. The
active Arty ARMv7 build SHA-256 is
`54468714d702eb807237ecf12865c1cf88c2956637cd14d5ec753fcebe31b517`
and passes the same certifier on the Arty plus the unchanged deployed Axiom ROM
through `ASTRA68-QEMU READY`. The prior active emulator is retained under its
hash-qualified rollback name. The certifier covers keyboard usage
mapping, signed X/Y motion, pointer buttons, FIFO ordering, independent device
sequences, full-queue overflow, pop/overflow acknowledgement, and IRQ
assertion/deassertion. The Arty Linux kernel has USB HID, input, keyboard,
mouse, and evdev support built in. No physical keyboard or mouse event node was
present at the checkpoint, so direct evdev hardware-event qualification remains
pending. Protected userspace can drain physical events through the input-device
lease. The cross-built, allocation-free input service core now implements a
replaceable keymap with a built-in US map, separate key/text events, modifiers
and Caps Lock, bounded repeat,
integer pointer acceleration and clipping, eight-client focus routing,
generation/overflow reset repair, and bounded 56-byte application-port
  messages. Beast functional, sanitizer, GCC analyzer, MC68030 cross-build, and
  kernel regression gates pass. The production launcher now starts the input
  core as a protected Astra process, transfers the input-device lease and IRQ,
  and publishes `INPUT_SERVICE`. The display process is its unique seat owner;
  pointer-only observers require explicit delegation. Physical evdev
  qualification is tracked by the active GUI pointer checkpoint below.

## Arty execution and memory (2026-07-30)

The Arty Z7-20 is attached to `beast`. Its Zynq processing system runs Linux
and executes the big-endian MC68030/PMMU machine through the Astra QEMU
backend. The exact Axiom K1-K10 image reaches the required markers there, and
the accepted current CPU performance baseline is approximately 30 MHz
equivalent. The MC68030 CPU is not part of the Arty PL resource budget.

The Arty's 512 MiB DDR is divided into 128 MiB of Astra guest RAM, 128 MiB of
graphics RAM, and 256 MiB for Linux and host services. The graphics RAM is the
physically contiguous `no-map` range `0x18000000..0x1fffffff`; Linux System RAM
ends at `0x17ffffff`. QEMU preallocates Astra's 128 MiB from normal cached Linux
RAM at launch. Read-only `/`,
writable `/data`, persistent SSH state, DHCP, and the shared Mac-directory service all
survive the graphics boot-package replacement.

The 128 MiB profile is hardware-retained at
`/data/astra/deploy/memory128-538563d8`. The active stripped ARM QEMU is
`538563d84b8e43ffd3e2d9cc149594c3ccd8a3b372bc0b32ef8336818fabc5ca`,
the ROM is
`970dd9dae9ddbfc07fa26fa696d76512a2fc2f78be509f347057dc219c5e878f`,
and the launcher is
`c8f7fdc36621e14332242b25ad6c300f34f676721c06273d08c4234c5e05f82a`.
On 2026-08-09 the exact board command line included the preallocated 128 MiB
backend; QEMU held 147,892 KiB RSS with no swap, POST reported 128 MiB, the
kernel reported 32,370 free of 32,768 physical pages, storage reached stage 8,
and the published 90x30 text plane contained a live `WORK:>` prompt. Rollback
artifacts are retained beside the active files in that deployment directory.

[`GRAPHICS_ARCHITECTURE.md`](GRAPHICS_ARCHITECTURE.md) is the normative Arty
Vega/Astraea contract. It locks 1280x720p60, INDEX8/RGB565/XRGB8888 scanout,
two-axis ring scrolling, two INDEX4/INDEX8 tile layers with pixel scrolling,
64 INDEX8 sprites up to 128x128 with sixteen independently selected 256-entry
palette banks, sprite 0 as the optional desktop cursor, fenced scene promotion,
copper, blitter/virtual sprites, geometry, pattern and flood operations, AFNT
glyph expansion, bounded command rings, and the ARM/PL coherence and release
gates.

The output decision is based on PG235, UG934, PG230, UG471, DS187, and the
Digilent board manual. CEA 720p60 uses 1280x720 active, 1650x750 total, a
74.25 MHz pixel clock, 742.5 Mb/s TMDS lanes, and a 371.25 MHz DDR serializer
clock. The rejected 1080p60 proposal would require 1.485 Gb/s lanes and a
742.5 MHz serializer clock, beyond the device's characterized clock limits.

The qualified transport shell remains the rollback checkpoint. Physical HDMI
shows its complete 1280x720 test raster; retained screenshot
`docs/evidence/astra-arty-720p60-hdmi-20260729.png` has SHA-256
`a5ca652d6cbc075b018f0b7f4f08d414f9ebbac6edaf81460d4fc3b8f1d3f12d`.
That shell established the exact 74.25 MHz pixel and 371.25 MHz serializer
path before DDR integration.

Integrated checkpoint `boot-text6` supersedes the earlier `full8` base. It includes
the Zynq PS, GP0 control, three 64-bit HP DDR read paths, INDEX8/RGB565/
XRGB8888 framebuffer scanout, both tile layers, palette stores, ordered
composition, four-line scheduling, atomic frame-boundary scene promotion,
counters, HDMI, and a four-row double-buffered CP437 boot-text plane. All nine
directed simulation programs pass; the final 1280-pixel INDEX8 tile test takes
1,346 of 4,444 available 200 MHz clocks. ARM software also passes strict
cross-compilation, GCC static analysis, and host unit tests.

The exact full-system Beast Vivado 2024.2 route meets every constraint with
+0.002 ns setup, +0.019 ns hold, and +0.538 ns pulse-width slack; all 23,261
routable nets complete without error. It uses 13,096 total LUTs, 12,892
registers, 29.5 BRAM36-equivalent tiles, and five DSPs. Bitstream SHA-256 is
`869b0b4917135486376ab868f5599963dced75a2f8cfa76b2261fe01d0439cf4`.
The boot-text route history records and fixes the initial flip-flop inference,
two pixel/font cones, oversized selector carry chain, and direct GP0 readback
cone. Exact source, report, methodology, CDC, resource, and artifact identities
are in `fpga/arty/graphics/TIMING_CLOSURE.md`.

The sprite-qualified XSA generated its own FSBL and boot package. That
`BOOT.BIN` SHA-256 is
`b88b142cc4624ea70dafc65b0aec900d506bcf17f90fc1c7ea6f5f834d8098a5`;
active `image.ub` SHA-256 remains
`e9ef016f059cb3bc71138edf2a5ae47646a0e11b3dab3b81f7362f592b97b542`.
The previous boot files remain on the card under hash-qualified rollback
names. U-Boot and the Linux kernel payload are unchanged.

The text-free 1280x720 PNG is checked in exactly, SHA-256
`cdf001bb70e130c9267f5205261eb3855f1b74cbbba832dbcd22fb6d66f77ff9`.
The deterministic big-endian RGB565 image is 1,843,200 bytes, SHA-256
`86eb30739db77b85f4deb1915fb9cb9263ab4755ae318ffb1b7a4a95b7017ba4`,
and CRC32 `611029ee`. On Arty, the static ARM loader wrote and read back every
byte, promoted graphics generation 1 with zero deferrals, and published the
final text bank at generation 2. A live row-only update advances generation 3;
MMIO reports capabilities `0x000000ff`, including the sprite engine.
The FPGA manager reports `operating`, `/` remains read-only, and `/data`
remains writable. Hardware evidence is
`docs/evidence/astra-arty-boot-text6-hardware-20260730.log`. Direct monitor
confirmation passes: all four dynamic rows are visible, correctly colored,
aligned inside the lower panel, and clear of the Astra OS badge. The retained
frame is `docs/evidence/astra-arty-boot-text6-hdmi-20260730.png`, SHA-256
`e2c00ecb090a4ac6eb5e93a48cf5562976e8ff1868932552672e9f877c13d0ae`.

Historical hardware release `sprite64-cdc-full2`, now superseded by the
complete renderer below, replaced `boot-text6` and the earlier
`sprite64-full3` candidate. It provides 64 INDEX8 shapes up to
128x128, sixteen per-sprite-selected ARGB palette banks, front/behind planes,
alpha and opacity, scaling, signed positioning and clipping, atomic scene
promotion, and all-pairs collision reporting. A bundled-data CDC correction
delays pixel-domain line-slot capture until one clock after the synchronized
publication toggle; a directed skew test presents the toggle before its tag
and proves that stale line metadata cannot be consumed.

The complete directed suite passes. The exact Beast Vivado 2024.2 production
route completes all 41,778 routable nets with +0.024 ns setup, +0.034 ns hold,
and +0.538 ns pulse-width slack. It uses 21,954 LUTs, 23,003 registers, 85.5
BRAM36-equivalent tiles and 51 DSPs. On Arty, the 64-way stress, fully hidden,
edge-clipped and aligned-grid phases all pass with zero dropped pixels,
overflow, AXI errors or deadline errors. Fully off-screen sprites issue zero
source reads and admit zero pixels. Physical HDMI inspection confirms the
aligned 8x8 grid has no scanline flicker. Exact evidence is in
`docs/evidence/astra-arty-sprite64-cdc-hardware-20260731.log`; the retained
frame is `docs/evidence/astra-arty-sprite64-cdc-hdmi-20260731.png`.

The bounded command/fence transport, descriptor validation, timeout/reset
handling, shared pixel writer, basic clipped fill, and overlap-safe same-format
copy were hardware-qualified at Stage 1 and are retained in the complete
renderer below. Exact Stage 1 checkpoint
`path-boundary-3/full-route-9` routes all 55,816 nets and meets every constraint
at +0.003 ns setup, +0.013 ns hold, and +0.538 ns pulse-width slack. It uses
28,549 LUTs, 33,087 registers, 84.5 BRAM36-equivalent tiles, 61 DSPs, and
10,982 of 13,300 physical slices. The complete directed suite still passes;
the tile workload improves from 1,346 to 1,179 build clocks.

Stage 1 `BOOT.BIN` SHA-256 was
`c118b5a9aa88b1d5d682ce92553b8b45b9133aa9efe1a8b8d5c0432ecd137509`;
the FIT was and remains
`e9ef016f059cb3bc71138edf2a5ae47646a0e11b3dab3b81f7362f592b97b542`.
After reboot, FPGA manager reports `operating`, capabilities are `0x000001ff`,
`/` is read-only, and `/data` is writable. Six consecutive basic-renderer
hardware runs each execute six fenced commands, verify 1,196,608 result pixels,
and report zero backpressure or engine errors. Visible scene promotion and
restore reach generations 6 and 7, and the complete 64-sprite hardware
regression passes unchanged. Evidence is
`docs/evidence/astra-arty-render-basic-hardware-20260801.log`; exact route and
source identities are in `fpga/arty/graphics/TIMING_CLOSURE.md`.

Independent sprite source width and height from 1 through 128 are now an
explicitly certified contract, not merely permissive descriptor fields. The
scene-store regression accepts all 16,384 source-size pairs while preserving
all 131,072 horizontal scaling checks and rejects zero or 129 on either axis.
The line-builder regression performs real first/last-row fetches for every
width and every height, both reflection axes, minimum legal 64/128-byte pitch,
admitted-pixel and AXI-byte accounting, and allocation-bound checks. The public
NDK now reports 64 descriptors, the 8,192-pixel line budget, INDEX8 sources,
independent 1..128 source extents and 1..1024 destination extents.

No sprite RTL or bitstream changed for this qualification. On the Stage 1
`c118b5a9...` Arty release, three consecutive certifier runs, including a
no-reboot repeat, cover every width and height across two packed 64-sprite
scenes. Both dimension phases fetch exactly 4,352 AXI bytes per line, peak near
3,070 build clocks, and report zero drops, overflow, AXI errors or deadlines.
The retained visible scene uses 327,680 bytes rather than the 1 MiB maximum
shape set. That Stage 1 certifier's SHA-256 is
`4692f723917d2589085580f7222c55804da6653e6db1cd77797abfad12b77f3a`;
the complete graphics-regression log is
`/mnt/Documents/astra68/work/sprite-v1/variable-dimensions-1/graphics-regression-20260801.log`
(SHA-256 `f074f620419a2392bab91aded927f5523abd9a0b99683546bbfc0eaa4c629be3`);
the persistent hardware log is
`/mnt/Documents/astra68/work/sprite-v1/variable-dimensions-1/hardware-20260801.log`
(SHA-256 `7955bfb2199dc64af4c36b6cfe12c474a63d999b0355c701dc8d3116fbc44657`).

The complete blitter is now hardware-qualified. Exact production checkpoint
`full-route-24-checkpoint-49` includes framebuffer and tile scanout, boot text,
64 independently sized sprites, command/fence transport, and the complete
blitter. Beast Vivado 2024.2 routes all 59,647 routable nets with +0.013 ns
setup, +0.051 ns hold, +0.538 ns pulse-width slack, and no failing endpoint.
The 74.25 MHz pixel domain has +2.620 ns setup slack. The route uses 30,185
LUTs, 36,050 registers, 84.5 BRAM36-equivalent tiles, 66 DSPs, and 11,695 of
13,300 physical slices.

Active `BOOT.BIN` SHA-256 is
`dfd34dd31bafd199889d7d2cc1f9f2682b72636b296e4f4b3a1964d4ef6acbaa`;
the FIT remains
`e9ef016f059cb3bc71138edf2a5ae47646a0e11b3dab3b81f7362f592b97b542`.
FPGA manager reports `operating`, `/` is read-only, `/data` is read-write, and
the complete splash readback still passes 1,843,200 bytes with CRC32
`611029ee`. Ten consecutive complete-blitter hardware runs each retire 29
fenced commands and verify exactly 1,196,651 pixels with zero backpressure or
engine errors. Coverage includes scaling, X/Y reflection, clipping, keying,
all 16 ROPs, format conversion, premultiplied source-over/opacity, palette
expansion, MASK1 suppression, and overlap-safe copy. Installed certifier
SHA-256 is
`c1ea9c75827c5de62a930ed5119b3ce72e26358146bb98b1c3e6783204f01c5d`.
Exact closure history, source identity, route artifacts, and NAS hardware
evidence are in `fpga/arty/graphics/TIMING_CLOSURE.md`.

Virtual sprites are now certified as bounded groups of ordinary BLIT commands
into hidden surfaces. This deliberately reuses each command's validation,
deadline, completion, and reset contract; the final sequence is the group
fence, and presentation is forbidden unless every completion through it
succeeds. A parent-command hardware batch sequencer was rejected after
behavioral success because it regressed focused 200 MHz timing. Restoring the
qualified command processor reroutes at +0.002 ns setup slack. On the unchanged
Route-24 bitstream, ten consecutive Arty runs each complete 64 scaled RGB565
virtual sprites, verify 16,384 pixels and fence 93, and report zero
backpressure or engine errors. Exact failed experiments and retained evidence
are in `fpga/arty/graphics/TIMING_CLOSURE.md`.

Geometry command validation and dispatch now cover lines, outlined/filled
rectangles, circles, ellipses, transparent/opaque 8x8 pattern fills, and a
bounded scanline flood fill through the shared writer. Flood workspace is
caller-provided and validated; exhaustion returns `WORK_OVERFLOW`. The complete
graphics suite passes, including the focused 60-pixel fill, eight-pixel
overflow case, and integrated 40-command regression.

Geometry is now hardware-qualified at an actual 166,666,672 Hz renderer
clock. The exact checkpoint-44 `full-route-17-166m667` route meets setup at
`+0.060 ns`, hold at `+0.016 ns`, and pulse width at `+0.538 ns`, with zero
failing endpoints. It uses 32,207 LUTs, 39,098 registers, 84.5
BRAM36-equivalent tiles, 70 DSPs, and 12,344 of 13,300 slices. The exact
bitstream SHA-256 is
`b2599c5c3b00f312fc4a8b149944243c0885741f5df061f91d521009ce24472b`;
active `BOOT.BIN` is
`08e188e2747ec801df151e517c50c126029b388ba048af6a95cd22549f24b3c9`.
The unchanged FIT remains
`e9ef016f059cb3bc71138edf2a5ae47646a0e11b3dab3b81f7362f592b97b542`.

The complete 22-program graphics regression passes against the exact source.
Ten consecutive Arty hardware runs then pass the complete blitter, 64-command
virtual-sprite group, line/rectangle/circle/ellipse/pattern batch, exact
60-pixel bounded flood, and one-entry workspace overflow. Every run reports
zero backpressure; geometry takes 18,901 through 19,299 renderer clocks and
overflow containment takes 1,597 through 1,804 clocks. Installed certifier
SHA-256 is
`ca83f3c564613fe88e0cf15399d94b6a5b2200c118b2a53e8202bb5cfdea7d2c`.
Evidence is retained under
`/mnt/Documents/astra68/work/render-v1/flood-1/cp49-postroute-strategy/full-route-17-166m667/hardware-cert`.

The 200 MHz `full-route-14` result remains rejected at
`-0.249/-38.962 ns` across 456 endpoints. A requested 185 MHz build quantizes
to 187.5 MHz and also fails at `-0.218/-12.770 ns` across 176 endpoints. These
measurements establish the 166.667 MHz point rather than a waiver. The
measured 200 MHz path population is distributed across command, writer,
blitter, geometry, flood, framebuffer, sprite, and PS interconnect logic.
Post-route routing/critical-pin optimization gives zero improvement, while
`Performance_ExtraTimingOpt` cannot place more than five percent of movable
instances. Those failed experiments remain relevant if a future feature again
pressures timing; they are not open geometry work.

AFNT glyph expansion is now hardware-qualified for MASK1, A4, A8, INDEX4, and
INDEX8. The first timing-clean candidate exposed a real intermittent AXI
failure on hardware: two of ten runs lost descriptor beat zero when legal
inter-beat backpressure occurred. The receiver now captures each descriptor
beat only on `RVALID && RREADY`, and a focused three-cycle-gap regression
guards that contract. The exact replacement route at 166,666,672 Hz meets
setup at `+0.078 ns`, hold at `+0.015 ns`, and pulse width at `+0.538 ns`; all
68,601 routable nets complete without error.

The complete graphics release now includes dual-bank copper. Each bank holds
4096 instructions in BRAM; WAIT, SKIP, validated MOVE, IRQ, command dispatch,
and hardware-enforced register timing classes are connected. Focused copper
tests and the frozen complete graphics regression pass. Ten consecutive Arty
copper certifications pass bank switching, execution, IRQ delivery, command
dispatch, and containment of an aligned forbidden MOVE target. Ten complete
renderer runs and ten sprite runs also pass with zero backpressure, timeout,
reset, dropped sprite, overflow, AXI, or deadline error.

The exact complete route at 166,666,672 Hz meets setup at `+0.036 ns` and hold
at `+0.016 ns`. It uses 37,534 LUTs, 44,655 registers, 118 BRAM36-equivalent
tiles, 83 DSPs, and 13,036 of 13,300 physical slices. All routable nets are
complete. The first post-route physical-optimization checkpoint left one AXI
address connection incomplete despite meeting timing; `route_design
-preserve` completed it without changing the `+0.036/+0.016 ns` result. The
normal build now runs the same documented repair as a post-route hook and a
clean from-source build independently completed, generated a bitstream, and
passed the exact timing gate.

The hardware-qualified recovery bitstream SHA-256 is
`6281d7cd544e279edf693d1fe41a7e47259845afdc3c3c0d0045e18c04e27879`;
active `BOOT.BIN` SHA-256 is
`9637e1035acb9d1bd6d2bd0eec2e3cf9ca5c13023560af8d2b4f27a546444504`.
The clean reproducibility build bitstream SHA-256 is
`7fdda9ab456d8df7c8d6eacf3c2b337d1409f47bc0ea0888ac51eaa76a125f0c`.
Exact route, regression, deployment, and hardware evidence is retained under
`/mnt/Documents/astra68/work/render-v1/copper-1/integration-13` and recorded in
`fpga/arty/graphics/TIMING_CLOSURE.md`.

The inherited root image still emits nonfatal read-only volatile-directory,
unclean FAT, interface-rename, and resolver warnings. Those host-image cleanup
items are separate from the now-correct graphics memory map.
