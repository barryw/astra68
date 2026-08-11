#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <astra/input.h>
#include <astra/input_service.h>
#include <astra/pointer.h>
#include <astra/status.h>

static uint32_t event_receive;
static uint32_t reply_receive;
static uint32_t pending_transaction;

static void header(AstraMessageHeader *value, uint32_t size,
                   uint32_t operation, uint32_t transaction)
{
    value->total_size = size;
    value->header_size = ASTRA_MESSAGE_HEADER_SIZE;
    value->protocol = ASTRA_INPUT_SERVICE_PROTOCOL;
    value->protocol_version = ASTRA_INPUT_SERVICE_VERSION;
    value->operation = operation;
    value->transaction_id = transaction;
}

uint32_t astra_ndk_test_syscall(uint32_t number, uintptr_t d1, uintptr_t d2,
                                uintptr_t d3, uintptr_t d4, uintptr_t d5,
                                uint32_t *out_d1, uint32_t *out_d2)
{
    static uint32_t port_count;

    *out_d1 = 0u;
    *out_d2 = 0u;
    if (number == ASTRA_SYSCALL_PORT_CREATE) {
        ++port_count;
        *out_d1 = 0x100u + port_count * 2u;
        *out_d2 = *out_d1 + 1u;
        if (d1 == 8u) {
            assert(d2 == 8u * sizeof(AstraInputEventMessage));
            event_receive = *out_d1;
        } else {
            assert(d1 == 1u && d2 == sizeof(AstraInputConnected));
            reply_receive = *out_d1;
        }
    } else if (number == ASTRA_SYSCALL_PORT_SEND_TRY) {
        const AstraInputConnect *request = (const AstraInputConnect *)d2;
        uint32_t *handles = (uint32_t *)d4;

        assert(d1 == 7u && d3 == sizeof(*request) && d5 == 2u);
        assert(request->subscriptions ==
               (ASTRA_POINTER_SUBSCRIBE_MOTION |
                ASTRA_POINTER_SUBSCRIBE_WHEEL));
        assert(request->flags == 0u && handles[0] == event_receive + 1u &&
               handles[1] == reply_receive + 1u);
        pending_transaction = request->header.transaction_id;
    } else if (number == ASTRA_SYSCALL_PORT_RECEIVE_TRY) {
        if (d1 == reply_receive) {
            AstraInputConnected *reply = (AstraInputConnected *)d2;

            assert(d3 == sizeof(*reply) && d5 == 0u);
            header(&reply->header, sizeof(*reply),
                   ASTRA_INPUT_OPERATION_CONNECTED, pending_transaction);
            reply->status = ASTRA_STATUS_OK;
            reply->client = 3u;
            reply->generation = 4u;
            *out_d1 = sizeof(*reply);
        } else {
            AstraInputEventMessage *message = (AstraInputEventMessage *)d2;

            assert(d1 == event_receive && d3 == sizeof(*message) && d5 == 0u);
            header(&message->header, sizeof(*message),
                   ASTRA_INPUT_OPERATION_EVENT, 9u);
            message->event.size = sizeof(message->event);
            message->event.version = ASTRA_INPUT_SERVICE_VERSION;
            message->event.type = ASTRA_INPUT_EVENT_POINTER_BUTTON;
            message->event.flags = ASTRA_INPUT_LOGICAL_DOWN;
            message->event.timestamp_ms = 12u;
            message->event.sequence = 9u;
            message->event.focus_generation = 4u;
            message->event.code = ASTRA_INPUT_BUTTON_WHEEL_DOWN;
            message->event.value_x = 321;
            message->event.value_y = 123;
            *out_d1 = sizeof(*message);
        }
    } else {
        assert(number == ASTRA_SYSCALL_CLOSE);
        assert(d1 == event_receive || d1 == event_receive + 1u ||
               d1 == reply_receive || d1 == reply_receive + 1u);
    }
    return ASTRA_SYSCALL_OK;
}

int main(void)
{
    AstraPointerObserver observer = ASTRA_POINTER_OBSERVER_INIT;
    AstraPointerEvent event;

    assert(astra_pointer_observer_open(
               7u, ASTRA_POINTER_SUBSCRIBE_MOTION |
                       ASTRA_POINTER_SUBSCRIBE_WHEEL,
               &observer) == ASTRA_OK);
    assert(observer._private_events == event_receive &&
           observer._private_client == 3u);
    assert(astra_pointer_event_try(&observer, &event) == ASTRA_OK);
    assert(event.type == ASTRA_POINTER_EVENT_WHEEL &&
           event.screen_x == 321 && event.screen_y == 123 &&
           event.wheel_x == 0 && event.wheel_y == -1);
    assert(astra_pointer_observer_close(&observer) == ASTRA_OK);
    assert(observer._private_events == 0u &&
           observer._private_client == 0u);
    puts("pointer observer contract tests passed");
    return 0;
}
