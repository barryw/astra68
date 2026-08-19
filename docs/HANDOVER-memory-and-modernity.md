# Astra 68 — Handover: memory, fragmentation, and what "modern" means here

Date: 2026-08-19. Written to be read cold. Read `CLAUDE.md` first, then
`docs/HANDOVER-boot-and-shell-gate.md`, which this continues — its §11 built the
POSIX file half and a heap, and the heap is what this document is mostly about,
because it was built the wrong way and has not been fixed yet.

**Nothing here is committed. Everything currently in the tree is green**: kernel
30/30, `sw/userspace`, `test-display.py`, `time-boot.py`, `test-events.py`,
`irq_quarantine_probe.py`, and `test-terminal.py` at
`ASTRA TERMINAL PASS 33 commands`.

> **2026-08-19, later the same day — §5 items 1, 2 and 3 are done.** Reserved
> areas, cluster fault-in, decommit, and `heap.c` rewritten on top of them.
> `astra_heap_bytes` is gone. All of the above still green, with four new
> `test_area` cases. What is left is §5 items 4 and 5, which are measurements
> and a decision, plus the ceiling question in §7 below. Read §6.1 before
> continuing — two things this document asserted turned out not to be true.

---

## 1. The standing instruction

Set by the person whose project this is, and it governs every implementation
decision from here:

> Use **modern** methods, but do not blindly copy modern code. Work within the
> constraints of the system — a 68030 with an MMU and 128 MB of RAM — and make
> damn sure we are not using 1980s methods of designing kernels. Exec was an
> amazing kernel for its time; we do not want to copy it verbatim.
>
> And: do it right the first time, so it does not have to be ripped up later.

Both halves matter, and the second one is the one that was violated. "Do it the
simple way for now" is not a saving when the thing being deferred is a design
decision — it is a decision made badly, and the next person pays for it with
interest.

Three tests to apply, in order:

1. **What would someone designing this today do?** Not what does Linux do — what
   is the idea Linux is expressing, and does the idea apply here.
2. **What do the constraints change?** 68030 at roughly 30 MHz, 128 MB (32,768
   frames of 4 KiB), a 22-entry ATC, hardware table walk, and **one core**. That
   last one deletes most of a modern allocator's complexity, and saying so is
   part of doing it right — dropping what does not apply is as important as
   taking what does.
3. **Does the capability model make something possible that Unix cannot do?**
   Several times now the answer has been yes, and taking the Unix answer would
   have been a downgrade.

Modern frequently means *less*. musl is more modern than glibc partly by having
dropped things. "Modern" is not a synonym for "elaborate".

## 2. What is wrong with the heap that is in the tree

`sw/userspace/posix/src/heap.c` provides `sbrk` over **one area of a fixed
256 KiB**, created on first allocation, which can never grow and never returns a
page. `astra_heap_bytes` is a weak symbol a program may override.

That is a fixed partition with a ceiling compiled into the image, chosen by
whoever wrote the file rather than by what the program does or what its owner's
budget allows. It is Exec-era thinking and it should not have been written. It
works, every gate passes, and it is still the wrong shape.

Three specific faults:

- **It cannot grow.** A program that needs 300 KiB fails at 256.
- **It never gives anything back.** A long-running editor ratchets upward until
  it dies. On a 128 MB machine that is not a detail.
- **The ceiling is a constant, not a budget.** The kernel already knows every
  owner's quota. A capability system that then hardcodes a second, smaller,
  invisible limit in userspace has thrown away the thing that made it better.

## 3. The design that replaces it

### Reserve is not commit

This is the one idea everything else follows from. There is 2 GB of user address
space and 128 MB of RAM. **Naming memory and owning memory are different
operations**, and today Astra conflates them: `kernel_area_create` calls
`kernel_memory_alloc_pages_zeroed_tagged(..., page_count, ...)` and commits every
frame at creation. A 2 MiB window surface that touches half still costs 2 MiB.

So areas gain a **reserved** form: creation takes the address range and commits
nothing.

### Commit on fault, in clusters

The kernel already does exactly this for stacks. `grow_user_stack`
(`sw/kernel/process.c:1821`) is reached from the PMMU fault handler at 6533 and
6567, and it decides by **address range** — no page-table bit is needed, because
the fault address says which object it belongs to. The area window is
`KERNEL_VM_AREA_BASE + slot × KERNEL_VM_AREA_SLOT_SIZE`, so the same trick
finds the area.

**Populate a cluster, not a page.** Linux calls it fault-around; the case is far
stronger here. An exception on a 30 MHz 68030 costs a frame push, a handler
entry, a table walk and an ATC fill, against a 4 KiB clear that is a few hundred
cycles. Eight to sixteen pages per fault is the right order of magnitude, and it
wants measuring rather than guessing.

### Decommit

`free` of a large block should hand frames back — a kernel operation shaped like
`MADV_DONTNEED`: drop the committed pages of a reserved area, keep the
reservation. Without it, "returns memory to the system" is a phrase rather than a
behaviour.

### The ceiling is the owner's quota

No `astra_heap_bytes`. The heap reserves generously — a whole 2 MiB area slot,
or several — and grows until the owner's frame quota refuses it. That is the
honest answer, it is what the kernel already tracks, and it is what a capability
system is *for*: you know your budget up front instead of racing a global pool
and meeting an OOM killer. seL4 and Fuchsia both work this way. Astra should not
import Linux's overcommit; it already has the better model.

### `sbrk` is a shim, not the design

`brk` is legacy in Linux and absent from musl. It survives here only because
picolibc's allocator asks that way, and it should be a bump pointer over
reserved space — which is what it should always have been, and what finally lets
it grow.

## 4. Fragmentation — the question, and the real answer

Two different problems share the word, and they have different answers.

### Physical frame fragmentation

Free frames scattered such that no long contiguous run exists.

**With an MMU this mostly does not matter.** Any frame will do for an ordinary
allocation; the page tables provide virtual contiguity. It bites only where
something needs *physically* contiguous memory: DMA buffers, and page tables
themselves.

Astra's allocator is a **bitmap with a rotating next-fit hint** —
`contiguous_search_hint` and `find_contiguous_frames` in `sw/kernel/memory.c`.
Fine on a fresh machine, and it degrades with uptime exactly the way next-fit
always has.

What modern kernels do:

| method | what it is | worth it here? |
|---|---|---|
| **Buddy allocator** | power-of-two order lists, coalescing on free; finding an order-N run is a list pop rather than a scan | Probably. It is not large, and it turns a scan into a pop. |
| **Grouping by mobility** | Linux migratetypes: keep unmovable allocations out of regions you may want to assemble a big run from | Overkill for 32,768 frames. |
| **Compaction** (`kcompactd`) | **moves pages and rewrites the page tables** — a real defragmenter, in Linux since 2010 | See below: Astra can do this *better* than Linux can. |
| **A reserved zone** | set DMA's frames aside at boot | Cheapest and most predictable. A display driver must not fail at hour six because the frame map got shredded. |

**Astra can compact, and cleanly.** An area is a capability-named object reached
through page tables the kernel owns; a process never holds a physical address. So
the kernel can move an area's frames and rewrite its mappings and the process
observes nothing. Linux's compaction is hard because of pinned pages, kernel
mappings and drivers holding physical addresses — Astra's design does not have
most of that. This is a genuine advantage and it should stay in the design's
back pocket.

### Heap fragmentation inside a process

100 KB free and no 8 KB block. This is `malloc`'s problem, and the answer is
categorically different.

**A C heap cannot be compacted.** Raw pointers, no indirection, no way to find
and rewrite every reference. This is *why* every modern C allocator fights
fragmentation by **avoidance** rather than repair:

- **Size classes / segregated fit** (jemalloc, tcmalloc, mimalloc). A freed
  32-byte block can only ever be reused by another 32-byte request, because it
  came from a page run dedicated to that class. External fragmentation largely
  disappears *by construction*, at the cost of a little internal waste from
  rounding up to the class. This is the big one.
- **Large allocations bypass the arena** and get their own mapping, returned
  exactly on free, so they never punch holes in the small-object heap.
- **Return empty runs to the kernel**, on a decay timer rather than instantly, so
  a program that allocates and frees in a loop does not thrash the fault path.
  jemalloc calls it purging.

**Who genuinely defragments:** managed runtimes with a moving collector — JVM,
.NET, Go, JavaScript engines — because the runtime knows every reference and can
rewrite them. And **handle-based systems**: classic Mac OS's Memory Manager
compacted its heap by relocating blocks behind `Handle` double indirection, with
`HLock`/`HUnlock` discipline. That is the 1980s answer to this exact problem on
this exact CPU, and it is precisely what not to do — it puts the burden on every
programmer and every pointer.

### Where that leaves Astra

The three levels each have an answer, which is how you can tell the design is the
right shape:

- **Virtual fragmentation stops mattering** once reserve and commit are split.
  Address space is free.
- **Physical fragmentation is repairable** by remapping, because the MMU
  indirection is there and nothing holds physical addresses.
- **Fragmentation within a page, among small C objects** is the only hard one,
  and size classes fix it by construction.

### The allocator decision, which is not made

picolibc's `malloc` is a first-fit free list — a 1970s design that fragments
under editor-style churn. A segregated-fit allocator sized for this machine is
perhaps 300 lines, with **none** of jemalloc's threading machinery, because there
is one core: no per-thread caches, no lock-free fast paths, no atomics.

I believe replacing it is right. It should be decided against a number, not
taste. **The measurement:** run an allocation trace with editor-shaped churn —
many small objects, mixed lifetimes, occasional large buffers — and report peak
RSS against live bytes. A ratio above roughly 1.5 says first-fit is costing real
memory on a 128 MB machine and the replacement pays for itself. That experiment
is the next step after §3, not a deferral.

## 5. Order of work

1. ~~**Reserved areas and cluster fault-in.**~~ **Done.** See §5.1.
2. ~~**Decommit.**~~ **Done.** See §5.1.
3. ~~**Rewrite `heap.c` on top of them**, and delete `astra_heap_bytes`.~~
   **Done.** See §5.1.
4. ~~**Measure the allocator.**~~ **Done, and it was replaced.** See §5.2.
5. ~~**The frame allocator's contiguous path.**~~ **Done: a reserved DMA zone,
   and not a buddy allocator.** See §5.3.

Nothing in §5 is outstanding. What is left is in §6 and §6.2.

### 5.2 Item 4: the allocator was measured, and it was replaced

`sw/userspace/commands/heapbench` is the instrument, and it answers in counts
rather than times on purpose: QEMU's cycle counter is TCG bookkeeping, and the
Arty's m68k is emulated on its ARM cores, so a time from either is not 68030
time. Bytes, pages and faults are as true here as on hardware.

It runs the editor-shaped trace §4 asks for — 20,000 operations, 85% objects
under 512 bytes, 12% up to 4 KiB, 3% between 8 and 64 KiB, freed in an order
unrelated to the order taken, from a fixed seed so two runs compare.

| allocator | peak live | peak footprint | ratio |
|---|---|---|---|
| picolibc first-fit | 403,491 B | 688,416 B | **1.70** |
| segregated fit (`posix/src/alloc.c`) | 403,491 B | 552,960 B | **1.37** |

§4's bar was 1.5. First-fit was over it, so the replacement was the answer the
number gave rather than the one taste preferred. Peak live is identical across
the two, which is what makes it a like-for-like comparison rather than two
different workloads.

The replacement is segregated fit: 20 size classes to 2 KiB, each served from
runs of pages holding nothing else, so a freed 32-byte block can only be reused
by another 32-byte request and external fragmentation is prevented by
construction rather than repaired. Larger requests take whole pages and are
returned exactly on free. Runs that empty are decommitted, which is §4's third
point and what makes a long-running program's footprint follow its live set
back down. None of jemalloc's threading machinery is present, because there is
one core — dropping what does not apply is most of what makes it the right
size. The run is found from a pointer through a page-index table rather than a
per-block header, because on a 16-byte class a header is not overhead, it is
the allocation.

**The cluster size is 16, and now for a reason.** `sbrk` is a bump pointer, so
the heap's pages are touched in address order and both terms follow from the
footprint alone: `ceil(N/C)` faults, and `C·ceil(N/C) − N` pages committed but
never touched, which averages `(C−1)/2`. At the measured N of 135 pages the
table is:

| cluster | faults | wasted pages |
|---|---|---|
| 4 | 34 | 1 |
| 8 | 17 | 1 |
| 16 | 9 | 9 |
| 32 | 5 | 25 |
| 64 | 3 | 57 |

Minimising `(N/C)·F + ((C−1)/2)·Z` — F the per-fault overhead, Z the cost of
clearing a page — gives `C ≈ sqrt(2N/(F/Z))`, and at F≈Z that is `sqrt(270)`,
about 16. F and Z are within an order of magnitude of each other by
inspection: a page clear is ~1024 longword writes, a fault is a frame push, a
handler entry, a table walk and an ATC fill. So 16 stands, and it is flat
enough nearby that 8 would also be defensible; 32 and above clearly are not,
because the waste grows linearly while the faults saved do not. Pinning F/Z
exactly needs a real 68030 and is the one thing here a clock would settle.

### 5.3 Item 5: a reserved DMA zone, and explicitly not a buddy allocator

The decision was made from what the drivers actually ask for, which turned out
to be a very short list. Every allocation in the kernel asks for **one frame at
alignment one** — stacks, code pages, page tables, library pages, and every
page of every area — except one caller:

| caller | frames | alignment |
|---|---|---|
| display framebuffer (`ASTRA_RENDER_BUILDER_BYTES`) | 64 | 1 |
| block transfer (16 sectors x 512) | 2 | 1 |

`test_memory.c` then establishes that the risk is real rather than theoretical:
comb the frame map by freeing every other frame and half the machine is free
while the largest run in it is one frame, at which point a two-frame transfer
is refused and a framebuffer certainly is. That is §4's "a display driver must
not fail at hour six because the frame map got shredded", reproduced.

**A buddy allocator would have failed that same test.** It turns finding a run
into a list pop and coalesces on free, but it cannot manufacture contiguity
while the alternating frames are still held. It also has exactly one customer
making a handful of requests. So it would have been machinery bought for a
problem it does not solve.

The zone does solve it: 128 frames, 512 KiB, 0.4% of the machine, sized as the
measured demand rather than a round number — 64 for the framebuffer and two for
each of the 32 block transfer slots. DMA searches it first and falls back to
the general pool, so nothing that works today stops working. The test now shows
both requests succeeding on a combed map, from inside the zone; setting
`KERNEL_DMA_ZONE_FRAMES` to zero makes it fail again, which is how the test was
checked.

The part that mattered most was the least visible: the **scattered** allocation
path — every area page, every stack page, every code page, and precisely what
combs the map — checked only the blocked bitmap and walked straight through the
zone. Reserved in name only is worse than not reserving at all, because it
looks handled.

### 5.4 A latent limit this uncovered

Placing the zone at the top of memory panicked the machine at boot with
`Class: PMMU translation, Fault: 0x09F60000` — the top of RAM less the
emergency reserve less the zone, exactly.

**The kernel can only directly address the low 32 MiB.** The supervisor
identity-maps SDRAM from `0x02000000` through root index 15 and nothing above
it has a supervisor translation. A frame beyond that can be given to a process,
which reaches it through its own page tables, but the kernel cannot zero it,
poison it, or copy through it — and zeroing a freshly allocated frame is the
kernel touching it. The general allocator never meets this because it searches
upward from the bottom and a 128 MB machine never gets that far, so it has been
latent.

It is written down as `KERNEL_DIRECT_MAP_LIMIT` in `memory.h` and the zone is
placed below it. **That the machine believes it has 128 MB while the kernel can
directly reach 32 MB of it is a real limit and the next thing in this area
worth fixing** — see §6.2.

### 5.1 What items 1 to 3 actually did

**The reserved form.** `kernel_area_create` takes flags now, and
`KERNEL_AREA_CREATE_RESERVED` makes it take the address range and commit
nothing. An ordinary area is unchanged and still commits at creation, so
nothing on the machine changed shape except what asked to.

**Presence is per page.** `KernelArea.physical_pages[]` holds
`AREA_PAGE_ABSENT` (`0xffffffff`) for an uncommitted page, and
`committed_pages` counts the rest. Zero was the obvious marker and is the
wrong one: whether physical zero is a real frame depends on the boot info's
RAM base, so it is a property of the machine rather than of `area.c`. A value
with its low bits set cannot be a page-aligned frame on any machine. The
struct absorbed both new fields without growing — the 2088-byte budget assert
still holds.

**Commit is a cluster.** `KERNEL_AREA_COMMIT_CLUSTER_PAGES` is 16, which is
64 KiB, and clusters are aligned to their own size inside the area so a
program walking upward never re-does work. Sixteen is the order of magnitude
§3's argument gives, **not a measured optimum** — it is the knob item 4's
trace should set.

**Commit reaches every holder.** An area is one object, so a page committed
for one process is published into every address space that has the area
mapped, in the same call. Letting each space fault its own pages in would
have made an area two objects sharing a name, and would have bought nothing:
the frame is spent the moment anybody touches it either way.

**Authority is checked.** `kernel_area_fault` refuses a process that does not
already hold a mapping of that area. Without it, any process could spend an
area owner's frames by reading into a window it was never given. Both the
fault path and the decommit path go through one `authorised_area` helper so
the two cannot drift.

**Kernel-side access.** `kernel_area_write` commits the page it needs — the
same reversal `kernel_process_commit_user_stack` makes for the copy path —
and `kernel_area_read` returns zeros for an uncommitted page rather than
spending a frame to say so. Before this, both indexed `physical_pages[]`
directly, and on m68k `physical_pointer` only casts, so a reserved area would
have been written through a wild address.

**Decommit.** `kernel_area_decommit` (syscall 54) drops the committed pages
lying wholly inside a range and keeps the reservation — `MADV_DONTNEED` in
shape. It rounds *inward*: a page half of which is still live stays. Touching
the address again re-faults and gets a fresh zeroed page.

**The heap.** `sw/userspace/posix/src/heap.c` reserves a whole 2 MiB slot,
commits nothing, and bumps. `astra_heap_bytes` is deleted and nothing defined
one. Shrinking the break decommits the pages it passes. The ceiling went from
256 KiB to 2 MiB and the cost of an unused heap went from 64 frames to zero.

**A bug the coverage audit found, after the first pass called this done.**
`commit_cluster` scanned forward from the cluster's *first* page, so a hole
punched into the middle of a committed cluster by `decommit` could not be
refilled: the scan found the base occupied, committed nothing, and left the
fault unanswered, which retires the process for touching an address its own
reservation covers. The comment above it described the behaviour it did not
implement. It grows the run outward from the faulting page now. The first
round of tests missed it because they only ever decommitted whole clusters —
`test_reserved_area_commits_holes_and_short_tails` fails against the old scan
and passes against the new one, which was checked by reverting the fix.

**Evidence.** Kernel 30/30 from a clean build, with seven new cases:
`test_area` covers commit, sharing across every mapping, authority refusal,
kernel read/write, decommit, holes and short tail clusters, and the commit-time
quota; `test_process` drives the whole thing from outside — create through the
syscall with a bad flag bit refused, the fault answered through
`kernel_process_on_fault` with the thread resumed and nothing reported,
decommit through syscall 54, and an unowned area-window address classified
`KERNEL_PROCESS_FAULT_AREA_WINDOW`; `test_vm` covers the invariant that
replaced slot alignment, in both directions. `sw/userspace` builds and tests;
ROM builds; all five emulator gates exit 0 with
`ASTRA TERMINAL PASS 33 commands`. The `posix` command's self-check `malloc`s
4 KiB, writes every byte, `realloc`s to 8 KiB, and then drives `sbrk` up and
back down twice with the pages written through each time — so fault-in *and*
the decommit-on-shrink path are exercised on the machine, not only in host
tests. `malloc` alone would never have shrunk the break.

**A trap worth repeating.** `rsync -a` preserves mtimes, so restoring a file
on `beast` from the Mac can hand `make` a source *older* than an object built
from something else, and it will quietly keep the stale object. That produced
a test failure that looked exactly like the bug above, after it was fixed.
`make clean` before believing a result you have gone back and forth on.

**One ABI break, deliberate.** `ASTRA_SYSCALL_AREA_CREATE` now reads `d3` as
its flags, and `user_test.S` never set that register — it would have passed
whatever was in it. Every in-tree caller sets it now and
`ASTRA_SYSCALL_ABI_VERSION` went to `0x00010011`, which is what makes a stale
binary say so instead of failing strangely. Unknown flag bits are rejected
rather than ignored, which is what lets flags be added later.

## 6.3 The frame tables are sized by the board, not by the image

Done, and it is the thing §6.2 said was worth fixing before more code bound
itself to the old constant.

`KERNEL_MAX_FRAMES` is gone. It was `ASTRA_RAM_SIZE_ARTY_GUEST / 4096`, 32768,
and every per-frame table was a static array of that length, so the size of the
machine was a property of the image:

- a board with **more** RAM than the constant was refused at
  `kernel_memory_init` and did not boot at all;
- a board with less carried the tables for one it was not;
- and behind both sat a quieter ceiling -- the owner frame links and the owner
  ledger's head and count were `uint16_t`, so raising the constant would have
  stopped at 65535 frames, 256 MB.

The boot ROM already reads RAM size from a hardware register and reports it, so
the number was always known at run time and thrown away at compile time.

The tables are now carved from RAM at init, sized from the count the board
reports, and the links are 32-bit. The arena holds the frame records, the two
owner link arrays, the four bitmaps and the allocation sites --
`kernel_memory_metadata_bytes` states the cost, about 17.5 bytes per frame,
0.4% of the machine. It is placed in the largest range the firmware calls
usable, because the allocator's own bitmaps are inside it and it therefore
cannot come from the allocator; its frames are taken out of circulation
immediately afterwards, once there is a bitmap to record that in. `vm.c`'s
per-frame alias table is allocated the same way, from the frame allocator,
which is running by then.

Every accessor still reads `frames[index]`. The tables became pointers rather
than arrays, so the shape of the code did not change -- only where the storage
comes from.

**A trap worth naming.** The first version keyed the host-versus-kernel arena
on `KERNEL_MEMORY_HOST_TEST`, and `test_memory` does not define it -- it builds
with `KERNEL_MEMORY_NO_POISON` instead. So the host binary took the kernel path
and wrote the arena through a raw physical pointer into its own address space.
It is keyed on `defined(__m68k__)` now, because the kernel is only ever built
for m68k and a target that forgets to define a test macro is exactly how that
went wrong.

**Evidence.** `test_frame_tables_scale_past_the_old_ceiling` builds a gigabyte
-- eight times the old constant, four times what the old links could address --
initialises it, checks the DMA zone lands at a frame index above `UINT16_MAX`,
allocates from it and releases the owner, which is what walks the wide links at
that index, and then returns to a 32 MB board in the same run to show nothing
is sticky. On the machine, the same ROM boots at both RAM sizes the emulator
offers, 32 MiB and 128 MiB, with the tables sized to each.

**What now limits the sizes we can test.** The QEMU machine model refuses
anything that is not its 32 MiB physical or 128 MiB hosted profile --
`Astra68 RAM must be the 32 MiB physical or 128 MiB hosted profile`. That is an
emulator-side constant, not a kernel one, and it is the next thing to widen
when a board with a third memory size arrives. The kernel side is not waiting
on it.

## 6.2 What was the open question, and is not any more

Both items here are closed: the direct map in the commit that extended it to
all of RAM, and the static frame tables in §6.3.

The kernel's 32 MiB direct map, from §5.4. Ninety-six of the machine's 128 MB
can only ever be reached by a process through its own page tables, and anything
the kernel must touch -- a DMA buffer it zeroes, a page it copies through, an
area it writes on a caller's behalf -- has to come from the low quarter. Every
such allocation competes for it while three quarters of the machine looks free.

The modern answer is a temporary mapping window: reserve one or two page slots
in the supervisor space and map a frame into them for the duration of the
access, rather than requiring every frame the kernel touches to be permanently
mapped. Linux called it kmap for the same reason on 32-bit machines with more
RAM than address space, and it is the same shape of problem -- 2 GB of user
space and 128 MB of RAM does not need it, but a supervisor window of 32 MiB
over 128 MB of RAM does.

The alternative -- extending the identity map to cover all 128 MB -- costs 32
root descriptors of 4 MiB pages, which is nearly free, and is worth checking
first. It was not done here because it is a change to the supervisor's address
space made while chasing a different item, and that is how latent faults get
introduced rather than removed.

Also raised while measuring, and not acted on:

- `heapbench` is a real instrument and is now a command, but it is not in any
  gate. Twenty thousand allocations is too slow to run on every build, and its
  output is a number rather than a pass. Run it by hand when the allocator or
  the cluster size changes.
- The allocator returns empty runs immediately rather than on a decay timer.
  jemalloc purges lazily so a program that allocates and frees in a loop does
  not thrash the fault path. Nothing on this machine does that yet; the place
  to put the timer is `give_pages`.

## 6. The other debts from the same session

These were shipped knowingly and are the same category of mistake, in miniature.
`sw/userspace/posix/README.md` lists them too.

| what | what is there | what it should be |
|---|---|---|
| `rename` | absent | **On the critical path, not "later".** Every editor saves by write-temp-then-rename — that is *how* an atomic save works. vim meets it on day one. Needs an operation on the wire. |
| `O_EXCL` | `stat` then `open` | A TOCTOU race that got documented instead of closed. Needs an exclusive-create flag in the protocol. |
| `readdir` | batches **2** entries, because that is what fits in picolibc's `DIR` | Own the allocation once there is a heap, and batch a page — `getdents` in shape. A round trip is 7.5 ms. |
| `opendir` | static pool of 4 | Allocate it. |
| `access()` | ignores the mode argument entirely | Answer it, or do not ship it. |
| `ls` union member | dropped when `ls` became a program | A listing that cannot say which member answered is a union you cannot see. |

## 6.1 Two things this document asserted that are not true

Both were load-bearing for §3, and the next person should not rebuild on them.

**There is no per-owner frame quota.** §3 says the heap "grows until the
owner's frame quota refuses it" and calls that "what the kernel already
tracks". The kernel tracks every owner's frame *usage* —
`kernel_memory_owner_frames`, `owner_ledgers[].frame_count` — and has no
policy ceiling on it beyond the ledger's `UINT16_MAX`, which is more frames
than the machine has. The nearest real quota is `KERNEL_AREA_OWNER_PAGE_MAX`,
which is a different and much smaller thing.

**That quota is 512 pages, and the heap can now spend all of it.**
`KERNEL_AREA_OWNER_PAGE_MAX` is one slot's worth — 2 MiB — across *all* of an
owner's areas, and a fully committed heap is exactly that. It is charged at
commit rather than at creation now, so nothing regresses for a program that
allocates as little as it used to: the old heap committed 64 pages the moment
anything called `malloc`, and the new one commits 16 per cluster touched. But
a program that genuinely allocates 2 MiB will now succeed at `malloc` and
then fail to create a surface, where before it failed at `malloc`. The
failure moved rather than appeared, and it moved somewhere less obvious.

The recommendation, for whoever does item 4: raise
`KERNEL_AREA_OWNER_PAGE_MAX` to `KERNEL_AREA_OWNER_MAX * KERNEL_AREA_PAGE_MAX`
so that an owner's page budget matches the address space it is already
allowed to name, and let `KERNEL_AREA_SYSTEM_PAGE_MAX` and the free frame
count be what actually refuses. That was left alone here because it is a
deliberate policy number with a comment explaining its current value, and
changing it wants the number item 4 produces rather than an argument.

## 7. Where things stand, and how to run it

Built on `beast` in `~/astra68-verify`, an rsync copy. The Mac cannot build the
kernel.

```
rsync -a --delete --exclude 'build*/' --exclude '*.o' \
      --exclude 'astra_kernel.*' --exclude 'astra_boot.*' --exclude 'astra68.rom' \
      sw/ beast:astra68-verify/sw/
rsync -a emu/ beast:astra68-verify/emu/

ssh beast 'cd ~/astra68-verify/sw/kernel    && make -j8 test'   # 30/30
ssh beast 'cd ~/astra68-verify/sw/userspace && make -j8 test'
ssh beast 'cd ~/astra68-verify/sw/userspace && make -j8'        # apps, for the volume
ssh beast 'cd ~/astra68-verify/sw/boot      && make -j8'        # ROM
```

Then, with `QEMU` the host build at
`~/.cache/astra68/qemu-9.2.4/build-host-d5e02d.../qemu-system-m68k` and the image
`/tmp/storage-cmds.img`:

```
for gate in test-display time-boot test-events test-terminal irq_quarantine_probe; do
  python3 emu/qemu/$gate.py $QEMU sw/boot/astra_boot.bin --image /tmp/storage-cmds.img
  echo "$gate=$?"
done
```

**Check the status, not the tail** — `... | tail` reports `tail`'s status, and
three gates once looked green that were not.

`COMMANDS:posix` is the POSIX layer's self-check and runs inside the terminal
gate; each check has its own exit code, listed in the enum at the top of
`sw/userspace/commands/posix/posix.c`. A failure names the step.
