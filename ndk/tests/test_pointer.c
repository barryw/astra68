#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <astra/input.h>
#include <astra/input_service.h>
#include <astra/pointer.h>
#include <astra/port.h>
#include <astra/status.h>

#include "syscall.h"

static uint32_t event_receive;

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
    (void)d4;
    static uint32_t port_count;

    *out_d1 = 0u;
    *out_d2 = 0u;
    if (number == ASTRA_SYSCALL_PORT_CREATE) {
        ++port_count;
        *out_d1 = 0x100u + port_count * 2u;
        *out_d2 = *out_d1 + 1u;
        assert(d1 == 8u && d2 == 8u * sizeof(AstraInputEventMessage));
        event_receive = *out_d1;
    } else if (number == ASTRA_SYSCALL_PORT_CALL) {
        const AstraCall *call = astra_ndk_test_call;
        const AstraInputConnect *request = call->request;
        AstraInputConnected *reply = call->reply;

        assert(((const AstraPortCall *)d1)->port == 7u);
        assert(call->request_size == sizeof(*request));
        assert(call->handle_count == 1u && call->reply_index == 1u &&
               call->handles[0] == event_receive + 1u);
        assert(call->reply_capacity == sizeof(*reply) &&
               call->reply_handle_capacity == 0u);
        assert(request->subscriptions ==
               (ASTRA_POINTER_SUBSCRIBE_MOTION |
                ASTRA_POINTER_SUBSCRIBE_WHEEL));
        assert(request->flags == 0u);
        header(&reply->header, sizeof(*reply),
               ASTRA_INPUT_OPERATION_CONNECTED,
               request->header.transaction_id);
        reply->status = ASTRA_STATUS_OK;
        reply->client = 3u;
        reply->generation = 4u;
        *out_d1 = sizeof(*reply);
    } else if (number == ASTRA_SYSCALL_PORT_RECEIVE_TRY) {
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
        message->event.modifiers =
            ASTRA_INPUT_MOD_LEFT_ALT | ASTRA_INPUT_MOD_META;
        message->event.value_x = 321;
        message->event.value_y = 123;
        *out_d1 = sizeof(*message);
    } else {
        assert(number == ASTRA_SYSCALL_CLOSE);
        assert(d1 == event_receive);
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
           event.wheel_x == 0 && event.wheel_y == -1 &&
           event.modifiers ==
               (ASTRA_INPUT_MOD_LEFT_ALT | ASTRA_INPUT_MOD_META));
    assert(astra_pointer_observer_close(&observer) == ASTRA_OK);
    assert(observer._private_events == 0u &&
           observer._private_client == 0u);
    puts("pointer observer contract tests passed");
    return 0;
}
