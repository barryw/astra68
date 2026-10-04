#ifndef ASTRA_SYSCALL_H
#define ASTRA_SYSCALL_H

/**
 * @file syscall.h
 * @brief The raw Astra syscall ABI: the trap/vector the kernel is entered
 *        through, every ASTRA_SYSCALL_* call number, the ASTRA_SYSCALL_*
 *        result codes a call leaves behind, and the argument-block structs
 *        the handful of calls that need more than scalar registers pass by
 *        address instead.
 *
 * This is the boundary itself: the numbers a caller loads into the syscall
 * register file before `trap #15`, and what it finds there afterwards. It
 * is included from assembly as well as C -- sw/kernel/user_test.S,
 * sw/userspace/loader/start.S and the m68k runtime's crt0.S, syscall.S and
 * thread_start.S all take these numbers from here rather than keeping a
 * second copy, so every comment in this file has to stay a block comment:
 * the assembler that includes this header does not know C++-style line
 * comments.
 */

#include <astra/compiler.h>
#include <astra/address_space.h>
#include <astra/message_abi.h>
#include <astra/limits.h>
#include <astra/object_abi.h>

/** @defgroup astra_syscall_abi Raw syscall ABI
 *  @brief Trap number, call numbers, result codes and the argument-block
 *         layouts the kernel and every userspace caller agree on.
 *  @{
 */

/** The m68k exception vector a userspace caller traps into the kernel with. */
#define ASTRA_SYSCALL_TRAP 15
/** Exception vector number the CPU dispatches `trap #15` through. */
#define ASTRA_SYSCALL_VECTOR 47
/**
 * The syscall ABI's own version, reported by ::ASTRA_SYSCALL_QUERY_ABI.
 * A caller that does not recognize this value should refuse to run rather
 * than guess at a register layout that may have changed underneath it.
 */
#define ASTRA_SYSCALL_ABI_VERSION 0x0001003Eu

/**
 * Identifies the running kernel's syscall ABI. No argument. Returns
 * ::ASTRA_SYSCALL_ABI_VERSION in data[1], a handle to the calling process
 * in data[2], and a handle to the calling thread in data[3].
 */
#define ASTRA_SYSCALL_QUERY_ABI 0
/**
 * Reports the calling process's own startup/boot progress. data[1] is the
 * new progress value; it must be no less than the value already recorded,
 * or the call answers ::ASTRA_SYSCALL_INVALID_ARGUMENT. The initial user
 * image's progress additionally drives the kernel's own boot-milestone
 * accounting.
 */
#define ASTRA_SYSCALL_PROGRESS  1
/** Voluntarily yields the remainder of the calling thread's quantum. No argument. */
#define ASTRA_SYSCALL_YIELD     2
/** Exits the calling process. data[1] is the exit status. Does not return. */
#define ASTRA_SYSCALL_EXIT      3
/** Closes a handle. data[1] is the handle. */
#define ASTRA_SYSCALL_CLOSE     4
/**
 * Reads the monotonic clock. No argument. Returns the elapsed nanoseconds
 * since an arbitrary fixed point, high half in data[1] and low half in
 * data[2] -- the split every 64-bit nanosecond value in this ABI uses.
 */
#define ASTRA_SYSCALL_CLOCK_MONOTONIC  5
/**
 * Creates an event, a sync object latched open by ::ASTRA_SYSCALL_SIGNAL
 * until ::ASTRA_SYSCALL_EVENT_RESET clears it (or, without
 * ::ASTRA_EVENT_MANUAL_RESET, until one waiter has consumed it). data[1] is
 * the ASTRA_EVENT_* creation flags and data[2] the rights to install on the
 * returned handle, placed in data[1].
 */
#define ASTRA_SYSCALL_EVENT_CREATE     6
/**
 * Creates a counting semaphore. data[1] is the initial count, data[2] the
 * maximum count, and data[3] the rights to install on the returned handle,
 * placed in data[1].
 */
#define ASTRA_SYSCALL_SEMAPHORE_CREATE 7
/**
 * Waits for one waitable handle: a sync object, timer, thread, process,
 * port send/receive endpoint, bulk-ring producer/consumer endpoint, or IRQ
 * endpoint. data[1] is the handle and data[2]:data[3] the absolute
 * monotonic deadline (::ASTRA_DEADLINE_FOREVER to wait without one).
 * Returns an object-specific detail in data[1] -- nothing for an ordinary
 * sync object, the ASTRA_IRQ_EVENT_* flags for an IRQ endpoint, or the exit
 * reason for a dead thread or process.
 */
#define ASTRA_SYSCALL_WAIT_ONE         8
/**
 * Signals a sync object. data[1] is the handle (::ASTRA_RIGHT_SIGNAL) and
 * data[2] the signal count (an event ignores it; a semaphore adds it, up
 * to its maximum). Returns the number of waiters woken in data[1].
 */
#define ASTRA_SYSCALL_SIGNAL           9
/** Clears a manual-reset event back to unsignaled. data[1] is the handle. */
#define ASTRA_SYSCALL_EVENT_RESET      10
/**
 * Cancels another thread's blocked wait with ::ASTRA_SYSCALL_CANCELLED.
 * data[1] is a thread handle carrying ASTRA_RIGHT_ADMINISTER.
 */
#define ASTRA_SYSCALL_CANCEL_WAIT      11
/**
 * Creates a new thread in the calling process. data[1] is the entry point
 * (must lie within the process's code), data[2] the initial stack pointer,
 * data[3] the priority (bounded by the process's priority ceiling), and
 * data[4] the thread rights to install on the returned handle. Returns the
 * handle in data[1] and the new thread id in data[2].
 */
#define ASTRA_SYSCALL_THREAD_CREATE    12
/** Exits the calling thread. data[1] is the exit status. Does not return. */
#define ASTRA_SYSCALL_THREAD_EXIT      13
/**
 * Waits for any of up to ::ASTRA_WAIT_MULTIPLE_MAX waitable handles -- see
 * ::ASTRA_SYSCALL_WAIT_ONE for the object types. data[1] is the address of
 * a `KernelHandle` array, data[2] its element count, and data[3]:data[4]
 * the absolute monotonic deadline. Returns the index of the ready handle
 * in data[1] (::ASTRA_WAIT_INDEX_NONE if none became ready before the
 * deadline) and that handle's detail word in data[2].
 */
#define ASTRA_SYSCALL_WAIT_MULTIPLE    14
/**
 * Creates a timer, a sync object armed by ::ASTRA_SYSCALL_TIMER_SET.
 * data[1] is the rights to install on the returned handle, placed in
 * data[1].
 */
#define ASTRA_SYSCALL_TIMER_CREATE     15
/**
 * Arms a timer. data[1] is the handle (ASTRA_RIGHT_ADMINISTER) and
 * data[2]:data[3] the absolute monotonic deadline it fires at. Returns the
 * number of waiters woken in data[1].
 */
#define ASTRA_SYSCALL_TIMER_SET        16
/**
 * Disarms a timer, waking its waiters with ::ASTRA_SYSCALL_CANCELLED.
 * data[1] is the handle (ASTRA_RIGHT_ADMINISTER). Returns the number of
 * waiters woken in data[1].
 */
#define ASTRA_SYSCALL_TIMER_CANCEL     17
/**
 * Creates a port, a rendezvous with one receive endpoint and a cloneable
 * send endpoint. data[1] and data[2] are implementation-defined creation
 * parameters consumed by the port subsystem. Returns the receive handle in
 * data[1] and the send handle in data[2].
 */
#define ASTRA_SYSCALL_PORT_CREATE      18
/**
 * Sends a message, or replies to an ::ASTRA_SYSCALL_PORT_CALL, without
 * blocking. data[1] is a port-send or reply handle; data[2] the message
 * address and data[3] its size; data[4] an attached-handle array address
 * and data[5] its count. WOULD_BLOCK if the port has no room.
 */
#define ASTRA_SYSCALL_PORT_SEND_TRY    19
/**
 * Receives a message without blocking. data[1] is a port-receive handle;
 * data[2] the output message buffer and data[3] its capacity; data[4] an
 * output handle-array buffer and data[5] its capacity. Returns the
 * required message size in data[1] and required handle count in data[2]
 * (so a too-small buffer can be resized and retried) and, once received,
 * the sender's process id in data[3]. WOULD_BLOCK if nothing is queued.
 */
#define ASTRA_SYSCALL_PORT_RECEIVE_TRY 20
/**
 * Duplicates a handle. data[1] is the source handle and data[2] the
 * rights for the copy, which must be a subset of the source's. Returns the
 * new handle in data[1].
 */
#define ASTRA_SYSCALL_HANDLE_DUPLICATE 21
/**
 * Creates a shared or reserved area. data[1] is its byte size, data[2] the
 * rights to install on the returned handle, and data[3] the
 * ASTRA_AREA_CREATE_* flags. Returns the handle in data[1].
 */
#define ASTRA_SYSCALL_AREA_CREATE      22
/**
 * Maps an area into the calling process. data[1] is the area handle,
 * data[2] the ASTRA_AREA_MAP_* permissions (must include READ and must be
 * a subset of the handle's rights). Returns the mapped base address in
 * data[1] and its byte size in data[2].
 */
#define ASTRA_SYSCALL_AREA_MAP         23
/** Unmaps a previously mapped area. data[1] is the virtual base it was mapped at. */
#define ASTRA_SYSCALL_AREA_UNMAP       24
/**
 * Creates a bulk ring: a fixed-element-size SPSC queue laid out in an area
 * the caller already holds with ASTRA_RIGHT_ADMINISTER. data[1] is the
 * area handle; data[2] the ring's byte offset into it; data[3] the fixed
 * element size; data[4] the power-of-two element capacity; data[5] the
 * ASTRA_BULK_RING_CREATE_* flags. Returns a producer handle in data[1] and
 * a consumer handle in data[2].
 */
#define ASTRA_SYSCALL_RING_CREATE      25
/**
 * Publishes a bulk-ring position update and wakes waiters on the other
 * endpoint. data[1] is a producer or consumer handle; data[2] and data[3]
 * are the new producer and consumer positions (only the caller's own side
 * may move); data[4] is the `KERNEL_RING_ENDPOINT_PRODUCER` /
 * `_CONSUMER` value matching the handle's own endpoint. Returns the
 * resulting producer position in data[1] and consumer position in data[2].
 */
#define ASTRA_SYSCALL_RING_NOTIFY      26
/**
 * Reads the oldest undelivered record from an IRQ endpoint without
 * blocking. data[1] is the endpoint handle (ASTRA_RIGHT_READ) and data[2]
 * a 4-byte-aligned address to receive one ::AstraIrqRecord. Returns the
 * endpoint's current ASTRA_IRQ_EVENT_* flags in data[1] regardless of
 * outcome; WOULD_BLOCK if nothing is pending.
 */
#define ASTRA_SYSCALL_IRQ_READ         27
/**
 * Acknowledges the record ::ASTRA_SYSCALL_IRQ_READ just returned, letting
 * the controller accept the next interrupt. data[1] is the endpoint
 * handle (ASTRA_RIGHT_SIGNAL) and data[2] that record's `sequence`, which
 * must still be the oldest pending one.
 */
#define ASTRA_SYSCALL_IRQ_ACK          28
/** Arms an IRQ endpoint to resume delivering interrupts. data[1] is the handle (ASTRA_RIGHT_ADMINISTER). */
#define ASTRA_SYSCALL_IRQ_ARM          29
/** Masks an IRQ endpoint, suppressing further delivery. data[1] is the handle (ASTRA_RIGHT_ADMINISTER). */
#define ASTRA_SYSCALL_IRQ_MASK         30
/**
 * Clears a quarantined IRQ endpoint's sticky ASTRA_IRQ_ENDPOINT_EVENT_*
 * flags and returns it to service. data[1] is the handle
 * (ASTRA_RIGHT_ADMINISTER).
 */
#define ASTRA_SYSCALL_IRQ_RECOVER      31
/**
 * Revokes an IRQ endpoint, waking every waiter with ::ASTRA_SYSCALL_PEER_DEAD.
 * data[1] is the handle (ASTRA_RIGHT_ADMINISTER). Returns the number of
 * waiters woken in data[1].
 */
#define ASTRA_SYSCALL_IRQ_REVOKE       32
/**
 * Reads a device's identity and lease state. data[1] is the device lease
 * handle (ASTRA_RIGHT_READ) and data[2] a 4-byte-aligned address to
 * receive one ::AstraDeviceInfo.
 */
#define ASTRA_SYSCALL_DEVICE_QUERY     33
/**
 * Resets a device, first ending any in-flight completions the device will
 * never send (block requests, a display DMA transfer) so the reset cannot
 * leave the service waiting forever. data[1] is the device lease handle
 * (ASTRA_RIGHT_ADMINISTER).
 */
#define ASTRA_SYSCALL_DEVICE_RESET     34
/**
 * Revokes a device lease, aborting any in-flight device DMA first. data[1]
 * is the device lease handle (ASTRA_RIGHT_ADMINISTER).
 */
#define ASTRA_SYSCALL_DEVICE_REVOKE    35
/**
 * Reads queued input events without blocking. data[1] is the input0
 * device lease handle (ASTRA_RIGHT_READ); data[2] a 4-byte-aligned output
 * event-array address; data[3] its capacity (clamped to
 * ASTRA_INPUT_FIFO_CAPACITY). Returns the number of events copied in
 * data[1] and the ASTRA_INPUT_READ_* flags in data[2]. WOULD_BLOCK if
 * nothing was read and no overflow is pending.
 */
#define ASTRA_SYSCALL_INPUT_READ_TRY   36
/**
 * Reads a process's accounting snapshot. data[1] is a process handle
 * carrying ASTRA_RIGHT_QUERY and data[2] the output address for one
 * `AstraProcessInfo`.
 */
#define ASTRA_SYSCALL_PROCESS_INFO     37
/**
 * Allocates a transfer (DMA) buffer: kernel pages, physically contiguous,
 * charged to the caller and mapped into it read/write. data[1] is the
 * requested byte size and data[2] a 4-byte-aligned address to receive one
 * ::AstraDmaBufferInfo. Also mirrors that struct's `handle`,
 * `virtual_base` and `byte_size` into data[1], data[2] and data[3].
 */
#define ASTRA_SYSCALL_DMA_CREATE       38
/**
 * Reads block-device geometry. data[1] is the block0 device lease handle
 * (ASTRA_RIGHT_READ, lease active) and data[2] the output address for an
 * `AstraBlockLeaseInfo`.
 */
#define ASTRA_SYSCALL_BLOCK_QUERY      39
/**
 * Submits a block I/O request. data[1] is the block0 device lease handle
 * (ASTRA_RIGHT_TRANSFER, lease active) and data[2] the address of an
 * `AstraBlockRequest` naming a DMA buffer the caller holds. Returns a
 * block-request handle in data[1], later passed to
 * ::ASTRA_SYSCALL_BLOCK_COLLECT.
 */
#define ASTRA_SYSCALL_BLOCK_SUBMIT     40
/**
 * Drains completed block requests and collects one. data[1] is the block0
 * device lease handle; data[2] the output address for an
 * `AstraBlockCompletion`; data[3] the request handle from
 * ::ASTRA_SYSCALL_BLOCK_SUBMIT to collect. Returns, in data[1], the number
 * of completions drained by this call (independent of which one data[3]
 * named). WOULD_BLOCK if that request has not completed yet.
 */
#define ASTRA_SYSCALL_BLOCK_COLLECT    41
/**
 * Reads the text console's geometry. data[1] is the display0 device
 * lease handle (ASTRA_RIGHT_READ). Returns the column count in data[1]
 * and row count in data[2].
 */
#define ASTRA_SYSCALL_CONSOLE_INFO     42
/**
 * Writes character cells to the text console. data[1] is the display0
 * device lease handle (ASTRA_RIGHT_TRANSFER); data[2] the starting cell
 * index; data[3] the address of the source cell bytes; data[4] the cell
 * count.
 */
#define ASTRA_SYSCALL_CONSOLE_WRITE    43
/**
 * Appends one event to the kernel's trace stream. Universal: no handle
 * and no capability gate it, because an account of the machine with holes
 * wherever something went wrong would not be an account -- reading the
 * stream back (::ASTRA_SYSCALL_TRACE_READ) is the half that needs one.
 * data[1] is the message id (nonzero); data[2] the ASTRA_EVENT_* flags;
 * data[3] the payload address (zero iff data[4] is zero); data[4] the
 * payload length, 0..ASTRA_EVENT_ARGUMENT_MAX. A ring that cannot accept
 * the record drops it and counts the drop; the call still succeeds, since
 * a program must never fail because the machine could not write down what
 * it said.
 */
#define ASTRA_SYSCALL_LOG_WRITE        44
/*
 * The calling thread's activity: what it is currently doing, for correlation.
 * data[1] of zero begins a fresh one; CURRENT reads it, NONE clears it, and
 * anything else adopts that value. The call returns the current activity in
 * data[1] and the previous activity in data[2].
 *
 * The kernel holds it, per thread, so that every event is stamped without any
 * call site passing one. A machine where correlation is a parameter is a
 * machine where the events that matter are the ones that forgot it.
 */
/**
 * Sets or queries the calling thread's activity id, the correlation id
 * every trace event it emits is stamped with.
 *
 * data[1] of zero begins a fresh one; ::ASTRA_ACTIVITY_CURRENT reads it
 * without changing it; ::ASTRA_ACTIVITY_NONE clears it; anything else
 * adopts that value as the new activity. The call returns the current
 * activity in data[1] and the previous activity in data[2].
 *
 * The kernel holds it per thread so that every event is stamped without
 * any call site passing one. A machine where correlation is a parameter
 * is a machine where the events that matter are the ones that forgot it.
 */
#define ASTRA_SYSCALL_ACTIVITY         45

/** Reads a thread's activity via ::ASTRA_SYSCALL_ACTIVITY without allocating a new global id. */
#define ASTRA_ACTIVITY_CURRENT 0xfffffffeu
/** Clears a thread's activity via ::ASTRA_SYSCALL_ACTIVITY without allocating a new global id. */
#define ASTRA_ACTIVITY_NONE 0xffffffffu

/*
 * The other half of the reversal: reading the stream back.
 *
 * data[1] is a process handle carrying ASTRA_RIGHT_DEBUG that names the caller;
 * data[2] is the cursor -- the sequence already seen, zero for everything the
 * ring still holds; data[3] is where to put them and data[4] is how many
 * AstraEventDrained records will fit. The kernel copies in page-sized chunks;
 * the trace contents and caller's buffer are the only bounds.
 *
 * It returns how many were copied in data[1], the cursor to pass next time in
 * data[2], and in data[3] how many records the caller will never see because
 * the ring displaced them first. That last one is the point: a log that
 * quietly loses records is worse than one that admits it, because everything
 * read after the gap is an assumption.
 *
 * The handle must name the caller. Reading the machine's whole stream is the
 * observer's own authority, and borrowing a debug handle over some third
 * process to obtain it would be laundering authority through a bystander. Only
 * a build with a diagnostic surface puts DEBUG on a process's own handle, which
 * is what makes this a capability rather than a syscall anyone can reach.
 */
/**
 * Reads the kernel's trace stream back, the half of tracing that is
 * authority rather than universal: it is every process's events at once,
 * which is where a secret leaks.
 *
 * data[1] is a process handle carrying ASTRA_RIGHT_DEBUG that must name
 * the caller itself -- borrowing a debug handle over some other process
 * to reach the whole machine's stream would be laundering authority
 * through a bystander, so only a build with a diagnostic surface puts
 * DEBUG on a process's own handle. data[2] is the cursor (the sequence
 * already seen, zero for everything the ring still holds); data[3] is the
 * output buffer address and data[4] how many `AstraEventDrained` records
 * it holds. The kernel copies in page-sized chunks; the trace contents
 * and caller's buffer are the only bounds.
 *
 * Returns how many records were copied in data[1], the cursor to pass
 * next time in data[2], and in data[3] how many records the caller will
 * never see because the ring displaced them first -- a log that quietly
 * loses records is worse than one that admits it, because everything read
 * after the gap is an assumption.
 */
#define ASTRA_SYSCALL_TRACE_READ       46

/*
 * Launching a program.
 *
 * data[1] is the ELF image in the caller's own memory and data[2] its length;
 * data[3] is an AstraLaunchGrant array and data[4] how many; data[5] is an
 * AstraLaunchArguments block, or zero for none. The record may point at one
 * bounded packed environment block; the kernel copies it before launching and
 * publishes a conventional null-terminated `environ` vector. It returns a
 * handle to the new process in data[1] -- carrying QUERY, WAIT, SIGNAL,
 * TERMINATE, TRANSFER and ADMINISTER for priority, and never DEBUG, because
 * having launched something is not authority to inspect its event stream --
 * and the new process id in data[2].
 *
 * Every grant names a handle the caller already holds, with rights that are a
 * subset of the caller's. A handle it does not hold, or rights wider than its
 * own, fails the whole call rather than being dropped: a child whose namespace
 * is quietly smaller than the line that launched it says would fail later, as a
 * path that does not resolve for a reason nobody can see.
 *
 * There is no fork. Nothing is inherited implicitly, so what a program may
 * touch is what somebody wrote down.
 *
 * AstraLaunchArguments.flags may request ASTRA_LAUNCH_FLAG_ESSENTIAL only
 * when the caller is the firmware-selected initial supervisor. Essential
 * processes retain access to protected memory headroom and the complete
 * priority band. Every ordinary child is capped at NORMAL priority.
 */
/**
 * Launches a program as a brand-new process.
 *
 * data[1] is the ELF image in the caller's own memory and data[2] its
 * length; data[3] is an `AstraLaunchGrant` array and data[4] how many;
 * data[5] is an `AstraLaunchArguments` block, or zero for none. The
 * record may point at one bounded packed environment block; the kernel
 * copies it before launching and publishes a conventional
 * null-terminated `environ` vector. Returns a handle to the new process
 * in data[1] -- carrying QUERY, WAIT, SIGNAL, TERMINATE, TRANSFER and
 * ADMINISTER for priority, and never DEBUG, because having launched
 * something is not authority to inspect its event stream -- and the new
 * process id in data[2].
 *
 * Every grant names a handle the caller already holds, with rights that
 * are a subset of the caller's. A handle it does not hold, or rights
 * wider than its own, fails the whole call rather than being dropped: a
 * child whose namespace is quietly smaller than the line that launched it
 * says would fail later, as a path that does not resolve for a reason
 * nobody can see.
 *
 * There is no fork. Nothing is inherited implicitly, so what a program
 * may touch is what somebody wrote down.
 *
 * `AstraLaunchArguments.flags` may request ASTRA_LAUNCH_FLAG_ESSENTIAL
 * only when the caller is the firmware-selected initial supervisor.
 * Essential processes retain access to protected memory headroom and the
 * complete priority band. Every ordinary child is capped at NORMAL
 * priority.
 */
#define ASTRA_SYSCALL_PROCESS_CREATE   48
/**
 * Reads one interrupt endpoint slot's diagnostic state. Only present on
 * a build with the kernel's diagnostic surface enabled, gated the same
 * way as ::ASTRA_SYSCALL_TRACE_READ and for the same reason: it is every
 * process's devices at once. data[1] is a process handle carrying
 * ASTRA_RIGHT_DEBUG that must name the caller itself; data[2] the slot
 * index; data[3] the output address for one ::AstraIrqEndpointInfo.
 * Always returns the total number of endpoint slots in data[1], so a
 * caller can size its loop without a compiled-in constant.
 */
#define ASTRA_SYSCALL_IRQ_ENDPOINT_INFO 49
/**
 * Moves the text-console cursor. data[1] is the display0 device lease
 * handle (ASTRA_RIGHT_TRANSFER); data[2] the row; data[3] the column
 * (may equal the column count, meaning a pending line wrap); data[4]
 * whether the cursor is visible (0 or 1).
 */
#define ASTRA_SYSCALL_CONSOLE_CURSOR   50
/**
 * Submits a display frame or render batch. data[1] is the display0
 * device lease handle, which must carry both ASTRA_DISPLAY_CAP_SOLID_FRAME
 * and ASTRA_DISPLAY_CAP_FENCED_PRESENT; data[2] the address of an
 * `AstraDisplayFrameRequest` describing the operation (solid color, RGB565
 * blit, render batch, cursor image, or surface read-back) and its source
 * DMA buffer. The full request layout lives with the display subsystem,
 * not in this header.
 */
#define ASTRA_SYSCALL_DISPLAY_SUBMIT   51
/**
 * Collects a previously submitted display frame's completion. Same lease
 * and capability requirements as ::ASTRA_SYSCALL_DISPLAY_SUBMIT; data[2]
 * is the address of the completion record to fill in.
 */
#define ASTRA_SYSCALL_DISPLAY_COLLECT  52
/* 53 retired: whole-image shared-library mapping was replaced by 89-91. */
/**
 * Hands the committed pages of a reserved area back, keeping the reservation.
 * data[1] is the address and data[2] the length; it answers with the number of
 * pages actually released in data[1].
 *
 * Only whole pages inside the range go, so an allocator may pass the block it
 * just freed without first working out which pages it has entirely to itself.
 */
#define ASTRA_SYSCALL_AREA_DECOMMIT    54

/*
 * The date: nanoseconds since the Unix epoch, high half in data[1] and low in
 * data[2], the same shape ASTRA_SYSCALL_CLOCK_MONOTONIC answers in.
 *
 * The two clocks are different things and both are needed. Monotonic never
 * goes backwards and is what a timeout is measured against; this one is what
 * a file's timestamp and a person's question are about, and it moves whenever
 * the machine's clock is corrected.
 *
 * A machine that does not know the date answers ASTRA_SYSCALL_UNSUPPORTED
 * rather than zero, because zero is a real instant -- and a program that
 * cannot tell "midnight in 1970" from "no idea" writes the first one into a
 * file and calls it a timestamp.
 */
/**
 * Reads the wall-clock date: nanoseconds since the Unix epoch, high half
 * in data[1] and low half in data[2], the same shape
 * ::ASTRA_SYSCALL_CLOCK_MONOTONIC answers in. No argument. Also returns
 * the current UTC offset in seconds (signed) in data[3] and a zone id in
 * data[4], read atomically with the instant so a caller cannot straddle a
 * summer-time change and render an hour that never happened.
 *
 * The two clocks are different things and both are needed: monotonic
 * never goes backwards and is what a timeout is measured against; this
 * one is what a file's timestamp and a person's question are about, and
 * it moves whenever the machine's clock is corrected.
 *
 * A machine that does not know the date answers ::ASTRA_SYSCALL_UNSUPPORTED
 * rather than zero, because zero is a real instant -- and a program that
 * cannot tell "midnight in 1970" from "no idea" writes the first one into
 * a file and calls it a timestamp.
 */
#define ASTRA_SYSCALL_CLOCK_REALTIME   55
/**
 * Maps an exact library identity already resident in the kernel cache.
 * data[1] is the address of an `AstraLibraryReference` naming the exact
 * name, version and ABI major to match. Returns the mapped base in
 * data[1], its span in data[2] and a load handle in data[3].
 * ::ASTRA_SYSCALL_WOULD_BLOCK if no cached entry matches exactly.
 */
#define ASTRA_SYSCALL_LIBRARY_ATTACH   56
/*
 * Changes the scheduling priority of a process and all its live threads.
 * data[1] is a process handle carrying ASTRA_RIGHT_ADMINISTER; data[2] is a
 * user priority from 1 through 23. The previous priority is returned in
 * data[1]. New threads inherit the new process default.
 */
/**
 * Changes the scheduling priority of a process and all its live threads.
 * data[1] is a process handle carrying ASTRA_RIGHT_ADMINISTER; data[2] is
 * a user priority from 1 through 23, capped by the process's priority
 * ceiling. The previous priority is returned in data[1]. New threads
 * inherit the new process default.
 */
#define ASTRA_SYSCALL_PROCESS_PRIORITY 57
/*
 * COW clone. The child is atomically created suspended so its parent can
 * publish required bookkeeping before resuming it. D1=child handle and
 * D2=child id in the parent, both zero in the child.
 */
/**
 * Clones the calling process copy-on-write. No argument. The child is
 * atomically created suspended so its parent can publish required
 * bookkeeping before resuming it. Returns the child handle in data[1]
 * and the child id in data[2] in the parent; both are zero in the child,
 * which is how the two tell themselves apart after the one call returns
 * twice.
 */
#define ASTRA_SYSCALL_PROCESS_CLONE    58
/* Kernel-serialized transfers for clone-safe byte-mode bulk rings. */
/**
 * Reads from a bulk-ring consumer endpoint through a kernel-serialized
 * copy, for rings created with ::ASTRA_BULK_RING_CREATE_KERNEL_COPY.
 * data[1] is the consumer handle; data[2] the destination address;
 * data[3] the byte capacity. Returns the number of bytes copied in
 * data[1].
 */
#define ASTRA_SYSCALL_RING_READ_TRY    59
/**
 * Writes to a bulk-ring producer endpoint through a kernel-serialized
 * copy, for rings created with ::ASTRA_BULK_RING_CREATE_KERNEL_COPY.
 * data[1] is the producer handle; data[2] the source address; data[3]
 * the byte count (nonzero); data[4] the ASTRA_BULK_RING_WRITE_* flags.
 * Returns the number of bytes actually written in data[1].
 */
#define ASTRA_SYSCALL_RING_WRITE_TRY   60
/**
 * Reserves clone-private anonymous address space for the calling process.
 * data[1] is the requested size and data[2] is ASTRA_VM_PRIVATE_*; the result
 * is a root-slot-aligned base in data[1] and the rounded span in data[2]. No
 * RAM is committed until first touch, and every committed page is charged to
 * the process through the ordinary frame quota.
 */
#define ASTRA_SYSCALL_VM_PRIVATE_RESERVE  61
/**
 * Decommits the whole pages inside a private reservation range. data[1]
 * is the address and data[2] the length. Returns the number of pages
 * actually released in data[1]. Refused with ::ASTRA_SYSCALL_ACCESS_DENIED
 * over a live dynamic-linker TLS template range.
 */
#define ASTRA_SYSCALL_VM_PRIVATE_DECOMMIT 62
/**
 * Registers the POSIX signal trampoline, its stack, and the calling
 * thread's blocked-signal mask. data[1] is the trampoline address
 * (must already be mapped); data[2] the top of the signal stack (must be
 * 4-byte aligned, mapped read/write); data[3] the new blocked-signal mask
 * (SIGKILL and SIGSTOP are never blocked). Returns the process's pending
 * signals in data[1] and the thread's previous blocked mask in data[2].
 */
#define ASTRA_SYSCALL_SIGNAL_CONFIGURE 63
/**
 * Arms, disarms or queries the process's ITIMER_REAL-style interval
 * timer. data[1]:data[2] is the relative delay in nanoseconds and
 * data[3]:data[4] the relative repeat interval; both 0xffffffff_ffffffff
 * together means query without changing anything. Returns the previously
 * remaining delay in data[1]:data[2] and the previous interval in
 * data[3]:data[4].
 */
#define ASTRA_SYSCALL_INTERVAL_TIMER 64
/**
 * Restores the context saved when the active POSIX signal upcall was
 * delivered, resuming the interrupted thread. No argument. Does not
 * return. ::ASTRA_SYSCALL_INVALID_ARGUMENT if no signal context is active.
 */
#define ASTRA_SYSCALL_SIGNAL_RETURN 65
/*
 * Atomically replaces the calling process image with a load built by
 * PROCESS_LOAD_REPLACE; success never returns. D1 is the load handle and D2
 * the AstraExecRequest. Exec is the launch transaction aimed at the caller:
 * the same streamed headers and segment pages, a different commit.
 */
/**
 * Atomically replaces the calling process's image with a load built by
 * ::ASTRA_SYSCALL_PROCESS_LOAD_REPLACE; success never returns. data[1] is
 * the load handle and data[2] the address of an `AstraExecRequest`
 * carrying the new program's arguments. Exec is the launch transaction
 * aimed at the caller itself: the same streamed headers and segment
 * pages ::ASTRA_SYSCALL_PROCESS_CREATE uses, committed into the running
 * process instead of a new one.
 */
#define ASTRA_SYSCALL_PROCESS_EXEC 66
/*
 * Transactional executable loading from a userspace file reader.
 *
 * BEGIN consumes only the fixed ELF header and the complete file size, then
 * returns a load handle plus the exact next file range in data[2:3]. WRITE
 * accepts only that range and returns the next one. Once all program headers
 * are accepted, CREATE snapshots grants and arguments and builds a private,
 * non-runnable child. Further WRITE calls fill its validated segment pages.
 * COMMIT publishes the initial thread and returns the child handle and id.
 *
 * Closing the load handle at any point aborts the transaction and releases
 * every partial process resource. The kernel never opens a file or interprets
 * a VFS protocol; userspace supplies only the bytes the kernel requests.
 */
/**
 * Begins transactional executable loading from a userspace file reader.
 *
 * data[1] is the address of the fixed ELF header already read into the
 * caller's memory and data[2] the complete file size. Returns a load
 * handle in data[1] plus the exact next file range the kernel wants in
 * data[2]:data[3].
 *
 * BEGIN consumes only the fixed header and the file size. WRITE
 * (::ASTRA_SYSCALL_PROCESS_LOAD_WRITE) accepts only the range just
 * returned and returns the next one. Once every program header is
 * accepted, CREATE (::ASTRA_SYSCALL_PROCESS_LOAD_CREATE) snapshots grants
 * and arguments and builds a private, non-runnable child; further WRITE
 * calls then fill its validated segment pages. COMMIT
 * (::ASTRA_SYSCALL_PROCESS_LOAD_COMMIT) publishes the initial thread and
 * returns the child handle and id.
 *
 * Closing the load handle at any point aborts the transaction and
 * releases every partial process resource. The kernel never opens a file
 * or interprets a VFS protocol; userspace supplies only the bytes the
 * kernel requests.
 */
#define ASTRA_SYSCALL_PROCESS_LOAD_BEGIN  67
/**
 * Supplies the next file range ::ASTRA_SYSCALL_PROCESS_LOAD_BEGIN (or the
 * previous WRITE) asked for. data[1] is the load handle; data[2] the file
 * offset (must equal the range just returned); data[3] the address of
 * those bytes in the caller's memory; data[4] their length. Returns the
 * next file offset in data[1] and next length in data[2], and once the
 * program headers are known, the source of that next range
 * (ASTRA_PROCESS_LOAD_SOURCE_*) in data[3].
 */
#define ASTRA_SYSCALL_PROCESS_LOAD_WRITE  68
/**
 * Snapshots launch grants and arguments and builds a private, non-runnable
 * child from an executable load once its program headers are accepted.
 * data[1] is the load handle; data[2] the address of an
 * `AstraLaunchGrant` array; data[3] how many; data[4] the address of an
 * `AstraLaunchArguments` block, or zero for none. Returns the next
 * segment file range in data[1]:data[2] and its source
 * (ASTRA_PROCESS_LOAD_SOURCE_*) in data[3], for the WRITE calls that
 * follow.
 */
#define ASTRA_SYSCALL_PROCESS_LOAD_CREATE 69
/**
 * Publishes the initial thread of a fully written executable load.
 * data[1] is the load handle. Returns the new child's handle in data[1]
 * and its process id in data[2].
 */
#define ASTRA_SYSCALL_PROCESS_LOAD_COMMIT 70
/* Protected network broker transport; application protocols stay userspace. */
/**
 * Reads the network device's lease state and transport limits. data[1]
 * is the network0 device lease handle (ASTRA_RIGHT_QUERY, lease active);
 * data[2] the output address for an `AstraNetworkLeaseInfo`. Application
 * protocols stay in userspace; this is the protected broker transport
 * underneath them.
 */
#define ASTRA_SYSCALL_NETWORK_QUERY        71
/**
 * Executes a batch of network host commands. data[1] is the network0
 * device lease handle (ASTRA_RIGHT_TRANSFER, lease active); data[2] the
 * address of an `AstraNetworkTransportRequest` naming a DMA buffer the
 * caller holds. Returns the number of commands executed in data[1].
 */
#define ASTRA_SYSCALL_NETWORK_EXECUTE      72
/**
 * Sets the machine's wall clock. data[1] is the clock0 device lease
 * handle (ASTRA_RIGHT_ADMINISTER); data[2]:data[3] the new instant as
 * Unix epoch nanoseconds, the same split ::ASTRA_SYSCALL_CLOCK_REALTIME
 * reads.
 */
#define ASTRA_SYSCALL_CLOCK_SET            73
/*
 * Process-private atomic address waits. WAIT verifies the aligned word at D1
 * equals D2 and sleeps until the absolute D3:D4 deadline. WAKE wakes at most
 * D2 priority-ordered waiters for D1. The physical thread pool is the only
 * bound on concurrent wait addresses.
 */
/**
 * Process-private atomic address wait. Verifies the 4-byte-aligned word
 * at data[1] still equals data[2] and, if so, sleeps the calling thread
 * until the absolute monotonic deadline in data[3]:data[4].
 * ::ASTRA_SYSCALL_WOULD_BLOCK if the word had already changed. The
 * physical thread pool is the only bound on concurrently waited addresses.
 */
#define ASTRA_SYSCALL_FUTEX_WAIT            74
/**
 * Wakes threads waiting on a futex address. data[1] is the address;
 * data[2] the maximum number of priority-ordered waiters to wake. Returns
 * the number actually woken in data[1].
 */
#define ASTRA_SYSCALL_FUTEX_WAKE            75
/* Capability-checked batched DMA transport to an attached host accelerator. */
/**
 * Reads the attached host accelerator's lease state and transport
 * limits. data[1] is the host0 device lease handle (ASTRA_RIGHT_QUERY);
 * data[2] the output address for an `AstraHostLeaseInfo`.
 */
#define ASTRA_SYSCALL_HOST_QUERY             76
/**
 * Executes a batch of host accelerator commands. data[1] is the host0
 * device lease handle (ASTRA_RIGHT_EXECUTE, lease active); data[2] the
 * address of an `AstraHostTransportRequest` naming a DMA buffer the
 * caller holds. Returns the number of commands executed in data[1].
 */
#define ASTRA_SYSCALL_HOST_EXECUTE           77
/**
 * Opens a host accelerator channel for the calling thread. data[1] is
 * the host0 device lease handle (ASTRA_RIGHT_EXECUTE); data[2] the
 * address of an `AstraHostChannelOpen` describing the channel to open.
 */
#define ASTRA_SYSCALL_HOST_CHANNEL_OPEN      78
/**
 * Closes the calling thread's open host accelerator channel, waking any
 * waiter with ::ASTRA_SYSCALL_PEER_DEAD. data[1] is the host0 device
 * lease handle; a lease that is no longer active still allows the close.
 */
#define ASTRA_SYSCALL_HOST_CHANNEL_CLOSE     79
/* D2=consumer position, D3:D4=deadline; the current thread owns the channel. */
/**
 * Blocks until the calling thread's open host accelerator channel reaches
 * or passes a consumer position, or a deadline. data[2] is the consumer
 * position to wait for and data[3]:data[4] the absolute monotonic
 * deadline. The calling thread must own the channel opened with
 * ::ASTRA_SYSCALL_HOST_CHANNEL_OPEN.
 */
#define ASTRA_SYSCALL_HOST_CHANNEL_WAIT      80
/*
 * Delivers one process notification through the registered user trampoline.
 * D1 names a process handle carrying SIGNAL; D2 is a bit number from 1..31.
 * Standard notifications coalesce while pending. Policy such as POSIX process
 * groups stays in its userspace service; Axiom only enforces the capability
 * and arranges the upcall.
 */
/**
 * Delivers one process notification through the registered user
 * trampoline. data[1] names a process handle carrying
 * ASTRA_RIGHT_SIGNAL; data[2] is a signal bit number from 1 through 31.
 * Standard notifications coalesce while pending. Policy such as POSIX
 * process groups stays in its userspace service; the kernel only
 * enforces the capability and arranges the upcall.
 */
#define ASTRA_SYSCALL_PROCESS_SIGNAL          81
/*
 * Unconditionally retires the named process. D1 carries TERMINATE and D2 is
 * the nonzero 1..31 reason preserved for a waiter. This is the native
 * mechanism POSIX uses for SIGKILL; it cannot be masked or redirected through
 * a user trampoline.
 */
/**
 * Unconditionally retires the named process. data[1] is a process handle
 * carrying ASTRA_RIGHT_TERMINATE; data[2] is the nonzero 1..31 reason
 * preserved for a waiter. This is the native mechanism POSIX uses for
 * SIGKILL; it cannot be masked or redirected through a user trampoline.
 */
#define ASTRA_SYSCALL_PROCESS_TERMINATE       82
/* Stops/continues all live threads without disturbing blocked waits. */
/**
 * Suspends every live thread of a process, without disturbing any wait
 * already blocked. data[1] is a process handle carrying
 * ASTRA_RIGHT_SIGNAL. A no-op if already suspended.
 */
#define ASTRA_SYSCALL_PROCESS_SUSPEND         83
/**
 * Resumes a process suspended with ::ASTRA_SYSCALL_PROCESS_SUSPEND.
 * data[1] is a process handle carrying ASTRA_RIGHT_SIGNAL. A no-op if not
 * suspended.
 */
#define ASTRA_SYSCALL_PROCESS_RESUME          84
/*
 * Interruptible sleep on the monotonic clock. D1:D2 is an absolute deadline.
 * With ASTRA_THREAD_SLEEP_REPLACE_SIGNAL_MASK, D4 atomically replaces the
 * signal mask before the pending-signal check and D1 returns the old mask.
 */
/**
 * Interruptible sleep on the monotonic clock. data[1]:data[2] is a
 * deadline, absolute unless ::ASTRA_THREAD_SLEEP_RELATIVE is set in
 * data[3], and data[3] carries the ASTRA_THREAD_SLEEP_* flags. With
 * ::ASTRA_THREAD_SLEEP_REPLACE_SIGNAL_MASK set, data[4] atomically
 * replaces the calling thread's blocked-signal mask before the
 * pending-signal check and data[1] returns the old mask.
 */
#define ASTRA_SYSCALL_THREAD_SLEEP             85
/** ::ASTRA_SYSCALL_THREAD_SLEEP flag: data[4] replaces the signal mask atomically with the sleep. */
#define ASTRA_THREAD_SLEEP_REPLACE_SIGNAL_MASK (1u << 0)
/** ::ASTRA_SYSCALL_THREAD_SLEEP flag: the deadline in data[1]:data[2] is relative, not absolute. */
#define ASTRA_THREAD_SLEEP_RELATIVE            (1u << 1)
/** Every valid ::ASTRA_SYSCALL_THREAD_SLEEP flag. */
#define ASTRA_THREAD_SLEEP_FLAG_MASK \
    (ASTRA_THREAD_SLEEP_REPLACE_SIGNAL_MASK | ASTRA_THREAD_SLEEP_RELATIVE)
/*
 * Complete live-process snapshot for the initial supervisor's PROC: service.
 * D1 is its own PROCESS handle, D2 an AstraProcSnapshot array, and D3 its
 * record capacity. On success D1 returns the live record count. When the
 * capacity is too small, nothing is copied and D1 returns the required count.
 * This authority is deliberately not conferred by an ordinary process handle.
 */
/**
 * Complete live-process snapshot for the initial supervisor's PROC:
 * service. data[1] must be a QUERY handle the caller holds on itself, and
 * the caller must be the initial supervisor process -- this authority is
 * deliberately not conferred by an ordinary process handle. data[2] is an
 * `AstraProcSnapshot` output array address and data[3] its record
 * capacity. On success, returns the live record count in data[1]. When
 * the capacity is too small, nothing is copied and
 * ::ASTRA_SYSCALL_BUFFER_TOO_SMALL is returned with the required count in
 * data[1].
 */
#define ASTRA_SYSCALL_PROCESS_SNAPSHOT          86
/*
 * Complete resident-library snapshot for the initial supervisor's PROC:
 * service. Arguments and authority match PROCESS_SNAPSHOT; D3 is measured in
 * AstraProcLibrarySnapshot records and D1 returns the record count. A library
 * mapped by several processes contributes one record per mapping.
 */
/**
 * Paginated resident-library snapshot for the initial supervisor's PROC:
 * service. Authority matches ::ASTRA_SYSCALL_PROCESS_SNAPSHOT (data[1] a
 * QUERY handle the caller holds on itself, caller is the initial
 * supervisor). data[2] is an `AstraProcLibrarySnapshot` output array
 * address, data[3] its record capacity for this call, and data[4] the
 * pagination cursor -- the ordinal of the first record to return, unlike
 * PROCESS_SNAPSHOT, which has no cursor and reports BUFFER_TOO_SMALL
 * instead of paging. Returns the number of records copied by this call
 * in data[1] and the total live record count in data[2] (pass it, or any
 * value up to it, back as the next call's data[4]). A library mapped by
 * several processes contributes one record per mapping.
 */
#define ASTRA_SYSCALL_LIBRARY_SNAPSHOT          87
/*
 * Adds the supervisor-resolved PT_INTERP image to an unfinished load.
 * D1 is the load handle, D2 points at its fixed ELF header, and D3 is its
 * complete byte length. The exact next interpreter-header range is returned
 * in D1:D2. Segment range replies identify their source in D3 using the
 * ASTRA_PROCESS_LOAD_SOURCE_* values below.
 */
/**
 * Adds the supervisor-resolved PT_INTERP image to an unfinished executable
 * load. data[1] is the load handle, data[2] points at its fixed ELF
 * header, and data[3] is its complete byte length. The exact next
 * interpreter-header range is returned in data[1]:data[2]. Later segment
 * range replies from ::ASTRA_SYSCALL_PROCESS_LOAD_WRITE identify their
 * source in data[3] using the ASTRA_PROCESS_LOAD_SOURCE_* values below.
 */
#define ASTRA_SYSCALL_PROCESS_LOAD_INTERPRETER 88
/*
 * CREATE's counterpart for exec: once every header is accepted, builds a
 * replacement address space for the calling process itself rather than a
 * child. D1 is the load handle; the first segment range is returned in
 * D1:D2 and its source in D3, exactly as CREATE returns it. PROCESS_EXEC
 * commits the load; closing the handle discards the replacement.
 */
/**
 * ::ASTRA_SYSCALL_PROCESS_LOAD_CREATE's counterpart for exec: once every
 * header is accepted, builds a replacement address space for the calling
 * process itself rather than a child. data[1] is the load handle; the
 * first segment range is returned in data[1]:data[2] and its source
 * (ASTRA_PROCESS_LOAD_SOURCE_*) in data[3], exactly as CREATE returns it.
 * ::ASTRA_SYSCALL_PROCESS_EXEC commits the load; closing the handle
 * discards the replacement instead.
 */
#define ASTRA_SYSCALL_PROCESS_LOAD_REPLACE 103
/**
 * Moves, shows or hides the display's hardware cursor. data[1] is the
 * display0 device lease handle, which must carry
 * ASTRA_DISPLAY_CAP_HARDWARE_CURSOR (::ASTRA_SYSCALL_UNSUPPORTED otherwise);
 * data[2] and data[3] are the on-screen x and y; data[4] the cursor flags
 * (ASTRA_DISPLAY_CURSOR_VISIBLE and ASTRA_DISPLAY_CURSOR_SHAPE()).
 *
 * A posted, latest-value write, the way a KMS cursor plane is: it replaces
 * whatever position the device has not yet shown and returns at once. It
 * takes no request slot, has no fence or completion, raises no interrupt,
 * and never waits behind ::ASTRA_SYSCALL_DISPLAY_SUBMIT work, so pointer
 * motion costs a frame nothing. A position off the screen or an undefined
 * flag answers ::ASTRA_SYSCALL_INVALID_ARGUMENT and changes nothing.
 */
#define ASTRA_SYSCALL_DISPLAY_CURSOR   104
/**
 * Open a handle to any live process by id, for the initial supervisor's
 * PROC: service. data[1] must be a QUERY handle the caller holds on itself,
 * and the caller must be the initial supervisor process -- the same
 * authority as ::ASTRA_SYSCALL_PROCESS_SNAPSHOT, deliberately not conferred
 * by any other handle. data[2] is the process id and data[3] its
 * generation, as PROC: reported it: a number alone never names a process,
 * because ids are reused. data[4] is the rights wanted, a nonzero subset of
 * QUERY, TERMINATE, SIGNAL, WAIT, TRANSFER and ADMINISTER (priority).
 * Returns the new handle in data[1]. A process that does not exist, has
 * exited, or has another generation answers ::ASTRA_SYSCALL_PEER_DEAD.
 *
 * This is how PROC:'s per-process `ctl` file reaches every process, however
 * it was started: everyone else asks the supervisor, which applies one
 * policy, rather than holding authority over processes by number.
 */
#define ASTRA_SYSCALL_PROCESS_OPEN     105
/**
 * Machine-wide scheduler counters for the initial supervisor's PROC:
 * service. Authority matches ::ASTRA_SYSCALL_PROCESS_SNAPSHOT: data[1] a
 * QUERY handle on the caller itself, which must be the initial supervisor.
 * data[2] is the address of an `AstraSchedulerStats` record and data[3] its
 * size, which must be exactly that record's.
 */
#define ASTRA_SYSCALL_SCHEDULER_STATS  106
/** ASTRA_PROCESS_LOAD_SOURCE_* value: the next requested range belongs to the main program image. */
#define ASTRA_PROCESS_LOAD_SOURCE_PROGRAM     0u
/** ASTRA_PROCESS_LOAD_SOURCE_* value: the next requested range belongs to the PT_INTERP image. */
#define ASTRA_PROCESS_LOAD_SOURCE_INTERPRETER 1u
/*
 * Transactional shared-library loading from an immutable positioned reader.
 * BEGIN consumes the fixed ELF header and complete file length and returns a
 * load handle plus the exact next range in D2:D3. WRITE accepts only that
 * range and returns the next. MAP publishes a reversible mapping in the
 * calling process and returns its base and page-rounded span. COMMIT seals
 * GNU RELRO and makes the mapping permanent.
 *
 * Closing an uncommitted handle aborts and unmaps it. The kernel may retain
 * validated immutable cache pages only while another mapping references them.
 * The kernel never opens paths or interprets a filesystem protocol.
 */
/**
 * Begins transactional shared-library loading from an immutable
 * positioned reader.
 *
 * data[1] is the address of the fixed ELF header already read into the
 * caller's memory and data[2] the complete file length. Returns a load
 * handle in data[1] plus the exact next file range in data[2]:data[3].
 *
 * WRITE (::ASTRA_SYSCALL_LIBRARY_LOAD_WRITE) accepts only the range just
 * returned and returns the next. MAP (::ASTRA_SYSCALL_LIBRARY_LOAD_MAP)
 * publishes a reversible mapping in the calling process and returns its
 * base and page-rounded span. COMMIT (::ASTRA_SYSCALL_LIBRARY_LOAD_COMMIT)
 * seals GNU RELRO and makes the mapping permanent.
 *
 * Closing an uncommitted handle aborts the load and unmaps it. The kernel
 * may retain validated immutable cache pages only while another mapping
 * still references them. It never opens paths or interprets a filesystem
 * protocol; userspace supplies only the bytes it requests.
 */
#define ASTRA_SYSCALL_LIBRARY_LOAD_BEGIN  89
/**
 * Supplies the next file range ::ASTRA_SYSCALL_LIBRARY_LOAD_BEGIN (or the
 * previous WRITE) asked for. data[1] is the load handle; data[2] the file
 * offset (must equal the range just returned); data[3] the address of
 * those bytes; data[4] their length. Returns the next file offset in
 * data[1] and next length in data[2].
 */
#define ASTRA_SYSCALL_LIBRARY_LOAD_WRITE  90
/**
 * Publishes a reversible mapping of a fully written library load in the
 * calling process. data[1] is the load handle. Returns the mapped base
 * in data[1] and its page-rounded span in data[2].
 */
#define ASTRA_SYSCALL_LIBRARY_LOAD_MAP    91
/**
 * Seals GNU RELRO and makes a mapped library load's mapping permanent.
 * data[1] is the load handle. No output.
 */
#define ASTRA_SYSCALL_LIBRARY_LOAD_COMMIT 92
/*
 * Completes interpreter bootstrap for the calling process. D1 names the
 * loader-built combined TLS template, D2 its exact byte size, D3 its
 * power-of-two alignment, and D4 the exclusive page-rounded storage span.
 * A zero address/size/span with alignment one declares no TLS. The kernel
 * atomically installs current/future-thread TLS and seals the program,
 * interpreter, and template RELRO pages. The call is accepted once, while the
 * interpreted process still has only its initial thread.
 */
/**
 * Completes interpreter bootstrap for the calling process. data[1] names
 * the loader-built combined TLS template, data[2] its exact byte size,
 * data[3] its power-of-two alignment, and data[4] the exclusive
 * page-rounded storage span. A zero address/size/span with alignment one
 * declares no TLS. The kernel atomically installs current- and
 * future-thread TLS and seals the program, interpreter, and template
 * RELRO pages. Accepted only once, while the interpreted process still
 * has just its initial thread.
 */
#define ASTRA_SYSCALL_PROCESS_DYNAMIC_COMMIT 93
/** data[1]=thread handle with ASTRA_RIGHT_QUERY, data[2]=output address for one `AstraThreadInfo`. */
#define ASTRA_SYSCALL_THREAD_INFO 94
/*
 * Maps the sole resident library whose ABI identity matches the fixed-size,
 * zero-padded name at D1. Ambiguous or absent identities return WOULD_BLOCK
 * so userspace can resolve the exact provider and use LIBRARY_ATTACH.
 */
/**
 * Maps the sole resident library whose ABI identity matches the
 * fixed-size, zero-padded name at data[1]. Returns the mapped base in
 * data[1], its span in data[2] and a load handle in data[3]. Ambiguous or
 * absent identities return ::ASTRA_SYSCALL_WOULD_BLOCK so userspace can
 * resolve the exact provider and use ::ASTRA_SYSCALL_LIBRARY_ATTACH
 * instead.
 */
#define ASTRA_SYSCALL_LIBRARY_ATTACH_RESIDENT 95
/** Commits every page touched by data[1]=address, data[2]=length in a private reservation. */
#define ASTRA_SYSCALL_VM_PRIVATE_COMMIT       96
/** PID 1 only. Retires PID 1 after all other guest processes have exited. No argument. */
#define ASTRA_SYSCALL_SYSTEM_SHUTDOWN          97
/** PID 1 only. Same as ::ASTRA_SYSCALL_SYSTEM_SHUTDOWN, but the machine restarts afterwards. */
#define ASTRA_SYSCALL_SYSTEM_RESTART            98
/** data[1]=thread handle with ASTRA_RIGHT_ADMINISTER, data[2]=new priority; returns prior priority in data[1]. */
#define ASTRA_SYSCALL_THREAD_PRIORITY          99
/*
 * PID 1 only. D1=message address, D2=length (1..ASTRA_SYSTEM_FAIL_MESSAGE_MAX).
 * The system cannot continue -- a critical service died -- so the kernel halts
 * with the panic screen and names the reason. Does not return.
 */
/**
 * PID 1 only. Halts the machine with the panic screen, naming the
 * reason: a critical service died and the system cannot continue.
 * data[1] is the message address and data[2] its length, 1 through
 * ::ASTRA_SYSTEM_FAIL_MESSAGE_MAX. Does not return.
 */
#define ASTRA_SYSCALL_SYSTEM_FAIL              100
/** Longest message ::ASTRA_SYSCALL_SYSTEM_FAIL accepts, in bytes. */
#define ASTRA_SYSTEM_FAIL_MESSAGE_MAX          96u
/*
 * D1=AstraAreaCopy. Copies rows of the caller's memory into an area it holds
 * with WRITE right. The machine's copy engine moves the bytes, not the
 * MC68040; the call returns when they are in the area. UNSUPPORTED means the
 * machine has no copy engine.
 */
/**
 * Copies rows of the caller's memory into an area it holds with
 * ASTRA_RIGHT_WRITE, using the machine's copy engine rather than the
 * MC68040. data[1] is the address of one ::AstraAreaCopy. The call
 * returns once the bytes are in the area.
 * ::ASTRA_SYSCALL_UNSUPPORTED means the machine has no copy engine.
 */
#define ASTRA_SYSCALL_AREA_COPY_IN             101

/*
 * D1=AstraPortCall. A request and its reply in one trap.
 *
 * The kernel makes a one-shot reply capability for the calling thread, puts it
 * in the sent handle list at `reply_index`, sends as PORT_SEND_TRY does, and
 * blocks the caller until the reply, the deadline, a cancel, or the service
 * dropping the capability. The service answers with an ordinary PORT_SEND_TRY
 * on the capability and closes it as it would a reply port's send handle, so
 * no service knows the difference.
 *
 * A refusal before the request is sent -- a full port answers WOULD_BLOCK, as
 * PORT_SEND_TRY does -- leaves d1 = ASTRA_PORT_CALL_UNSENT and every handle
 * with the caller. Several statuses can come from either side of the send, so
 * d1 is the only way to know whether the handles moved. Once sent, the call
 * ends in d0 = the reply status, d1 = reply size, d2 = reply handle count and
 * d3 = the replying process id. BUFFER_TOO_SMALL
 * reports the sizes the reply needed; PEER_DEAD means the capability was
 * closed without a reply. TIMED_OUT and CANCELLED leave the request delivered
 * and the capability abandoned: a late reply fails for the service with
 * PEER_DEAD, exactly as a send to a closed reply port did.
 */
/**
 * A request and its reply in one trap. data[1] is the address of one
 * ::AstraPortCall.
 *
 * The kernel makes a one-shot reply capability for the calling thread,
 * puts it in the sent handle list at `reply_index`, sends as
 * ::ASTRA_SYSCALL_PORT_SEND_TRY does, and blocks the caller until the
 * reply, the deadline, a cancel, or the service dropping the capability.
 * The service answers with an ordinary PORT_SEND_TRY on the capability
 * and closes it as it would a reply port's send handle, so no service
 * knows the difference.
 *
 * A refusal before the request is sent -- a full port answers
 * ::ASTRA_SYSCALL_WOULD_BLOCK, as PORT_SEND_TRY does -- leaves data[1] =
 * ::ASTRA_PORT_CALL_UNSENT and every handle with the caller. Several
 * statuses can come from either side of the send, so data[1] is the only
 * way to know whether the handles moved. Once sent, the call ends in
 * data[0] = the reply status, data[1] = reply size, data[2] = reply
 * handle count and data[3] = the replying process id.
 * ::ASTRA_SYSCALL_BUFFER_TOO_SMALL reports the sizes the reply needed;
 * ::ASTRA_SYSCALL_PEER_DEAD means the capability was closed without a
 * reply. TIMED_OUT and CANCELLED leave the request delivered and the
 * capability abandoned: a late reply fails for the service with
 * PEER_DEAD, exactly as a send to a closed reply port did.
 */
#define ASTRA_SYSCALL_PORT_CALL                102
/**
 * ::ASTRA_SYSCALL_PORT_CALL output in data[1]: the request was never
 * sent, so every handle the call named is still with the caller.
 */
#define ASTRA_PORT_CALL_UNSENT 0xffffffffu

/** ::ASTRA_SYSCALL_VM_PRIVATE_RESERVE permission: the reservation may be mapped readable. */
#define ASTRA_VM_PRIVATE_READ  (1u << 0)
/** ::ASTRA_SYSCALL_VM_PRIVATE_RESERVE permission: the reservation may be mapped writable. */
#define ASTRA_VM_PRIVATE_WRITE (1u << 1)
/** ::ASTRA_SYSCALL_VM_PRIVATE_RESERVE mode: fail unless the exact requested size is free. */
#define ASTRA_VM_PRIVATE_RESERVE_EXACT   0u
/** ::ASTRA_SYSCALL_VM_PRIVATE_RESERVE mode: take the largest free span up to the requested size. */
#define ASTRA_VM_PRIVATE_RESERVE_LARGEST 1u
/* Complete anonymous window in the 32-bit Astra process address map. */
/** Complete anonymous window in the 32-bit Astra process address map. */
#define ASTRA_VM_PRIVATE_ADDRESS_SPACE_MAX \
    (ASTRA_PRIVATE_ADDRESS_END - ASTRA_PRIVATE_ADDRESS_START)

/*
 * The event channel. A process that is not holding the display lease has no
 * way to say anything about itself -- the progress counter is a monotonic
 * integer and the exit status is one word -- so a service debugging itself had
 * nothing to say it with.
 *
 * It is not authority. The call takes a message id, flags, and at most
 * ASTRA_EVENT_ARGUMENT_MAX bytes of arguments, and no capability at all: a
 * machine whose account of what happened depends on a right has holes exactly
 * where something went wrong. ASTRA_RIGHT_DEBUG gates *reading* other
 * processes' events, and gates the console sink, which is where the leaking
 * risk actually is.
 *
 * There is no handle either. A process may only speak for itself, and the
 * kernel already knows who is calling, so there is nothing to pass and nothing
 * to get wrong.
 *
 * This is the cap on one line of text, which the runtime splits into a chain
 * of events. One event carries at most ASTRA_EVENT_ARGUMENT_MAX; see
 * astra/event.h.
 */
/**
 * Byte size of one ::AstraDmaBufferInfo, the record
 * ::ASTRA_SYSCALL_DMA_CREATE fills in. Kept as its own macro, rather than
 * `sizeof`, so the ABI size is checked by `_Static_assert` against
 * accidental struct-layout drift.
 */
#define ASTRA_DMA_BUFFER_INFO_SIZE 20u

/** ::ASTRA_SYSCALL_INPUT_READ_TRY output flag: the input device's event FIFO has overflowed. */
#define ASTRA_INPUT_READ_OVERFLOW  (1u << 0)


/**
 * Byte size of one ::AstraDeviceInfo, the record
 * ::ASTRA_SYSCALL_DEVICE_QUERY fills in. Kept as its own macro, rather
 * than `sizeof`, so the ABI size is checked by `_Static_assert` against
 * accidental struct-layout drift.
 */
#define ASTRA_DEVICE_INFO_SIZE 24u
/** ::AstraDeviceInfo.device_state value: the device is idle and may be leased. */
#define ASTRA_DEVICE_STATE_READY      1u
/** ::AstraDeviceInfo.device_state value: the device is leased to a process. */
#define ASTRA_DEVICE_STATE_LEASED     2u
/** ::AstraDeviceInfo.device_state value: the device is winding down its current lease. */
#define ASTRA_DEVICE_STATE_QUIESCING  3u
/** ::AstraDeviceInfo.device_state value: the device is running ::ASTRA_SYSCALL_DEVICE_RESET. */
#define ASTRA_DEVICE_STATE_RESETTING  4u
/** ::AstraDeviceInfo.device_state value: the device reported a failure and needs a reset. */
#define ASTRA_DEVICE_STATE_FAILED     5u
/** ::AstraDeviceInfo.lease_state value: the calling process's lease is in effect. */
#define ASTRA_DEVICE_LEASE_ACTIVE     1u
/** ::AstraDeviceInfo.lease_state value: the lease is being revoked and will not accept new work. */
#define ASTRA_DEVICE_LEASE_REVOKING   2u
/** ::AstraDeviceInfo.lease_state value: the lease has been fully revoked. */
#define ASTRA_DEVICE_LEASE_REVOKED    3u

/** Alias: ::ASTRA_SYSCALL_EXIT under the name POSIX callers expect. */
#define ASTRA_SYSCALL_PROCESS_EXIT ASTRA_SYSCALL_EXIT

/** The call succeeded. */
#define ASTRA_SYSCALL_OK               0
/** The syscall number in data[0] does not name a call this kernel implements. */
#define ASTRA_SYSCALL_BAD_SYSCALL      1
/** An argument's value, combination, or alignment is not one this call accepts. */
#define ASTRA_SYSCALL_INVALID_ARGUMENT 2
/** The handle does not exist, names the wrong object type, or does not carry a right the call needs. */
#define ASTRA_SYSCALL_INVALID_HANDLE   3
/** The handle exists and is the right type, but lacks the specific right this call needs. */
#define ASTRA_SYSCALL_ACCESS_DENIED    4
/** A pool, table, or quota is exhausted; retrying later, or after closing something, may succeed. */
#define ASTRA_SYSCALL_RESOURCE_LIMIT   5
/** The call would have to wait, and was asked not to (or asked only to try). */
#define ASTRA_SYSCALL_WOULD_BLOCK      6
/** The call's deadline passed before it could complete. */
#define ASTRA_SYSCALL_TIMED_OUT        7
/** The handle's peer (a reply capability, a port endpoint, a channel) is gone. */
#define ASTRA_SYSCALL_PEER_DEAD        8
/** A user-memory address or range named by an argument is not valid for this access. */
#define ASTRA_SYSCALL_BAD_ADDRESS      9
/** The call's wait was cancelled, by ::ASTRA_SYSCALL_CANCEL_WAIT or an equivalent. */
#define ASTRA_SYSCALL_CANCELLED        10
/** The kernel could not allocate the memory this call needed. */
#define ASTRA_SYSCALL_OUT_OF_MEMORY    11
/** The underlying device or transport reported a failure. */
#define ASTRA_SYSCALL_IO_ERROR         12
/** The object the call addressed has been closed. */
#define ASTRA_SYSCALL_CLOSED           13
/** An output buffer was too small; where documented, the required size is returned instead. */
#define ASTRA_SYSCALL_BUFFER_TOO_SMALL 14
/*
 * The machine cannot answer this at all -- not a refusal, not a failure, and
 * not something a retry changes. A machine with no wall clock says this to
 * ASTRA_SYSCALL_CLOCK_REALTIME.
 */
/**
 * The machine cannot answer this at all -- not a refusal, not a failure,
 * and not something a retry changes. A machine with no wall clock says
 * this to ::ASTRA_SYSCALL_CLOCK_REALTIME.
 */
#define ASTRA_SYSCALL_UNSUPPORTED      15


/** ::ASTRA_SYSCALL_EVENT_CREATE flag: the event stays signaled until ::ASTRA_SYSCALL_EVENT_RESET clears it. */
#define ASTRA_EVENT_MANUAL_RESET       (1u << 0)
/** ::ASTRA_SYSCALL_EVENT_CREATE flag: the event starts out already signaled. */
#define ASTRA_EVENT_INITIALLY_SIGNALED (1u << 1)

/** High half of ::ASTRA_DEADLINE_FOREVER: no deadline, encoded as the farthest representable instant. */
#define ASTRA_DEADLINE_NONE_HI 0x7fffffffu
/** Low half of ::ASTRA_DEADLINE_FOREVER. */
#define ASTRA_DEADLINE_NONE_LO 0xffffffffu
/* The same deadline as one number, for the callers that take one. */
/** ::ASTRA_DEADLINE_NONE_HI and ::ASTRA_DEADLINE_NONE_LO as one 64-bit value, for callers that take one. */
#define ASTRA_DEADLINE_FOREVER \
    ((((uint64_t)ASTRA_DEADLINE_NONE_HI) << 32) | ASTRA_DEADLINE_NONE_LO)

/* A process cannot hold more waitable objects than handles. */
/** Largest handle count ::ASTRA_SYSCALL_WAIT_MULTIPLE accepts: a process cannot hold more waitable objects than handles. */
#define ASTRA_WAIT_MULTIPLE_MAX ASTRA_HANDLE_COUNT_MAX
/** ::ASTRA_SYSCALL_WAIT_MULTIPLE output: no handle became ready before the deadline. */
#define ASTRA_WAIT_INDEX_NONE 0xffffffff

/**
 * Byte size of one ::AstraIrqRecord, the record ::ASTRA_SYSCALL_IRQ_READ
 * fills in.
 */
#define ASTRA_IRQ_RECORD_SIZE 16u
/**
 * Byte size of one ::AstraIrqEndpointInfo, the record
 * ::ASTRA_SYSCALL_IRQ_ENDPOINT_INFO fills in.
 */
#define ASTRA_IRQ_ENDPOINT_INFO_SIZE 36u

/*
 * The states an endpoint can be in, as ASTRA_SYSCALL_IRQ_ENDPOINT_INFO reports
 * them. They are the kernel's own, published so that a program reading the
 * surface renders a word rather than a number.
 */
/** ::AstraIrqEndpointInfo.state value: the slot has no owner. */
#define ASTRA_IRQ_ENDPOINT_FREE     0u
/** ::AstraIrqEndpointInfo.state value: the endpoint is masked and not delivering. */
#define ASTRA_IRQ_ENDPOINT_MASKED   1u
/** ::AstraIrqEndpointInfo.state value: the endpoint is armed, waiting for the next interrupt. */
#define ASTRA_IRQ_ENDPOINT_ARMED    2u
/** ::AstraIrqEndpointInfo.state value: the endpoint has at least one undelivered record pending. */
#define ASTRA_IRQ_ENDPOINT_PENDING  3u
/** ::AstraIrqEndpointInfo.state value: ::ASTRA_SYSCALL_IRQ_REVOKE is in progress. */
#define ASTRA_IRQ_ENDPOINT_REVOKING 4u

/*
 * Why an endpoint stopped serving, if it did. These are sticky: an endpoint
 * carrying any of them answers every read with the matching status until
 * something recovers it, which is the whole reason this surface exists -- a
 * quarantined device is otherwise indistinguishable from an idle one.
 */
/**
 * ::AstraIrqEndpointInfo.event_flags bit: the endpoint's record queue
 * overflowed and dropped at least one delivery. Sticky until
 * ::ASTRA_SYSCALL_IRQ_RECOVER clears it -- see the comment above these
 * bits for why that matters.
 */
#define ASTRA_IRQ_ENDPOINT_EVENT_OVERFLOW     (1u << 0)
/** ::AstraIrqEndpointInfo.event_flags bit: the endpoint was quarantined for delivering too many interrupts too fast. */
#define ASTRA_IRQ_ENDPOINT_EVENT_STORM        (1u << 1)
/** ::AstraIrqEndpointInfo.event_flags bit: the device failed to acknowledge or complete an interrupt. */
#define ASTRA_IRQ_ENDPOINT_EVENT_DEVICE_ERROR (1u << 2)
/**
 * ::ASTRA_SYSCALL_IRQ_READ output in data[1]: the endpoint's record queue
 * overflowed. Identical bit value to ::ASTRA_IRQ_ENDPOINT_EVENT_OVERFLOW;
 * named separately because it is read from a syscall result register
 * rather than a struct field.
 */
#define ASTRA_IRQ_EVENT_OVERFLOW     (1u << 0)
/** ::ASTRA_SYSCALL_IRQ_READ output in data[1]: storm quarantine. Identical bit value to ::ASTRA_IRQ_ENDPOINT_EVENT_STORM. */
#define ASTRA_IRQ_EVENT_STORM        (1u << 1)
/** ::ASTRA_SYSCALL_IRQ_READ output in data[1]: device error quarantine. Identical bit value to ::ASTRA_IRQ_ENDPOINT_EVENT_DEVICE_ERROR. */
#define ASTRA_IRQ_EVENT_DEVICE_ERROR (1u << 2)



#ifndef __ASSEMBLER__

#include <stdint.h>

/*
 * Every record the syscall boundary copies to or from user memory carries
 * ASTRA_ABI_ALIGNMENT, and the kernel refuses an address that does not hold
 * it. The alignment has to be written down here rather than assumed: the m68k
 * ABI aligns uint32_t to two bytes, so a record built from uint32_t fields is
 * only four-byte aligned by luck of where the linker or the stack happens to
 * put it. A batch buffer that moved from a stack frame into .bss landed on an
 * odd word and every read was refused with INVALID_ARGUMENT -- which the
 * caller saw as "no input", so it looked like a hang rather than a refusal.
 */
/**
 * Required alignment for every record the syscall boundary copies to or
 * from user memory; the kernel refuses an address that does not hold it.
 *
 * Has to be written down here rather than assumed: the m68k ABI aligns
 * `uint32_t` to two bytes, so a record built from `uint32_t` fields is
 * only four-byte aligned by luck of where the linker or the stack happens
 * to put it. A batch buffer that moved from a stack frame into `.bss`
 * landed on an odd word and every read was refused with
 * INVALID_ARGUMENT -- which the caller saw as "no input", so it looked
 * like a hang rather than a refusal.
 */
#define ASTRA_ABI_ALIGNMENT 4u
/* Plain 32-bit scalar arrays follow the m68k ABI, which aligns them to 2. */
/** Required alignment for a plain 32-bit scalar array, which follows the m68k ABI's own alignment of 2 rather than ::ASTRA_ABI_ALIGNMENT. */
#define ASTRA_SCALAR_ALIGNMENT 2u

/**
 * Argument block for ::ASTRA_SYSCALL_AREA_COPY_IN: `rows` rows of
 * `row_bytes` bytes, copied by the machine's copy engine from `source`
 * (stepping `source_pitch` bytes per row) to `area_offset` within `area`
 * (stepping `area_pitch` bytes per row).
 */
typedef struct AstraAreaCopy {
    /** Must equal ::ASTRA_AREA_COPY_SIZE; checked before anything else in the block is read. */
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    /** Handle to the destination area, held with ASTRA_RIGHT_WRITE. */
    uint32_t area;
    /** Byte offset of the first destination row within `area`. */
    uint32_t area_offset;
    /** Bytes between the start of one destination row and the next; at least `row_bytes`. */
    uint32_t area_pitch;
    /** Address of the first source row in the caller's own memory. */
    uint32_t source;
    /** Bytes between the start of one source row and the next; at least `row_bytes`. */
    uint32_t source_pitch;
    /** Bytes copied per row. */
    uint32_t row_bytes;
    /** Number of rows to copy. */
    uint32_t rows;
} AstraAreaCopy;

/** Required value of `AstraAreaCopy.size`; also its `sizeof`, checked by `_Static_assert` below. */
#define ASTRA_AREA_COPY_SIZE 32u
/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraAreaCopy) == ASTRA_AREA_COPY_SIZE,
               "area copy ABI size changed");
/** @endcond */

/**
 * Argument block for ::ASTRA_SYSCALL_PORT_CALL: a request and the buffers
 * to receive its reply. `reply_index` may equal `handle_count`, which
 * puts the reply capability last; the service receives `handle_count + 1`
 * handles.
 */
typedef struct AstraPortCall {
    /** Must equal ::ASTRA_PORT_CALL_SIZE. */
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    /** Handle to the destination port-send (or reply) endpoint. */
    uint32_t port;
    /** Address of the request message in the caller's own memory. */
    uint32_t request;
    /** Byte length of the request message, including its header. */
    uint32_t request_size;
    /** Address of the array of handles attached to the request. */
    uint32_t handles;
    /** Number of handles in `handles`, not counting the reply capability. */
    uint32_t handle_count;
    /** Position within the attached-handle list the kernel inserts the one-shot reply capability at; may equal `handle_count`. */
    uint32_t reply_index;
    /** Address to receive the reply message. */
    uint32_t reply;
    /** Byte capacity of the `reply` buffer. */
    uint32_t reply_capacity;
    /** Address to receive the reply's attached handles. */
    uint32_t reply_handles;
    /** Element capacity of the `reply_handles` buffer. */
    uint32_t reply_handle_capacity;
    /** High half of the absolute monotonic deadline to wait for a reply. */
    uint32_t deadline_hi;
    /** Low half of the absolute monotonic deadline to wait for a reply. */
    uint32_t deadline_lo;
} AstraPortCall;

/** Required value of `AstraPortCall.size`; also its `sizeof`, checked by `_Static_assert` below. */
#define ASTRA_PORT_CALL_SIZE 52u
/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraPortCall) == ASTRA_PORT_CALL_SIZE,
               "port call ABI size changed");
/** @endcond */

/** One interrupt delivery, as ::ASTRA_SYSCALL_IRQ_READ copies it out. */
typedef struct AstraIrqRecord {
    /** High half of the delivery timestamp, in the monotonic clock's units. */
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t timestamp_high;
    /** Low half of the delivery timestamp. */
    uint32_t timestamp_low;
    /** Device-specific status word the controller's capture callback read at delivery. */
    uint32_t status;
    /** Monotonic per-endpoint delivery sequence, passed back to ::ASTRA_SYSCALL_IRQ_ACK. */
    uint32_t sequence;
} AstraIrqRecord;

/*
 * Transfer memory a service owns: kernel-allocated, physically contiguous,
 * charged to the caller, and mapped into it read/write. The service never
 * names a physical address; the handle is what it hands to the block engine.
 * Released by ASTRA_SYSCALL_CLOSE like any other handle.
 */
/**
 * Transfer (DMA) memory a service owns, as ::ASTRA_SYSCALL_DMA_CREATE
 * fills it in: kernel-allocated, physically contiguous, charged to the
 * caller, and mapped into it read/write. The service never names a
 * physical address; the handle is what it hands to the block, network or
 * host engine. Released by ::ASTRA_SYSCALL_CLOSE like any other handle.
 */
typedef struct AstraDmaBufferInfo {
    /** Must equal ::ASTRA_DMA_BUFFER_INFO_SIZE. */
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    /** Handle to the buffer, released with ::ASTRA_SYSCALL_CLOSE. */
    uint32_t handle;
    /** Base address the buffer is mapped at in the calling process. */
    uint32_t virtual_base;
    /** Page-rounded byte size actually mapped (may exceed the size requested). */
    uint32_t byte_size;
    /** Number of pages mapped. */
    uint32_t page_count;
} AstraDmaBufferInfo;

/*
 * What an interrupt endpoint is doing, and whether it is still doing it.
 *
 * A device that quarantines itself goes on looking exactly like a device
 * nobody is using: the handles are still open, the driver is still calling,
 * and every call comes back with an I/O error whose cause is three layers
 * down. This is the surface that tells them apart, and `event_flags` is the
 * field that does it.
 */
/**
 * What an interrupt endpoint is doing, and whether it is still doing it,
 * as ::ASTRA_SYSCALL_IRQ_ENDPOINT_INFO fills it in.
 *
 * A device that quarantines itself goes on looking exactly like a device
 * nobody is using: the handles are still open, the driver is still
 * calling, and every call comes back with an I/O error whose cause is
 * three layers down. This is the surface that tells them apart, and
 * `event_flags` is the field that does it.
 */
typedef struct AstraIrqEndpointInfo {
    /** Must equal ::ASTRA_IRQ_ENDPOINT_INFO_SIZE; filled in even for a free slot. */
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    /** Owning process id, or zero when the slot is free. */
    uint32_t owner;
    /** Generation counter, incremented each time the slot is reused for a new owner. */
    uint32_t generation;
    /** Total interrupts delivered to this endpoint. */
    uint32_t delivered;
    /** Total records acknowledged via ::ASTRA_SYSCALL_IRQ_ACK. */
    uint32_t acknowledged;
    /** Total records dropped because the endpoint's queue was full. */
    uint32_t dropped;
    /** Live handle references to this endpoint. */
    uint16_t references;
    /** Threads currently blocked waiting on this endpoint. */
    uint16_t waiters;
    /** Interrupt source number this endpoint is bound to. */
    uint8_t source;
    /** The endpoint's current state; one of the ASTRA_IRQ_ENDPOINT_* values. */
    uint8_t state;
    /** Trigger mode (edge/level) the controller was configured with. */
    uint8_t trigger;
    /** Interrupt priority level the source is routed at. */
    uint8_t ipl;
    /** Undelivered records currently queued. */
    uint8_t pending_records;
    /** Sticky quarantine flags; the ASTRA_IRQ_ENDPOINT_EVENT_* bits. */
    uint8_t event_flags;
    /** Consecutive deliveries counted toward the storm-quarantine threshold since the last acknowledgment. */
    uint8_t consecutive;
    /** Reserved; always zero. */
    uint8_t reserved;
} AstraIrqEndpointInfo;

/** A device's identity and lease state, as ::ASTRA_SYSCALL_DEVICE_QUERY fills it in. */
typedef struct AstraDeviceInfo {
    /** Must equal ::ASTRA_DEVICE_INFO_SIZE. */
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    /** The device's ASTRA_DEVICE_ID_* identity. */
    uint32_t device_id;
    /** The device's ASTRA_DEVICE_CLASS_* class. */
    uint32_t class_id;
    /** Device-class-specific capability bits. */
    uint32_t capabilities;
    /** Generation counter, incremented across resets and lease changes. */
    uint32_t generation;
    /** The device's current state; one of the ASTRA_DEVICE_STATE_* values. */
    uint8_t device_state;
    /** The calling process's lease state; one of the ASTRA_DEVICE_LEASE_* values. */
    uint8_t lease_state;
    /** Reserved; always zero. */
    uint16_t reserved;
} AstraDeviceInfo;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraDeviceInfo) == ASTRA_DEVICE_INFO_SIZE,
               "device-info ABI size changed");

_Static_assert(sizeof(AstraDmaBufferInfo) == ASTRA_DMA_BUFFER_INFO_SIZE,
               "dma-buffer-info ABI size changed");

_Static_assert(sizeof(AstraIrqRecord) == ASTRA_IRQ_RECORD_SIZE,
               "IRQ record ABI size changed");

_Static_assert(sizeof(AstraIrqEndpointInfo) == ASTRA_IRQ_ENDPOINT_INFO_SIZE,
               "IRQ endpoint-info ABI size changed");
_Static_assert(_Alignof(AstraIrqEndpointInfo) % ASTRA_ABI_ALIGNMENT == 0u,
               "IRQ endpoint-info must satisfy the syscall alignment rule");

/*
 * The refusal these prevent is silent at the call site, so it is caught at
 * compile time on the target that has the weaker alignment rule rather than
 * at run time on the machine that hangs.
 */
_Static_assert(_Alignof(AstraDeviceInfo) % ASTRA_ABI_ALIGNMENT == 0u,
               "device-info must satisfy the syscall alignment rule");
_Static_assert(_Alignof(AstraDmaBufferInfo) % ASTRA_ABI_ALIGNMENT == 0u,
               "dma-buffer-info must satisfy the syscall alignment rule");
_Static_assert(_Alignof(AstraIrqRecord) % ASTRA_ABI_ALIGNMENT == 0u,
               "IRQ record must satisfy the syscall alignment rule");
/** @endcond */


#endif

/** @} */

#endif
