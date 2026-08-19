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
4. **Measure the allocator.** §4. Then replace it or write down why not.
   Not started.
5. **The frame allocator's contiguous path.** §4 — buddy, or a DMA zone, decided
   by what the display and storage drivers actually ask for. Not started.

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
