#ifndef ASTRA_EVENT_H
#define ASTRA_EVENT_H

#include <stdint.h>

/*
 * What a program says about itself, and the shape the kernel records it in.
 *
 * This is the ABI half of the event system: the levels, the flags and the
 * argument bound are the same numbers in the kernel's trace ring and in a
 * program's call, defined here once so the two cannot drift. sw/kernel/trace.h
 * spells them KERNEL_TRACE_* and means these.
 *
 * Emitting needs no capability. If the machine's account of what happened
 * depended on one, it would have holes exactly where something went wrong;
 * ASTRA_RIGHT_DEBUG gates *reading* other processes' events instead.
 */

/*
 * Five levels, ordered, because filtering by severity is the one thing every
 * reader of a log wants. This is the only place severity lives on this
 * machine -- a status never carries it, and astra/status.h says why.
 */
#define ASTRA_EVENT_LEVEL_DEBUG   0u
#define ASTRA_EVENT_LEVEL_INFO    1u
#define ASTRA_EVENT_LEVEL_NOTICE  2u
#define ASTRA_EVENT_LEVEL_WARNING 3u
#define ASTRA_EVENT_LEVEL_ERROR   4u
#define ASTRA_EVENT_LEVEL_MASK    0x0007u

/* The person was shown this. What makes an event a notification history. */
#define ASTRA_EVENT_FLAG_PRESENTED     0x0008u
/* The payload is text rather than typed arguments. */
#define ASTRA_EVENT_FLAG_INLINE_STRING 0x0010u
/*
 * More of the same text follows in the next event from this thread. A line
 * longer than one payload is a chain rather than a longer record: the record
 * stays one slot wide, which is what keeps the ring's reader simple.
 */
#define ASTRA_EVENT_FLAG_CONTINUED     0x0020u

#define ASTRA_EVENT_FLAG_MASK (ASTRA_EVENT_LEVEL_MASK | \
                               ASTRA_EVENT_FLAG_PRESENTED | \
                               ASTRA_EVENT_FLAG_INLINE_STRING | \
                               ASTRA_EVENT_FLAG_CONTINUED)

/*
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
#define ASTRA_EVENT_SUBSYSTEM_KERNEL     0u
#define ASTRA_EVENT_SUBSYSTEM_RUNTIME    1u
#define ASTRA_EVENT_SUBSYSTEM_SUPERVISOR 2u
#define ASTRA_EVENT_SUBSYSTEM_STORAGE    3u
#define ASTRA_EVENT_SUBSYSTEM_VFS        4u
#define ASTRA_EVENT_SUBSYSTEM_SHELL      5u
#define ASTRA_EVENT_SUBSYSTEM_INPUT      6u
#define ASTRA_EVENT_SUBSYSTEM_DISPLAY    7u
#define ASTRA_EVENT_SUBSYSTEM_MAX        8u

/*
 * Reserved message ids. A message id becomes the address of a descriptor once
 * the ASTRA_EVENT macro exists, and descriptors live far above these.
 */
#define ASTRA_EVENT_MESSAGE_NONE         0u
#define ASTRA_EVENT_MESSAGE_UNSTRUCTURED 1u  /* a line of text, as text */
#define ASTRA_EVENT_MESSAGE_RESERVED_MAX 15u

/*
 * One drained event, as a reader receives it.
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
#define ASTRA_EVENT_DRAINED_SIZE 56u

typedef struct AstraEventDrained {
    uint32_t sequence;         /* the ring's total order; the drain cursor */
    uint32_t timestamp_high;
    uint32_t timestamp_low;
    uint32_t process;          /* generation-tagged, per OBSERVABILITY.md */
    uint32_t message;
    uint32_t activity;
    uint16_t thread;
    uint16_t flags;            /* level, presented, inline string */
    uint16_t payload_length;   /* 0..ASTRA_EVENT_ARGUMENT_MAX */
    uint16_t reserved;
    uint8_t  payload[ASTRA_EVENT_ARGUMENT_MAX];
} AstraEventDrained;

#endif
