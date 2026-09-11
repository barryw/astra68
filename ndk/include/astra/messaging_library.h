/** @file messaging_library.h @brief Messaging Kit shared-library ABI. */
#ifndef ASTRA_MESSAGING_LIBRARY_H
#define ASTRA_MESSAGING_LIBRARY_H

#include <astra/port.h>

/** Messaging Kit export-table ABI major version. */
#define ASTRA_MESSAGING_LIBRARY_ABI_MAJOR 1u
/** Messaging Kit export-table ABI minor version. */
#define ASTRA_MESSAGING_LIBRARY_ABI_MINOR 0u

/** Messaging Kit 1.x immutable export table. */
typedef struct AstraMessagingLibraryV1 {
    uint16_t abi_major; /**< ASTRA_MESSAGING_LIBRARY_ABI_MAJOR. */
    uint16_t abi_minor; /**< ASTRA_MESSAGING_LIBRARY_ABI_MINOR. */
    uint32_t structure_size; /**< Bytes available in this table. */
    /** Close and invalidate a handle. */
    AstraResult (*handle_close)(AstraHandle *);
    /** Duplicate a handle with reduced rights. */
    AstraResult (*handle_duplicate)(AstraHandle, uint32_t, AstraHandle *);
    /** Initialize the shared message header. */
    AstraResult (*message_header_init)(AstraMessageHeader *, uint32_t,
                                       uint32_t, uint16_t, uint32_t,
                                       uint32_t);
    /** Create a bounded message port. */
    AstraResult (*port_create)(uint32_t, uint32_t, AstraPort *);
    /** Close both ends of a message port. */
    AstraResult (*port_close)(AstraPort *);
    /** Attempt a nonblocking send. */
    AstraResult (*port_send_try)(AstraHandle, const void *, uint32_t,
                                 AstraHandle *, uint32_t);
    /** Send until an absolute deadline. */
    AstraResult (*port_send_until)(AstraHandle, const void *, uint32_t,
                                   AstraHandle *, uint32_t,
                                   AstraMonotonicDeadline);
    /** Attempt a nonblocking receive. */
    AstraResult (*port_receive_try)(AstraHandle, void *, uint32_t,
                                    AstraHandle *, uint32_t, uint32_t *,
                                    uint32_t *);
    /** Receive until an absolute deadline. */
    AstraResult (*port_receive_until)(AstraHandle, void *, uint32_t,
                                      AstraHandle *, uint32_t, uint32_t *,
                                      uint32_t *, AstraMonotonicDeadline);
} AstraMessagingLibraryV1;

#endif
