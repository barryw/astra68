#include <astra/input_service_core.h>
#include <astra/input_port_sink.h>

#include <assert.h>
#include <stdio.h>

#define TEST_EVENT_CAPACITY 64u

typedef struct TestSink {
    AstraLogicalInputEvent events[TEST_EVENT_CAPACITY];
    uint32_t count;
    uint32_t limit;
    uint8_t dead;
} TestSink;

static AstraInputDeliveryResult sink_deliver(
    void *context, const AstraLogicalInputEvent *event)
{
    TestSink *sink = context;

    if (sink->dead != 0u)
        return ASTRA_INPUT_DELIVERY_DEAD;
    if (sink->count >= sink->limit)
        return ASTRA_INPUT_DELIVERY_FULL;
    sink->events[sink->count++] = *event;
    return ASTRA_INPUT_DELIVERY_OK;
}

static AstraInputService make_service(void)
{
    const AstraInputServiceConfig config = {
        .pointer_width = 1280u,
        .pointer_height = 720u,
        .repeat_delay_ms = 500u,
        .repeat_interval_ms = 40u,
        .acceleration_threshold = 4u,
        .acceleration_numerator = 2u,
        .acceleration_denominator = 1u,
    };
    AstraInputService service;

    assert(astra_input_service_init(&service, &config));
    return service;
}

static AstraInputEvent key(uint32_t usage, bool down, uint32_t timestamp,
                           uint32_t generation)
{
    AstraInputEvent event = {
        .header = ASTRA_INPUT_HEADER(
            ASTRA_INPUT_CLASS_KEYBOARD, ASTRA_INPUT_KEY_PHYSICAL,
            down ? ASTRA_INPUT_FLAG_DOWN : 0u),
        .value = usage,
        .timestamp_ms = timestamp,
        .device_sequence = (1u << 16) | timestamp,
        .host_generation = generation,
    };
    return event;
}

static AstraInputEvent pointer(uint32_t kind, uint32_t flags, int32_t value,
                               uint32_t timestamp)
{
    AstraInputEvent event = {
        .header = ASTRA_INPUT_HEADER(ASTRA_INPUT_CLASS_POINTER, kind, flags),
        .value = (uint32_t)value,
        .timestamp_ms = timestamp,
        .device_sequence = (2u << 16) | timestamp,
        .host_generation = 1u,
    };
    return event;
}

static void focus(AstraInputService *service, TestSink *sink)
{
    sink->limit = TEST_EVENT_CAPACITY;
    assert(astra_input_service_attach(service, 10u, sink_deliver, sink));
    assert(astra_input_service_set_focus(service, 10u, 1u));
    assert(sink->count == 1u);
    assert(sink->events[0].type == ASTRA_INPUT_EVENT_FOCUS);
    assert((sink->events[0].flags & ASTRA_INPUT_LOGICAL_FOCUSED) != 0u);
    sink->count = 0u;
}

static void test_keymap_modifiers_and_repeat(void)
{
    AstraInputService service = make_service();
    TestSink sink = {0};
    AstraInputEvent event;

    focus(&service, &sink);
    event = key(0x04u, true, 10u, 1u);
    astra_input_service_ingest(&service, &event, false);
    assert(sink.count == 2u);
    assert(sink.events[0].type == ASTRA_INPUT_EVENT_KEY);
    assert(sink.events[0].code == 0x04u);
    assert(sink.events[1].type == ASTRA_INPUT_EVENT_TEXT);
    assert(sink.events[1].code == 'a');

    astra_input_service_tick(&service, 509u);
    assert(sink.count == 2u);
    astra_input_service_tick(&service, 510u);
    assert(sink.count == 4u);
    assert((sink.events[2].flags & ASTRA_INPUT_LOGICAL_REPEAT) != 0u);
    assert(sink.events[3].code == 'a');
    astra_input_service_tick(&service, 2000u);
    assert(sink.count == 6u); /* At most one repeat is emitted per tick. */

    event = key(0x04u, false, 2001u, 1u);
    astra_input_service_ingest(&service, &event, false);
    astra_input_service_tick(&service, 3000u);
    assert(sink.count == 7u);

    sink.count = 0u;
    event = key(0xe1u, true, 3001u, 1u);
    astra_input_service_ingest(&service, &event, false);
    event = key(0x05u, true, 3002u, 1u);
    astra_input_service_ingest(&service, &event, false);
    assert(sink.count == 3u);
    assert(sink.events[2].type == ASTRA_INPUT_EVENT_TEXT);
    assert(sink.events[2].code == 'B');

    event = key(0x05u, false, 3003u, 1u);
    astra_input_service_ingest(&service, &event, false);
    event = key(0xe1u, false, 3004u, 1u);
    astra_input_service_ingest(&service, &event, false);
    sink.count = 0u;
    event = key(0xe0u, true, 3010u, 1u);
    astra_input_service_ingest(&service, &event, false);
    event = key(0x04u, true, 3011u, 1u);
    astra_input_service_ingest(&service, &event, false);
    assert(sink.count == 2u); /* Ctrl and A key events, no text event. */
    assert((uint32_t)sink.events[1].value_x & ASTRA_INPUT_MOD_LEFT_CTRL);

    event = key(0x04u, false, 3012u, 1u);
    astra_input_service_ingest(&service, &event, false);
    event = key(0xe0u, false, 3013u, 1u);
    astra_input_service_ingest(&service, &event, false);
    sink.count = 0u;
    event = key(0x28u, true, 3020u, 1u);
    astra_input_service_ingest(&service, &event, false);
    assert(sink.count == 2u && sink.events[1].code == '\n');
    event = key(0x28u, false, 3021u, 1u);
    astra_input_service_ingest(&service, &event, false);
    event = key(0x2bu, true, 3022u, 1u);
    astra_input_service_ingest(&service, &event, false);
    assert(sink.events[sink.count - 1u].code == '\t');
    event = key(0x2bu, false, 3023u, 1u);
    astra_input_service_ingest(&service, &event, false);
    event = key(0x2cu, true, 3024u, 1u);
    astra_input_service_ingest(&service, &event, false);
    assert(sink.events[sink.count - 1u].code == ' ');
}

static uint32_t alternate_map(void *context, uint32_t usage,
                              uint32_t modifiers)
{
    uint32_t *calls = context;

    (void)modifiers;
    ++*calls;
    return usage == 0x04u ? 0x03bbu : 0u;
}

static void test_replaceable_keymap(void)
{
    AstraInputService service = make_service();
    TestSink sink = {0};
    AstraInputEvent event;
    uint32_t calls = 0u;

    service.config.translate = alternate_map;
    service.config.translate_context = &calls;
    focus(&service, &sink);
    event = key(0x04u, true, 10u, 1u);
    astra_input_service_ingest(&service, &event, false);
    assert(calls == 1u);
    assert(sink.count == 2u);
    assert(sink.events[1].type == ASTRA_INPUT_EVENT_TEXT);
    assert(sink.events[1].code == 0x03bbu);
}

static void test_caps_focus_and_repairs(void)
{
    AstraInputService service = make_service();
    TestSink first = {0};
    TestSink second = {.limit = TEST_EVENT_CAPACITY};
    AstraInputEvent event;

    focus(&service, &first);
    assert(astra_input_service_attach(&service, 20u, sink_deliver, &second));
    event = key(0x39u, true, 10u, 1u);
    astra_input_service_ingest(&service, &event, false);
    event = key(0x39u, false, 11u, 1u);
    astra_input_service_ingest(&service, &event, false);
    event = key(0x04u, true, 12u, 1u);
    astra_input_service_ingest(&service, &event, false);
    assert(first.events[first.count - 1u].code == 'A');

    assert(astra_input_service_set_focus(&service, 20u, 20u));
    assert(first.events[first.count - 1u].type == ASTRA_INPUT_EVENT_FOCUS);
    assert(second.count == 1u);
    assert(second.events[0].type == ASTRA_INPUT_EVENT_FOCUS);
    assert(service.modifiers == 0u && service.repeat_usage == 0u);

    event = key(0x05u, true, 30u, 2u);
    astra_input_service_ingest(&service, &event, false);
    assert(second.events[1].type == ASTRA_INPUT_EVENT_STATE_RESET);
    assert(second.events[2].type == ASTRA_INPUT_EVENT_KEY);
    event = key(0x06u, true, 31u, 2u);
    astra_input_service_ingest(&service, &event, true);
    assert(second.events[second.count - 3u].type ==
           ASTRA_INPUT_EVENT_STATE_RESET);
    assert((second.events[second.count - 3u].flags &
            ASTRA_INPUT_LOGICAL_LOSS) != 0u);
    assert(service.stats.generation_repairs == 1u);
    assert(service.stats.overflow_repairs == 1u);
}

static void test_pointer_acceleration_clipping_and_coalescing(void)
{
    AstraInputService service = make_service();
    TestSink sink = {0};
    AstraInputEvent event;

    focus(&service, &sink);
    event = pointer(ASTRA_INPUT_POINTER_RELATIVE, 0u, 10, 10u);
    astra_input_service_ingest(&service, &event, false);
    assert(sink.events[0].value_x == 16); /* 4 + (10 - 4) * 2 */
    event = pointer(ASTRA_INPUT_POINTER_RELATIVE,
                    ASTRA_INPUT_FLAG_AXIS_Y, -100, 11u);
    astra_input_service_ingest(&service, &event, false);
    assert(sink.events[1].value_y == 0);
    event = pointer(ASTRA_INPUT_POINTER_ABSOLUTE, 0u, 5000, 12u);
    astra_input_service_ingest(&service, &event, false);
    assert(sink.events[2].value_x == 1279);

    sink.limit = sink.count;
    event = pointer(ASTRA_INPUT_POINTER_RELATIVE, 0u, -10, 13u);
    astra_input_service_ingest(&service, &event, false);
    assert(service.clients[0].motion_pending != 0u);
    sink.limit = TEST_EVENT_CAPACITY;
    astra_input_service_tick(&service, 14u);
    assert(service.clients[0].motion_pending == 0u);
    assert(sink.events[sink.count - 1u].value_x == 1263);
    assert(service.stats.coalesced_motion == 1u);
    event = pointer(ASTRA_INPUT_POINTER_RELATIVE, 0u, INT32_MAX, 15u);
    astra_input_service_ingest(&service, &event, false);
    assert(sink.events[sink.count - 1u].value_x == 1279);
    event = pointer(ASTRA_INPUT_POINTER_RELATIVE, 0u, INT32_MIN, 16u);
    astra_input_service_ingest(&service, &event, false);
    assert(sink.events[sink.count - 1u].value_x == 0);
}

static void test_full_queue_forces_state_reset(void)
{
    AstraInputService service = make_service();
    TestSink sink = {0};
    AstraInputEvent event;

    focus(&service, &sink);
    sink.limit = 0u;
    event = key(0x04u, true, 10u, 1u);
    astra_input_service_ingest(&service, &event, false);
    assert(service.clients[0].desynchronized != 0u);
    sink.limit = TEST_EVENT_CAPACITY;
    event = key(0x04u, false, 11u, 1u);
    astra_input_service_ingest(&service, &event, false);
    assert(sink.count == 2u);
    assert(sink.events[0].type == ASTRA_INPUT_EVENT_STATE_RESET);
    assert(sink.events[1].type == ASTRA_INPUT_EVENT_KEY);
    assert(service.clients[0].desynchronized == 0u);
    assert(service.stats.queue_full != 0u);
}

static void test_client_limits_and_death(void)
{
    AstraInputService service = make_service();
    TestSink sinks[ASTRA_INPUT_CLIENT_MAX] = {0};
    TestSink extra = {0};
    AstraInputEvent event;

    for (uint32_t index = 0u; index < ASTRA_INPUT_CLIENT_MAX; ++index) {
        sinks[index].limit = TEST_EVENT_CAPACITY;
        assert(astra_input_service_attach(&service, index + 1u,
                                          sink_deliver, &sinks[index]));
    }
    assert(!astra_input_service_attach(&service, 99u, sink_deliver, &extra));
    assert(astra_input_service_set_focus(&service, 1u, 1u));
    sinks[0].dead = 1u;
    event = key(0x04u, true, 2u, 1u);
    astra_input_service_ingest(&service, &event, false);
    assert(service.focus_id == 0u);
    assert(service.stats.dead_clients == 1u);
    assert(astra_input_service_detach(&service, 2u));
    assert(astra_input_service_attach(&service, 99u, sink_deliver, &extra));
}

static void test_unfocused_client_gets_loss_retry(void)
{
    AstraInputService service = make_service();
    TestSink old = {0};
    TestSink next = {.limit = TEST_EVENT_CAPACITY};

    focus(&service, &old);
    assert(astra_input_service_attach(&service, 20u, sink_deliver, &next));
    old.limit = 0u;
    assert(astra_input_service_set_focus(&service, 20u, 10u));
    assert(service.clients[0].desynchronized != 0u);
    old.limit = TEST_EVENT_CAPACITY;
    astra_input_service_tick(&service, 11u);
    assert(old.count == 1u);
    assert(old.events[0].type == ASTRA_INPUT_EVENT_STATE_RESET);
    assert(service.clients[0].desynchronized == 0u);
}

typedef struct TestPort {
    AstraInputEventMessage message;
    AstraInputPortSendResult result;
    uint32_t sends;
} TestPort;

static AstraInputPortSendResult port_send(void *context, uint32_t send_handle,
                                           const void *message,
                                           uint32_t message_size)
{
    TestPort *port = context;

    assert(send_handle == 42u);
    assert(message_size == sizeof(port->message));
    port->message = *(const AstraInputEventMessage *)message;
    ++port->sends;
    return port->result;
}

static void test_port_protocol_adapter(void)
{
    AstraInputPortSink sink;
    AstraLogicalInputEvent event = {
        .size = sizeof(event),
        .version = ASTRA_INPUT_SERVICE_VERSION,
        .type = ASTRA_INPUT_EVENT_TEXT,
        .sequence = 77u,
        .code = 'x',
    };
    TestPort port = {.result = ASTRA_INPUT_PORT_SEND_OK};

    sink.send = port_send;
    sink.context = &port;
    sink.send_handle = 42u;
    assert(astra_input_port_deliver(&sink, &event) ==
           ASTRA_INPUT_DELIVERY_OK);
    assert(port.sends == 1u);
    assert(port.message.header.total_size == sizeof(port.message));
    assert(port.message.header.protocol == ASTRA_INPUT_SERVICE_PROTOCOL);
    assert(port.message.header.protocol_version ==
           ASTRA_INPUT_SERVICE_VERSION);
    assert(port.message.header.operation == ASTRA_INPUT_OPERATION_EVENT);
    assert(port.message.header.transaction_id == 77u);
    assert(port.message.event.code == 'x');
    port.result = ASTRA_INPUT_PORT_SEND_FULL;
    assert(astra_input_port_deliver(&sink, &event) ==
           ASTRA_INPUT_DELIVERY_FULL);
    port.result = ASTRA_INPUT_PORT_SEND_PEER_DEAD;
    assert(astra_input_port_deliver(&sink, &event) ==
           ASTRA_INPUT_DELIVERY_DEAD);
}

int main(void)
{
    test_keymap_modifiers_and_repeat();
    test_replaceable_keymap();
    test_caps_focus_and_repairs();
    test_pointer_acceleration_clipping_and_coalescing();
    test_full_queue_forces_state_reset();
    test_client_limits_and_death();
    test_unfocused_client_gets_loss_retry();
    test_port_protocol_adapter();
    puts("input service core tests passed");
    return 0;
}
