#ifndef ASTRA_MESSAGING_LIBRARY_H
#define ASTRA_MESSAGING_LIBRARY_H

#include <astra/port.h>

#define ASTRA_MESSAGING_LIBRARY_ABI_MAJOR 1u
#define ASTRA_MESSAGING_LIBRARY_ABI_MINOR 0u

typedef struct AstraMessagingLibraryV1 {
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t structure_size;
    AstraResult (*handle_close)(AstraHandle *);
    AstraResult (*handle_duplicate)(AstraHandle, uint32_t, AstraHandle *);
    AstraResult (*message_header_init)(AstraMessageHeader *, uint32_t,
                                       uint32_t, uint16_t, uint32_t,
                                       uint32_t);
    AstraResult (*port_create)(uint32_t, uint32_t, AstraPort *);
    AstraResult (*port_close)(AstraPort *);
    AstraResult (*port_send_try)(AstraHandle, const void *, uint32_t,
                                 AstraHandle *, uint32_t);
    AstraResult (*port_send_until)(AstraHandle, const void *, uint32_t,
                                   AstraHandle *, uint32_t,
                                   AstraMonotonicDeadline);
    AstraResult (*port_receive_try)(AstraHandle, void *, uint32_t,
                                    AstraHandle *, uint32_t, uint32_t *,
                                    uint32_t *);
    AstraResult (*port_receive_until)(AstraHandle, void *, uint32_t,
                                      AstraHandle *, uint32_t, uint32_t *,
                                      uint32_t *, AstraMonotonicDeadline);
} AstraMessagingLibraryV1;

#endif
