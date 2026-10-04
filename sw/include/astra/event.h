#ifndef ASTRA_EVENT_H
#define ASTRA_EVENT_H

/**
 * @file event.h
 * @brief What a program says about itself, and the shape the kernel records it in.
 *
 * This is the ABI half of the event system: the levels, the flags and the
 * argument bound are the same numbers in the kernel's trace ring and in a
 * program's call, defined here once so the two cannot drift. `sw/kernel/trace.h`
 * spells them `KERNEL_TRACE_*` and means these.
 *
 * Emitting needs no capability. If the machine's account of what happened
 * depended on one, it would have holes exactly where something went wrong;
 * the process-debug right gates *reading* other processes' events instead
 * (see `ASTRA_SYSCALL_TRACE_READ` in `%syscall.h`).
 */

#include <stdint.h>

/** @defgroup astra_event Event ABI
 *  @brief Severity levels, flags and the drained-event shape shared by every
 *  emitter and every reader of the event stream.
 *  @{
 */

/*
 * Five levels, ordered, because filtering by severity is the one thing every
 * reader of a log wants. This is the only place severity lives on this
 * machine -- a status never carries it, and astra/status.h says why.
 */

/** Diagnostic detail of interest only while developing or debugging. */
#define ASTRA_EVENT_LEVEL_DEBUG   0u
/** Routine, expected activity worth keeping in the record. */
#define ASTRA_EVENT_LEVEL_INFO    1u
/** Noteworthy but not itself a problem. */
#define ASTRA_EVENT_LEVEL_NOTICE  2u
/** A condition a person may need to act on. */
#define ASTRA_EVENT_LEVEL_WARNING 3u
/** A failure. */
#define ASTRA_EVENT_LEVEL_ERROR   4u
/** Mask isolating an ::ASTRA_EVENT_LEVEL_DEBUG .. ::ASTRA_EVENT_LEVEL_ERROR value within a flags word. */
#define ASTRA_EVENT_LEVEL_MASK    0x0007u

/** Flag: the person was shown this. What makes an event a notification history. */
#define ASTRA_EVENT_FLAG_PRESENTED     0x0008u
/** Flag: the payload is text rather than typed arguments. */
#define ASTRA_EVENT_FLAG_INLINE_STRING 0x0010u
/**
 * Flag: more of the same text follows in the next event from this thread.
 *
 * A line longer than one payload is a chain rather than a longer record: the
 * record stays one slot wide, which is what keeps the ring's reader simple.
 */
#define ASTRA_EVENT_FLAG_CONTINUED     0x0020u

/** Mask of every bit a flags word may set: the level plus every flag above. */
#define ASTRA_EVENT_FLAG_MASK (ASTRA_EVENT_LEVEL_MASK | \
                               ASTRA_EVENT_FLAG_PRESENTED | \
                               ASTRA_EVENT_FLAG_INLINE_STRING | \
                               ASTRA_EVENT_FLAG_CONTINUED)

/**
 * Largest argument payload one event may carry, in bytes.
 *
 * What one event's arguments may carry. Small on purpose: the values that
 * differ between occurrences are all an event needs, because the format, the
 * file and the line belong to the message and cost nothing per occurrence.
 */
#define ASTRA_EVENT_ARGUMENT_MAX 24u

/*
 * The subsystems a level can be set for. A small closed set, because the
 * configuration is one level per subsystem and a person has to be able to read
 * the list -- an open-ended registry would be a file nobody can audit.
 */

/** The kernel. */
#define ASTRA_EVENT_SUBSYSTEM_KERNEL     0u
/** The userspace runtime shared by every process. */
#define ASTRA_EVENT_SUBSYSTEM_RUNTIME    1u
/** The supervisor process. */
#define ASTRA_EVENT_SUBSYSTEM_SUPERVISOR 2u
/** The storage stack. */
#define ASTRA_EVENT_SUBSYSTEM_STORAGE    3u
/** The virtual filesystem layer. */
#define ASTRA_EVENT_SUBSYSTEM_VFS        4u
/** The shell. */
#define ASTRA_EVENT_SUBSYSTEM_SHELL      5u
/** The input service. */
#define ASTRA_EVENT_SUBSYSTEM_INPUT      6u
/** The display service. */
#define ASTRA_EVENT_SUBSYSTEM_DISPLAY    7u
/** One past the highest valid `ASTRA_EVENT_SUBSYSTEM_*` identifier. */
#define ASTRA_EVENT_SUBSYSTEM_MAX        8u

/*
 * Reserved message ids. A message id becomes the address of a descriptor once
 * the ASTRA_EVENT macro exists, and descriptors live far above these.
 */

/** No message: an event with nothing structured to say. */
#define ASTRA_EVENT_MESSAGE_NONE         0u
/** The payload is a line of text, carried as text rather than a format and arguments. */
#define ASTRA_EVENT_MESSAGE_UNSTRUCTURED 1u
/** Highest message id reserved by this header; application message ids (descriptor addresses) start above it. */
#define ASTRA_EVENT_MESSAGE_RESERVED_MAX 15u

/** Byte size of ::AstraEventDrained, fixed by the ABI. */
#define ASTRA_EVENT_DRAINED_SIZE 56u

/**
 * One drained event, as a reader receives it from `ASTRA_SYSCALL_TRACE_READ`.
 *
 * The ring stores an event in one slot and its arguments in the next; this is
 * the two rejoined, at a fixed stride, so a drain is a copy and a reader is an
 * index rather than a walk. `payload_length` says how much of the payload is
 * the event's; the rest is zero and means nothing.
 *
 * Fixed stride costs the 24 argument bytes on an event that has none. That is
 * the right trade for a bounded batch: the alternative is a variable-length
 * stream, which is a parser in the one place -- the drain out of the kernel --
 * where the machine can least afford one.
 */
typedef struct AstraEventDrained {
    /** The ring's total order for this event; pass back as the next drain's cursor to resume after it. */
    uint32_t sequence;
    /** High 32 bits of the event's 64-bit platform timestamp. */
    uint32_t timestamp_high;
    /** Low 32 bits of the event's 64-bit platform timestamp. */
    uint32_t timestamp_low;
    /** Generation-tagged id of the process that emitted the event; see `docs/OBSERVABILITY.md`. */
    uint32_t process;
    /** The message id: ::ASTRA_EVENT_MESSAGE_NONE, ::ASTRA_EVENT_MESSAGE_UNSTRUCTURED, or an ::AstraEventDescriptor address. */
    uint32_t message;
    /** The emitting thread's activity tag, or zero when none was set. */
    uint32_t activity;
    /** The emitting thread's id. */
    uint16_t thread;
    /** ::ASTRA_EVENT_LEVEL_MASK and the ::ASTRA_EVENT_FLAG_MASK flags this event carried. */
    uint16_t flags;
    /** Bytes of `payload` that belong to this event, 0..::ASTRA_EVENT_ARGUMENT_MAX. */
    uint16_t payload_length;
    /** Reserved; always zero. */
    uint16_t reserved;
    /** Packed argument words, or inline text when ::ASTRA_EVENT_FLAG_INLINE_STRING is set; only the first `payload_length` bytes are meaningful. */
    uint8_t  payload[ASTRA_EVENT_ARGUMENT_MAX];
} AstraEventDrained;

/** @} */

#endif
