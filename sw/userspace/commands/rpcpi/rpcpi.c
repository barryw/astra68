/* Disposable native RPC priority-inversion gate; never staged in a release. */
#include <astra/port.h>
#include <astra/program.h>
#include <astra/runtime.h>

#include <stdint.h>
#include <stdio.h>

ASTRA_PROGRAM("rpcpi", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

static AstraPort port = ASTRA_PORT_INIT;
static uint32_t ready_event;
static volatile uint32_t stop_hog;
static volatile uint32_t service_result = 1u;

static void service(uint32_t unused)
{
    AstraMessageHeader request;
    AstraMessageHeader reply;
    AstraHandle capability = ASTRA_INVALID_HANDLE;
    uint32_t size = 0u;
    uint32_t count = 0u;

    (void)unused;
    if (astra_rt_signal(ready_event, 1u, NULL) != ASTRA_SYSCALL_OK)
        return;
    if (astra_port_receive_until(port.receive, &request, sizeof(request),
                                 &capability, 1u, &size, &count,
                                 ASTRA_DEADLINE_INFINITE) != ASTRA_OK ||
        size != sizeof(request) || count != 1u)
        return;
    if (astra_message_header_init(&reply, sizeof(reply), 0x52504350u,
                                  1u, 2u, request.transaction_id) ==
            ASTRA_OK &&
        astra_port_send(capability, &reply, sizeof(reply), NULL, 0u) ==
            ASTRA_OK)
        service_result = 0u;
    (void)astra_close(capability);
}

static void hog(uint32_t unused)
{
    uint64_t deadline = astra_clock_monotonic() + UINT64_C(2000000000);

    (void)unused;
    while (stop_hog == 0u && astra_clock_monotonic() < deadline)
        __asm__ volatile("" ::: "memory");
}

int main(int argc, char **argv)
{
    const uint32_t rights = ASTRA_RIGHT_READ | ASTRA_RIGHT_WAIT |
                            ASTRA_RIGHT_ADMINISTER;
    AstraThreadStart service_start = {service, 0u};
    AstraThreadStart hog_start = {hog, 0u};
    AstraMessageHeader request;
    AstraMessageHeader reply;
    AstraCall call = {0};
    uint32_t self = ASTRA_INVALID_HANDLE;
    uint32_t service_handle = ASTRA_INVALID_HANDLE;
    uint32_t hog_handle = ASTRA_INVALID_HANDLE;
    uint32_t thread_id;
    uint32_t prior;
    uint64_t started;
    uint64_t elapsed;
    AstraResult result;
    int passed;

    (void)argc;
    (void)argv;
    if (astra_port_create(2u, 2u * sizeof(request), &port) != ASTRA_OK ||
        astra_rt_event_create(0u, ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL,
                              &ready_event) != ASTRA_SYSCALL_OK ||
        astra_current_thread_handle(&self) != ASTRA_SYSCALL_OK ||
        astra_rt_thread_create(&service_start, 5u, rights,
                                &service_handle, &thread_id) !=
            ASTRA_SYSCALL_OK ||
        astra_thread_priority(self, 1u, &prior) != ASTRA_SYSCALL_OK) {
        puts("rpcpi: setup FAIL");
        return 1;
    }
    if (astra_wait_one(ready_event, ASTRA_DEADLINE_INFINITE, NULL) !=
            ASTRA_SYSCALL_OK ||
        astra_thread_priority(self, prior, NULL) != ASTRA_SYSCALL_OK ||
        astra_rt_thread_create(&hog_start, 12u, rights,
                                &hog_handle, &thread_id) != ASTRA_SYSCALL_OK ||
        astra_message_header_init(&request, sizeof(request), 0x52504350u,
                                  1u, 1u, 1u) != ASTRA_OK) {
        puts("rpcpi: preparation FAIL");
        return 1;
    }
    call.request = &request;
    call.request_size = sizeof(request);
    call.reply = &reply;
    call.reply_capacity = sizeof(reply);
    started = astra_clock_monotonic();
    result = astra_port_call(port.send, &call, started + UINT64_C(500000000));
    elapsed = astra_clock_monotonic() - started;
    stop_hog = 1u;
    passed = result == ASTRA_OK &&
             elapsed < UINT64_C(500000000) &&
             call.reply_size == sizeof(reply) &&
             reply.transaction_id == request.transaction_id &&
             astra_wait_one(service_handle,
                            astra_clock_monotonic() + UINT64_C(2000000000),
                            NULL) == ASTRA_SYSCALL_OK &&
             service_result == 0u;
    (void)astra_wait_one(hog_handle,
                         astra_clock_monotonic() + UINT64_C(2000000000), NULL);
    (void)astra_close(hog_handle);
    (void)astra_close(service_handle);
    (void)astra_close(ready_event);
    if (astra_port_close(&port) != ASTRA_OK)
        passed = 0;
    printf("rpcpi: %s call=%lu elapsed_us=%lu\n",
           passed ? "PASS" : "FAIL", (unsigned long)result,
           (unsigned long)(elapsed / 1000u));
    return passed ? 0 : 1;
}
