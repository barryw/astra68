# Astra userspace runtime

Status: runtime, allocator, metrics, and executable loading implemented; libc pending

## Responsibility

`libastrart.a` is the small, permanent machine-facing runtime beneath Astra
Kits and any later C library. It owns only:

- validation of the versioned initial-process startup block;
- the MC68030 `TRAP #15` calling veneer and typed syscall wrappers;
- process entry and terminal process/thread exit;
- freestanding byte primitives required by C code and compiler output.

It does not own allocation, paths, files, streams, locales, environment
policy, ELF parsing, service discovery, or POSIX behavior. Those facilities
belong to a real libc, Kits, and protected services. Code must not fill a
missing layer with fixed addresses, global shared memory, unbounded polling,
or boot-only APIs.

The target archive is `sw/userspace/runtime/build/m68k/libastrart.a`.
`crt0.o` is separate so libraries and secondary thread entry points never
pull process startup into their images.

## Initial process entry

The loader enters `_start` with:

| Register | Value |
|---|---|
| `D2` | logical address of one read-only `AstraStartupInfo` |
| `D4` | process self handle |
| `D5` | initial thread self handle |
| `USP` | top of the writable initial user stack |

`AstraStartupInfo` is 64 bytes, naturally four-byte aligned, and defined in
`sw/include/astra/process.h`. It records its magic, size, startup ABI, syscall
ABI, process/thread handles, argument and environment vector addresses, and a
bounded capability table. Counts are explicit; absent vectors use a zero count
and zero address. Reserved words must be zero.

The loader validates every mapped range, string bound, handle right, and
entry/stack permission before making the thread runnable. The runtime performs
cheap structural validation again before calling:

```c
int astra_main(const AstraStartupInfo *startup);
```

Returning from `astra_main` terminates the process with its 32-bit return
value. Invalid startup data terminates it with status 127. A process must not
return to the loader.

Each `AstraStartupCapability` is 16 bytes: a protocol-defined four-character
name, process-local handle, exact granted rights, and zero flags in version 1.
The table has at most 32 entries. Handles are transferred into the new process
exactly once before launch; startup-block destruction does not close them.
Closing or process termination releases them through normal handle lifetime
rules.

## Trap transport

`astra_syscall5()` is the single raw C-callable transport. It accepts the call
number, five words, and a naturally aligned `AstraSyscallResult`. The assembly
veneer loads `D0-D5`, executes `TRAP #15`, captures `D0-D3`, and restores the
C-preserved `D2-D5` registers before returning. Higher-level code should use a
typed wrapper rather than depend on this transport directly.

The runtime returns native Astra status values. A future libc translates those
values to `errno` only at its POSIX boundary; native Kits do not use `errno`.

## C library layering

The intended stack is:

```text
application / protected service
  Astra Kits and protocol libraries
  standards C library and optional POSIX personality
  libastrart.a
  Axiom trap ABI
```

The project will port and configure an established C library after process
loading, virtual-memory growth, VFS calls, clocks, and terminal primitives have
real contracts. Astra will not grow a partial home-made libc opportunistically.
The byte primitives in `libastrart` are the freestanding compiler substrate and
may satisfy the corresponding libc symbols without changing behavior.

## Bounded allocator

`sw/userspace/alloc` builds `libastraalloc.a`, a separate library because
`libastrart` does not own allocation. It is not a heap. The caller supplies an
arena and a table of at most eight size classes, each with a hard block count,
so a service publishes an exact memory budget and exhaustion is a reported
status rather than a growth event. A request is served from the smallest class
that fits; a full class fails rather than spilling into a larger one, because
spilling would make the published budget meaningless.

The arena's origin is deliberately outside this contract: static storage today,
a mapped region once VM growth exists. Nothing above the allocator changes when
that happens.

Blocks carry no header. A class occupies one contiguous span with an occupancy
bitmap and a free list threaded through the free blocks themselves, so a free
is validated by address range, slot alignment, and bitmap state. Foreign,
misaligned, and double frees are rejected and counted instead of corrupting the
pool. `astra_alloc_valid()` proves free lists, bitmaps, and live counters agree
and that no free list contains a cycle or an allocated slot.

Allocation failure is injectable by global count and by per-class count, mirroring
the kernel's global-Nth and site-Nth selectors, and the test suite drives every
one of the first 32 allocation sites through injection and requires an exact
resource baseline afterwards.

The default class table was measured, not guessed. Running lwext4's full
evaluation workload big-endian on MC68030 through this allocator
(`make astra-alloc` in `sw/userspace/storage/lwext4-eval`) performs 15,475
allocations with 855 simultaneously live 33..64-byte descriptors, 17 live
4 KiB block buffers, a 126,144-byte charged peak, zero failures, zero
rejections, zero live blocks at unmount, and `astra_alloc_valid()` true
throughout, against a 151,936-byte arena. The MC68030 object is 1,270 bytes of
text with no data or BSS.

Commit accounting and per-process limits still belong to the kernel side and do
not exist yet. Until they do, a service's budget is the arena its image
reserves.

The allocator publishes itself through `astra_alloc_sampler`, so its live and
peak occupancy, failures, and rejections are readable through the contract in
`OBSERVABILITY.md` without any reader knowing the allocator exists.

## Metrics

`sw/userspace/metrics` builds `libastrametric.a`, the fixed-size registry every
module publishes into. `OBSERVABILITY.md` is the normative contract; the short
version is that a module supplies a sampler and its owner registers it under an
instance name, publishing costs only the header, and the registry is 320 bytes
of MC68030 text with exactly 388 bytes of BSS.

## Build and acceptance

The canonical target is big-endian `m68k-linux-gnu-gcc -m68030 -msoft-float`
on Beast. Every runtime change must pass:

- warnings-as-errors host tests;
- ASan and UBSan tests;
- GCC `-fanalyzer`;
- canonical MC68030 cross-compilation;
- generated veneer inspection when trap or entry assembly changes;
- target object size reporting.

The first measured build on 2026-08-04 contained 660 bytes of archive object
text and a separate 46-byte `crt0`, with no data or BSS. Adding `strcmp`,
`strncmp`, and `strncpy` to the byte primitives takes the archive to 776 bytes;
`crt0` is unchanged. These are object-level figures before executable section
garbage collection.

Userspace modules include `<astra/bytes.h>` rather than `<string.h>` for these
primitives. A freestanding target toolchain need not ship `<string.h>` at all
and the Mac's `m68k-elf` does not, so including the hosted header breaks the
cross build even where the symbols resolve.

## Executable loading

`sw/kernel/elf.c` is the acceptance profile and `kernel_process_create_executable()`
is the loader. The profile allocates nothing, touches no address space, and
knows nothing about page tables: it turns an untrusted byte range into a
bounded placement plan that the loader executes or discards.

Astra accepts exactly one shape of executable: ELF32, big-endian, current
version, System V ABI version 0, `ET_EXEC` for `EM_68K` with zero processor
flags, `PT_LOAD` segments only, each page-aligned in both file and memory,
ascending, non-overlapping, readable, and never both writable and executable,
with an entry point at an even address inside an executable segment. Dynamic
linking of any kind, shared objects, thread-local storage, an executable
stack, and any unlisted program header type are rejected outright. A linker's
empty `PT_LOAD` is skipped rather than rejected; there is nothing to map, and
an empty segment claiming file content is still malformed.

Page alignment is stricter than the format requires. The looser congruence
rule lets two segments share a page, which would force that page to carry the
union of two permission sets; Astra refuses the image instead.

`astra_user.ld` is the matching link contract and produces exactly that shape:
read-execute text, read-only rodata, and read-write data with its BSS tail,
each on its own page, plus a non-executable `PT_GNU_STACK`. Verified against
real `m68k-elf` output at three segments and three pages.

The loader maps each segment page by page, zero-filling beyond the file size,
publishes a read-only startup block at the bottom of the user range, installs
the process and thread self handles, and publishes them through the startup
block's capability table as `PROC` and `THRD`. Entry follows the register
contract above: `D2` the startup block, `D4` the process handle, `D5` the
initial thread handle.

Loading is transactional because every frame carries the process owner tag, so
one failure path unwinds mappings, handles, and frames together. The test
suite drives the global-Nth allocation selector across every site the load
touches and requires the free frame count to return to its exact pre-load
value each time.

Recording the executable span per process costs four bytes in `KernelProcess`,
which moves from 544 to 548 bytes on MC68030. The acceptance profile itself is
1,668 bytes of text with no data or BSS.

Granting foreign objects at launch is not implemented. The capability table
format is complete and validated; additional entries arrive with the first
service that has something to hand over.

## The first service

`sw/userspace/supervisor` is the image firmware hands to the kernel. It is the
supervisor/registrar of `docs/USERSPACE_ARCHITECTURE.md` at its first
increment: it validates the startup block the loader published, calls
`QUERY_ABI`, calls `PROCESS_INFO` on its own handle, and exits with a status
that names what it checked.

The status is tagged (`ASTRA_SUPERVISOR_STATUS_TAG`, `sw/include/astra/supervisor.h`)
with one bit per failed check in the low byte. The tag matters: a process that
never reached user mode exits zero, so an untagged zero cannot be mistaken for
success. The kernel prints the outcome the moment the process ends and panics
on anything but the expected value, because the process record is reclaimed
with its last handle and no later poll could recover it.

Gathering is separated from judging. `supervisor_validate()` takes what was
observed and returns the verdict, so the whole judgement runs on the host under
ASan/UBSan and `-fanalyzer`, where a `trap` instruction cannot. The MC68030
image is 1,306 bytes of text with no data or BSS.

## The boot path for the first image

Boot ABI 0.3 adds `user_image_base` and `user_image_size` to `AstraBootInfo`.
Firmware embeds the linked ELF in the ROM file, copies it to
`ASTRA_USER_IMAGE_ADDRESS`, verifies the copy, and reserves exactly the pages
it fills as firmware memory. The contract rejects any description that is
unaligned, larger than `ASTRA_USER_IMAGE_MAX_SIZE`, or not contained in a
readable firmware range — memory the allocator could hand out cannot hold an
image the kernel reads later.

The kernel loads it with `kernel_process_create_executable()` and registers it
as the initial image. No filesystem lookup, no path, no policy beyond the ELF
acceptance profile. Measured end to end under QEMU: the kernel reports
`Initial image ....... loaded, 6468 bytes` and then
`Initial image ....... OK, startup block and ABI verified from user mode`.

## Next implementation boundary

Filesystem lookup remains outside Axiom. Firmware supplies the first image;
the protected supervisor loads later programs through storage/VFS services.
The next slice is the block admission syscalls in `docs/STORAGE_AND_VFS.md`,
which is what the supervisor needs before it can start anything else.
