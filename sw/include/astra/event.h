#ifndef ASTRA_EVENT_H
#define ASTRA_EVENT_H

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
 * Reserved message ids. A message id becomes the address of a descriptor once
 * the ASTRA_EVENT macro exists, and descriptors live far above these.
 */
#define ASTRA_EVENT_MESSAGE_NONE         0u
#define ASTRA_EVENT_MESSAGE_UNSTRUCTURED 1u  /* a line of text, as text */
#define ASTRA_EVENT_MESSAGE_RESERVED_MAX 15u

#endif
