#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t header;
    uint32_t value;
    uint32_t timestamp_ms;
    uint32_t device_sequence;
} astra_input_event_t;

bool astra_input_initialize(void);

// Safe from FreeRTOS task context. The header is encoded as
// class[31:24], kind[23:16], flags[15:0]. Device zero is reserved.
bool astra_input_submit(uint8_t device_class, uint8_t kind, uint16_t flags,
                        uint32_t value, uint16_t device_id);

bool astra_input_receive(astra_input_event_t *event, uint32_t wait_ms);
