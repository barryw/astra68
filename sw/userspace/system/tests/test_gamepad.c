#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <astra/gamepad.h>
#include <astra/input_service.h>
#include <astra/port.h>
#include <astra/status.h>

#include "syscall.h"

static uint32_t event_receive;
static uint32_t event_number;

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

uint32_t astra_system_test_syscall(uint32_t number, uintptr_t d1, uintptr_t d2,
                                  uintptr_t d3, uintptr_t d4, uintptr_t d5,
                                  uint32_t *out_d1, uint32_t *out_d2)
{
    (void)d4;
    *out_d1 = 0u;
    *out_d2 = 0u;
    if (number == ASTRA_SYSCALL_PORT_CREATE) {
        assert(d1 == 256u &&
               d2 == 256u * sizeof(AstraInputEventMessage));
        event_receive = 0x102u;
        *out_d1 = event_receive;
        *out_d2 = event_receive + 1u;
    } else if (number == ASTRA_SYSCALL_PORT_CALL) {
        const AstraCall *call = astra_system_test_call;
        const AstraInputConnect *request = call->request;
        AstraInputConnected *reply = call->reply;

        assert(((const AstraPortCall *)d1)->port == 7u);
        assert(request->subscriptions == ASTRA_INPUT_SUBSCRIBE_GAMEPAD);
        assert(request->flags == 0u &&
               call->handles[0] == event_receive + 1u);
        header(&reply->header, sizeof(*reply),
               ASTRA_INPUT_OPERATION_CONNECTED,
               request->header.transaction_id);
        reply->status = ASTRA_STATUS_OK;
        reply->client = 2u;
        reply->generation = 3u;
        *out_d1 = sizeof(*reply);
    } else if (number == ASTRA_SYSCALL_PORT_RECEIVE_TRY) {
        AstraInputEventMessage *message = (AstraInputEventMessage *)d2;

        assert(d1 == event_receive && d3 == sizeof(*message) && d5 == 0u);
        header(&message->header, sizeof(*message),
               ASTRA_INPUT_OPERATION_EVENT, ++event_number);
        message->event.size = sizeof(message->event);
        message->event.version = ASTRA_INPUT_SERVICE_VERSION;
        message->event.type = event_number == 1u ?
            ASTRA_INPUT_EVENT_GAMEPAD_DEVICE :
            ASTRA_INPUT_EVENT_GAMEPAD_AXIS;
        message->event.flags = ASTRA_INPUT_LOGICAL_DOWN;
        message->event.timestamp_ms = 12u;
        message->event.sequence = event_number;
        message->event.focus_generation = 1u;
        message->event.code = event_number == 1u ? 0u : 2u;
        message->event.value_x = event_number == 1u ?
            (int32_t)0x046dc216u : -1234;
        message->event.value_y = (3 << 16) | 0x100;
        message->event.total_x = 4 | (12 << 8) | (1 << 16);
        *out_d1 = sizeof(*message);
    } else {
        assert(number == ASTRA_SYSCALL_CLOSE && d1 == event_receive);
    }
    return ASTRA_SYSCALL_OK;
}

int main(void)
{
    AstraGamepadObserver observer = ASTRA_GAMEPAD_OBSERVER_INIT;
    AstraGamepadEvent event;

    assert(astra_gamepad_observer_open(7u, &observer) == ASTRA_OK);
    assert(astra_gamepad_event_try(&observer, &event) == ASTRA_OK);
    assert(event.type == ASTRA_GAMEPAD_EVENT_DEVICE && event.slot == 0u &&
           event.vendor == 0x046du && event.product == 0xc216u &&
           event.bus == 3u && event.product_version == 0x100u &&
           event.axes == 4u && event.buttons == 12u && event.hats == 1u);
    assert(astra_gamepad_event_try(&observer, &event) == ASTRA_OK);
    assert(event.type == ASTRA_GAMEPAD_EVENT_AXIS && event.slot == 0u &&
           event.index == 2u && event.value == -1234);
    assert(astra_gamepad_observer_close(&observer) == ASTRA_OK);
    puts("gamepad observer contract tests passed");
    return 0;
}
