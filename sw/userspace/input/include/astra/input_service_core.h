#ifndef ASTRA_INPUT_SERVICE_CORE_H
#define ASTRA_INPUT_SERVICE_CORE_H

#include <astra/input.h>
#include <astra/input_service.h>

#include <stdbool.h>
#include <stdint.h>

#define ASTRA_INPUT_HELD_WORDS 8u
#define ASTRA_INPUT_REPEAT_DISABLED UINT32_MAX

typedef enum AstraInputDeliveryResult {
    ASTRA_INPUT_DELIVERY_OK = 0,
    ASTRA_INPUT_DELIVERY_FULL,
    ASTRA_INPUT_DELIVERY_DEAD
} AstraInputDeliveryResult;

typedef AstraInputDeliveryResult (*AstraInputDelivery)(
    void *context, const AstraLogicalInputEvent *event);

typedef uint32_t (*AstraInputTranslate)(void *context, uint32_t usage,
                                       uint32_t modifiers);

typedef struct AstraInputServiceConfig {
    uint32_t pointer_width;
    uint32_t pointer_height;
    uint32_t repeat_delay_ms;
    uint32_t repeat_interval_ms;
    uint32_t acceleration_threshold;
    uint32_t acceleration_numerator;
    uint32_t acceleration_denominator;
    AstraInputTranslate translate;
    void *translate_context;
} AstraInputServiceConfig;

typedef struct AstraInputClient {
    AstraInputDelivery deliver;
    void *context;
    uint32_t id;
    uint32_t generation;
    uint32_t subscriptions;
    int32_t pending_dx;
    int32_t pending_dy;
    uint8_t active;
    uint8_t desynchronized;
    uint8_t motion_pending;
    uint8_t reserved;
} AstraInputClient;

typedef struct AstraInputServiceStats {
    uint32_t physical_events;
    uint32_t logical_events;
    uint32_t text_events;
    uint32_t repeat_events;
    uint32_t focus_changes;
    uint32_t overflow_repairs;
    uint32_t generation_repairs;
    uint32_t queue_full;
    uint32_t dead_clients;
    uint32_t coalesced_motion;
} AstraInputServiceStats;

typedef struct AstraInputService {
    AstraInputServiceConfig config;
    AstraInputClient clients[ASTRA_INPUT_CLIENT_MAX];
    AstraInputServiceStats stats;
    uint32_t held[ASTRA_INPUT_HELD_WORDS];
    uint32_t modifiers;
    uint32_t physical_generation;
    uint32_t focus_id;
    uint32_t focus_generation;
    uint32_t logical_sequence;
    uint32_t repeat_deadline_ms;
    int32_t pointer_x;
    int32_t pointer_y;
    uint16_t repeat_usage;
    uint16_t reserved;
} AstraInputService;

bool astra_input_service_init(AstraInputService *service,
                              const AstraInputServiceConfig *config);
bool astra_input_service_attach(AstraInputService *service, uint32_t client_id,
                                AstraInputDelivery delivery, void *context);
bool astra_input_service_subscribe(AstraInputService *service,
                                   uint32_t client_id,
                                   uint32_t subscriptions,
                                   uint32_t timestamp_ms);
bool astra_input_service_detach(AstraInputService *service,
                                uint32_t client_id);
bool astra_input_service_set_focus(AstraInputService *service,
                                   uint32_t client_id, uint32_t timestamp_ms);
void astra_input_service_ingest(AstraInputService *service,
                                const AstraInputEvent *event,
                                bool physical_overflow);
void astra_input_service_ingest_batch(AstraInputService *service,
                                      const AstraInputEvent *events,
                                      uint32_t count,
                                      bool physical_overflow);
void astra_input_service_tick(AstraInputService *service,
                              uint32_t timestamp_ms);
uint32_t astra_input_service_next_delay(const AstraInputService *service,
                                        uint32_t timestamp_ms);
bool astra_input_service_stats(const AstraInputService *service,
                               AstraInputServiceStats *stats);

#endif
