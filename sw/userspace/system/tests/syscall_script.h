#ifndef ASTRA_SYSTEM_TEST_SYSCALL_SCRIPT_H
#define ASTRA_SYSTEM_TEST_SYSCALL_SCRIPT_H

#include <assert.h>
#include <stdint.h>

#ifndef ASTRA_SYSTEM_TEST_SYSCALL_SCRIPT_MAX
#define ASTRA_SYSTEM_TEST_SYSCALL_SCRIPT_MAX 64u
#endif

typedef struct AstraSystemExpectedSyscall {
    uint32_t number;
    uintptr_t arguments[5];
    uint32_t status;
    uint32_t out_d1;
    uint32_t out_d2;
} AstraSystemExpectedSyscall;

static AstraSystemExpectedSyscall astra_system_syscall_script[
    ASTRA_SYSTEM_TEST_SYSCALL_SCRIPT_MAX];
static uint32_t astra_system_syscall_script_count;
static uint32_t astra_system_syscall_script_cursor;

static void astra_system_syscall_script_reset(void)
{
    astra_system_syscall_script_count = 0u;
    astra_system_syscall_script_cursor = 0u;
}

static void astra_system_expect_syscall(uint32_t number,
                                     uintptr_t d1,
                                     uintptr_t d2,
                                     uintptr_t d3,
                                     uintptr_t d4,
                                     uintptr_t d5,
                                     uint32_t status,
                                     uint32_t out_d1,
                                     uint32_t out_d2)
{
    AstraSystemExpectedSyscall *call;

    assert(astra_system_syscall_script_count <
           ASTRA_SYSTEM_TEST_SYSCALL_SCRIPT_MAX);
    call = &astra_system_syscall_script[astra_system_syscall_script_count++];
    call->number = number;
    call->arguments[0] = d1;
    call->arguments[1] = d2;
    call->arguments[2] = d3;
    call->arguments[3] = d4;
    call->arguments[4] = d5;
    call->status = status;
    call->out_d1 = out_d1;
    call->out_d2 = out_d2;
}

static void astra_system_syscall_script_done(void)
{
    assert(astra_system_syscall_script_cursor ==
           astra_system_syscall_script_count);
}

uint32_t astra_system_test_syscall(uint32_t number,
                                uintptr_t d1,
                                uintptr_t d2,
                                uintptr_t d3,
                                uintptr_t d4,
                                uintptr_t d5,
                                uint32_t *out_d1,
                                uint32_t *out_d2)
{
    const AstraSystemExpectedSyscall *call;

    assert(astra_system_syscall_script_cursor <
           astra_system_syscall_script_count);
    call = &astra_system_syscall_script[astra_system_syscall_script_cursor++];
    assert(number == call->number);
    assert(d1 == call->arguments[0]);
    assert(d2 == call->arguments[1]);
    assert(d3 == call->arguments[2]);
    assert(d4 == call->arguments[3]);
    assert(d5 == call->arguments[4]);
    assert(out_d1 != 0);
    assert(out_d2 != 0);
    *out_d1 = call->out_d1;
    *out_d2 = call->out_d2;
    return call->status;
}

#endif
