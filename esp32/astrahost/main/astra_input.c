#include "astra_input.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define ASTRA_INPUT_QUEUE_DEPTH 32u

static StaticQueue_t queue_control;
static uint8_t queue_storage[
    ASTRA_INPUT_QUEUE_DEPTH * sizeof(astra_input_event_t)];
static QueueHandle_t event_queue;
static portMUX_TYPE sequence_lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t next_sequence;

bool astra_input_initialize(void)
{
    if (event_queue != NULL)
        return true;
    event_queue = xQueueCreateStatic(
        ASTRA_INPUT_QUEUE_DEPTH, sizeof(astra_input_event_t),
        queue_storage, &queue_control);
    return event_queue != NULL;
}

bool astra_input_submit(uint8_t device_class, uint8_t kind, uint16_t flags,
                        uint32_t value, uint16_t device_id)
{
    if (event_queue == NULL || device_class == 0 || device_id == 0)
        return false;

    portENTER_CRITICAL(&sequence_lock);
    uint16_t sequence = ++next_sequence;
    if (sequence == 0)
        sequence = ++next_sequence;
    portEXIT_CRITICAL(&sequence_lock);

    astra_input_event_t event = {
        .header = ((uint32_t)device_class << 24) |
                  ((uint32_t)kind << 16) | flags,
        .value = value,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
        .device_sequence = ((uint32_t)device_id << 16) | sequence,
    };
    return xQueueSend(event_queue, &event, 0) == pdTRUE;
}

bool astra_input_receive(astra_input_event_t *event, uint32_t wait_ms)
{
    if (event_queue == NULL || event == NULL)
        return false;
    return xQueueReceive(event_queue, event, pdMS_TO_TICKS(wait_ms)) == pdTRUE;
}
