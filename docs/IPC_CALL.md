# IPC call and one-shot reply capabilities (design)

Status: phase 1 implemented, 2026-09-29 (ABI `0x0001003A`, syscall 102).
Phase 2 is not. **As built** at the end of this page records where the
implementation departs from the proposal below, and why.

## Why

A client request today costs six traps: `PORT_CREATE` for a private reply
port, `PORT_SEND_TRY` with the reply send handle attached, `PORT_RECEIVE_TRY`
(which would block), `WAIT_ONE`, `PORT_RECEIVE_TRY` again, and `CLOSE`. The
service side is `WAIT_MULTIPLE`, `PORT_RECEIVE_TRY`, `PORT_SEND_TRY` for the
reply, and `CLOSE`. Every call also creates a port, and its messages and
handles go through the port allocator.

SDLFrameBench (640x480 streaming texture, beast, fake helper) makes three
round trips per frame: `SURFACE_WRITE`, `LIST_SUBMIT` and `WINDOW_PRESENT`.

- The client makes about 22 syscalls per frame, 18 of them in those three
  round trips.
- The display service makes about 12 port syscalls per frame.

After the copy-path fix (2026-09-29, +22% then +4% fps) the frame is about
260k guest instructions. About a quarter of that is the syscall and IPC
machinery: `registration_at`, `thread_at_slot`, `kernel_process_on_syscall`,
`process_for_thread`, `valid_wait_queue_header`, `find_entry`, `port_at`,
`reset_transfer_batch`, `kernel_memory_pin`/`unpin`, `memset`, and
`kernel_allocation_commit`. Each of these is O(1) and cheap. There are simply
too many calls.

## What the kernel allows (facts this design is built on)

- **No continuations.** A syscall runs to completion and returns straight to
  user mode, and a blocked thread's kernel stack is discarded. The waker
  writes the blocked thread's result registers (`complete_wait`, thread.c).
  So a blocked call can only complete if the replier's syscall finishes it
  on the caller's behalf.
- **Delivery into another address space and handle table exists.**
  `kernel_vm_write` over `copy_address_space` (vm.c) writes another address
  space by physical page, but does no lazy commit. `kernel_handle_import_
  reserve`/`commit` (handle.c) install into any table.
- **Handle transfer is a move.** Once a port's last send reference goes, the
  port becomes `PEER_CLOSED` for good (port.c). A reused per-thread reply
  port would therefore need a new "revivable" port mode. A client that keeps
  a send reference would never see `PEER_DEAD` when the service dies, so
  plain port reuse in the NDK is unsound.
- **Protocols put the reply handle in different places**: index 0, 1 or 2,
  or last. Some services keep a reply handle and answer later (terminal and
  storage shutdown). VFS, network, media and PCM use persistent reply
  channels, which already cost three traps per call. Services read the
  sender id (posixd, network, VFS).
- **Policy** (`docs/OS_VISION.md`): synchronous IPC only with a documented,
  short bound. A call blocks only its caller. A service never blocks on a
  client, because a reply cannot block (see below).

## Design

### New object: the reply capability (`KERNEL_OBJECT_REPLY`)

A one-shot capability the kernel creates for each call. It names the calling
thread (slot and generation) and a per-thread call sequence. It carries
`ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_TRANSFER` and can be moved and stored like
any handle, so deferred replies keep working. It cannot be duplicated.

### `ASTRA_SYSCALL_PORT_CALL` (client, one trap)

`d1` points to an `AstraPortCall` descriptor, because the syscall ABI has
only five argument registers:

```c
typedef struct AstraPortCall {
    uint32_t size;               /* sizeof(AstraPortCall) */
    AstraHandle port;            /* service send handle */
    const void *request;  uint32_t request_size;
    AstraHandle *handles; uint32_t handle_count;
    uint32_t reply_index;        /* where the reply capability is inserted
                                    in the sent handle list */
    void *reply;          uint32_t reply_capacity;
    AstraHandle *reply_handles; uint32_t reply_handle_capacity;
    uint32_t deadline_hi, deadline_lo;
} AstraPortCall;
```

The kernel does these steps in order:

1. Validates the descriptor, and commits the reply buffer pages now, with
   `kernel_process_prepare_user_copy` on the current thread. The replier can
   then write them without a lazy commit.
2. Creates the reply capability and inserts it into the message's handle
   list at `reply_index`. Each protocol keeps its own layout, so services
   receive exactly what they receive today.
3. Sends as `PORT_SEND_TRY` does.
   - A full queue returns `WOULD_BLOCK` with nothing consumed, as today. The
     NDK waits on the port and retries.
4. Blocks the thread on its own reply wait until the deadline.

On wake the caller's registers are:

| Register | Meaning |
|---|---|
| d0 | status |
| d1 | reply size |
| d2 | reply handle count |
| d3 | replying process id |

### Replying: unchanged service code

`PORT_SEND_TRY` on a reply capability delivers straight to the caller:

- It copies the message into the caller's reply buffer with
  `kernel_vm_write` on the caller's address space.
- It moves the handles into the caller's table with import
  reserve and commit.
- It writes the caller's d0 to d3 and wakes the caller.

The reply capability is then *spent*, but stays in the service's table. The
service's usual `CLOSE` frees it, so no service has to change.

A reply never blocks. The caller is either waiting, or has gone (see below).
When the reply does not fit, the caller gets `BUFFER_TOO_SMALL` with the
sizes, as `PORT_RECEIVE_TRY` reports it today, and the service's send
succeeds. When the caller's table is full, the caller gets `RESOURCE_LIMIT`,
and the handles are returned to the service's send, which fails as a send to
a full port would.

### Failure and teardown

- **Service closes or dies without replying.** The capability's release
  callback wakes the caller with `PEER_DEAD`, the same outcome as today when
  the reply port's last send handle goes away.
- **Caller's deadline passes, the caller is cancelled (a signal), or the
  caller dies.** The capability is marked abandoned. A later reply succeeds
  for the service and is discarded. The caller sees `TIMED_OUT` or
  `CANCELLED`. A cancelled call cannot be retried transparently, because
  the request has already been delivered. The NDK wrappers already return
  `CANCELLED` rather than restarting, so their behaviour is unchanged.
  Runtime callers that restart waits (`posix_process`, `event_control`)
  stay on ports.
- A thread has at most one outstanding call, and the capability refers to
  that thread's current call sequence. A stale capability from an abandoned
  call can never complete a later call.

## Migration

- **Phase 1 (kernel and NDK)**
  - The new object, the syscall, reply delivery, and the waker writing d3.
    Kernel tests in `test_port.c` and `test_process.c`, following
    `test_wait_multiple_syscall_contract_and_races` for the two-thread
    cases.
  - `astra_port_call()` in `ndk/src/port.c`.
  - The seven NDK helpers that make a fresh reply port per call switch to
    it: graphics, window command and create, application, service manager,
    clipboard, and pointer. Their mocks in `ndk/tests` change with them.
  - ABI bump to `0x0001003A`.
  - Services are untouched.
- **Phase 2 (optional)**
  - `ASTRA_SYSCALL_PORT_REPLY_RECEIVE`: reply, then a non-blocking receive
    of the next request, in one trap. It saves the service's `CLOSE` and one
    receive.
  - Move `posix_process`, `entropy`, `ntp` and `streams` to calls.
  - The persistent-channel libraries (VFS, network, media, PCM) stay as
    they are. They are already at three traps per call.

## Expected gain

- **SDLFrameBench:** the client goes from about 22 to about 7 syscalls per
  frame, and the display service from about 12 to 9 port syscalls in phase 1
  and 6 in phase 2. Per frame, three port creations and frees, their message
  records, and five wait registrations disappear.
- **Instruction share:** about 29% fewer syscalls per frame, applied to
  about a quarter of the frame, predicts **8-12% more frames per second** in
  phase 1. Every NDK request (window, clipboard, launch) gets the same
  six-to-one cut, which helps desktop latency beyond games.

## Effort and risk

- **Effort.** Phase 1 is about 700-900 kernel lines including tests,
  about 200 NDK lines, and test mock updates. That is two to three working
  sessions with the full gate after each step.
- **Risks.**
  - Copying into another address space depends on the reply buffer being
    committed at call time. If the caller unmaps it concurrently, the caller
    gets `BAD_ADDRESS`.
  - The handle import can fail on a full table.
  - Registration and deadline lifetimes must be right when a call and its
    reply race a signal, a deadline, or a death. That is the main test
    focus.

## Alternatives considered

- **Reuse a reply port per thread in the NDK:** unsound, because a service
  death is never observed (see above).
- **A blocking receive that restarts the syscall by rewinding the PC:** it
  saves only one trap per blocking receive.
- **A full seL4 design (ReplyRecv plus scheduling-context donation):** it
  needs continuations and priority donation that this kernel lacks. The
  gain over this design is small at this machine's scale.

## As built (phase 1)

Code: `sw/kernel/process.c` (`port_call_syscall`, `deliver_reply`,
`reply_release`), `sw/kernel/thread.c` (`kernel_thread_call_*`),
`sw/kernel/handle.c` (`kernel_handle_lookup_context`,
`kernel_handle_set_context`), `ndk/src/port.c` (`astra_port_call`). Tests:
`test_port_call_contract_and_races` and `test_port_call_between_processes` in
`sw/kernel/tests/test_process.c`, `test_call` in `ndk/tests/test_port.c`.

- **Measured** (beast, `bench-frame.py --fake-helper`, SDLFrameBench
  640x480, against the post-copy-fix numbers in
  `HANDOVER_2026-09-29_IPC.md`): argb 1,316 -> ~1,565 fps (+19%),
  argb-blend 1,367 -> ~1,560 (+14%), rgb565 1,460 -> ~1,690 (+16%). The
  proposal predicted 8-12%. On the board (release `76104ba8`, 60 s):
  argb 55 -> 57, argb-blend 52 -> 53-54, rgb565 59-60 unchanged. A board
  frame is ~7 ms upload plus 10-11 ms present, helper and fabric time, so the
  trap count is no longer what limits it there.
- **The capability allocates nothing.** Its handle entry's object is the
  caller's thread slot plus one, and its release context is the call number.
  Call numbers are machine-wide, so a capability that outlives its thread
  cannot match whatever reuses the slot. A spent capability has context zero.
  The caller waits on a queue in its own thread record, `call_waiters`; being
  registered there is what "waiting in this call" means, so the deadline,
  cancel and death paths needed no new code.
- **The capability travels as an ordinary handle.** PORT_CALL installs it in
  the caller's table and the normal send moves it, so every protocol's handle
  layout, the transfer pool and the release-on-discard path are the existing
  ones. A request discarded from a closing service port releases its caller
  with PEER_DEAD through the same release.
- **A late reply fails for the service** with PEER_DEAD, and the service keeps
  its handles. The proposal had it succeed and be discarded. The display
  service tears a new window down when its reply fails and the loader does the
  same for a launch; answering success would have left both orphaned. PEER_DEAD
  is also what a send to a closed reply port answered.
- **What the caller cannot accept is the caller's problem.** A reply that does
  not fit, a full caller handle table, or a caller that unmapped its buffer
  each end the call with BUFFER_TOO_SMALL, RESOURCE_LIMIT or BAD_ADDRESS, while
  the service's send succeeds and its handles are released. The proposal
  returned the handles to the service on a full table, which a move cannot do
  once exported.
- **`ASTRA_PORT_CALL_UNSENT`.** PEER_DEAD, TIMED_OUT, BAD_ADDRESS and
  RESOURCE_LIMIT can each come from before or after the send, so status alone
  cannot tell a caller whether its handles moved. Every refusal before the
  send leaves `d1 = ASTRA_PORT_CALL_UNSENT`; the NDK retries WOULD_BLOCK on it
  and clears the handle array otherwise.
- **The reply buffers are checked at the call.** The kernel commits them on the
  caller's behalf and refuses the call with BAD_ADDRESS unless every page is
  mapped writable, so the replier's physical-page write needs no lazy commit.
  A deadline already past answers TIMED_OUT with nothing sent.
- **Kernel stack.** A message may carry 255 handles, so a transfer batch and
  import reservation together are over 3 KiB. Inlined into `port_syscall` they
  took its frame from 2,428 to 5,484 bytes and the send path past the 8 KiB
  kernel stack. The reply's transfer state is static (one syscall runs at a
  time on the one processor) and `deliver_reply` is out of line; the PORT_CALL
  path is 1,228 + 2,232 + 2,104 bytes, below the existing send path.
- A second send on a spent capability answers CLOSED. The capability has
  SIGNAL and TRANSFER only: WAIT_ONE refuses it and HANDLE_DUPLICATE refuses it
  because it has no retain.
- Unmigrated: the display service's own connection to the input service
  (`connect_input`) still makes a reply port. It is service code, which phase 1
  leaves alone.

