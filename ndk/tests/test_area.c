#include <astra/area.h>

#include "syscall.h"
#include "syscall_script.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static const uint32_t full_area_rights =
    ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
    ASTRA_RIGHT_TRANSFER | ASTRA_RIGHT_ADMINISTER;

static void test_create_map_unmap_close(void)
{
    AstraArea area = ASTRA_AREA_INIT;

    astra_ndk_syscall_script_reset();
    astra_ndk_expect_syscall(ASTRA_SYSCALL_AREA_CREATE, 8192u,
                             full_area_rights, 0, 0, 0,
                             ASTRA_SYSCALL_OK, 0x101u, 0);
    assert(astra_area_create(8192u, full_area_rights, &area) == ASTRA_OK);
    assert(area.handle == 0x101u);
    assert(area.address == 0 && area.size == 0u && area.map_flags == 0u);
    astra_ndk_syscall_script_done();

    astra_ndk_syscall_script_reset();
    astra_ndk_expect_syscall(ASTRA_SYSCALL_AREA_MAP, 0x101u,
                             ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                             0, 0, 0, ASTRA_SYSCALL_OK,
                             0x40010000u, 8192u);
    assert(astra_area_map(&area,
                          ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE) ==
           ASTRA_OK);
    assert((uintptr_t)area.address == UINT32_C(0x40010000));
    assert(area.size == 8192u);
    assert(area.map_flags ==
           (ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE));
    astra_ndk_syscall_script_done();

    astra_ndk_syscall_script_reset();
    astra_ndk_expect_syscall(ASTRA_SYSCALL_AREA_UNMAP, 0x40010000u,
                             0, 0, 0, 0, ASTRA_SYSCALL_OK, 0, 0);
    assert(astra_area_unmap(&area) == ASTRA_OK);
    assert(area.address == 0 && area.size == 0u && area.map_flags == 0u);
    assert(area.handle == 0x101u);
    astra_ndk_syscall_script_done();

    astra_ndk_syscall_script_reset();
    astra_ndk_expect_syscall(ASTRA_SYSCALL_CLOSE, 0x101u,
                             0, 0, 0, 0, ASTRA_SYSCALL_OK, 0, 0);
    assert(astra_area_close(&area) == ASTRA_OK);
    assert(area.handle == ASTRA_INVALID_HANDLE);
    astra_ndk_syscall_script_done();
}

static void test_close_orders_unmap_before_handle(void)
{
    AstraArea area = {
        0x201u, (void *)(uintptr_t)UINT32_C(0x40020000), 4096u,
        ASTRA_AREA_MAP_READ
    };

    astra_ndk_syscall_script_reset();
    astra_ndk_expect_syscall(ASTRA_SYSCALL_AREA_UNMAP, 0x40020000u,
                             0, 0, 0, 0, ASTRA_SYSCALL_OK, 0, 0);
    astra_ndk_expect_syscall(ASTRA_SYSCALL_CLOSE, 0x201u,
                             0, 0, 0, 0, ASTRA_SYSCALL_OK, 0, 0);
    assert(astra_area_close(&area) == ASTRA_OK);
    assert(area.handle == ASTRA_INVALID_HANDLE && area.address == 0);
    astra_ndk_syscall_script_done();

    area.handle = 0x202u;
    area.address = (void *)(uintptr_t)UINT32_C(0x40020000);
    area.size = 4096u;
    area.map_flags = ASTRA_AREA_MAP_READ;
    astra_ndk_syscall_script_reset();
    astra_ndk_expect_syscall(ASTRA_SYSCALL_AREA_UNMAP, 0x40020000u,
                             0, 0, 0, 0, ASTRA_SYSCALL_IO_ERROR, 0, 0);
    astra_ndk_expect_syscall(ASTRA_SYSCALL_CLOSE, 0x202u,
                             0, 0, 0, 0, ASTRA_SYSCALL_OK, 0, 0);
    assert(astra_area_close(&area) == ASTRA_ERROR_IO);
    assert(area.handle == ASTRA_INVALID_HANDLE);
    assert((uintptr_t)area.address == UINT32_C(0x40020000));
    astra_ndk_syscall_script_done();
}

static void test_reduced_right_duplicate(void)
{
    AstraHandle duplicate = ASTRA_INVALID_HANDLE;
    const uint32_t rights =
        ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER;

    astra_ndk_syscall_script_reset();
    astra_ndk_expect_syscall(ASTRA_SYSCALL_HANDLE_DUPLICATE, 0x301u,
                             rights, 0, 0, 0,
                             ASTRA_SYSCALL_OK, 0x302u, 0);
    assert(astra_handle_duplicate(0x301u, rights, &duplicate) == ASTRA_OK);
    assert(duplicate == 0x302u);
    astra_ndk_syscall_script_done();

    duplicate = ASTRA_INVALID_HANDLE;
    astra_ndk_syscall_script_reset();
    astra_ndk_expect_syscall(ASTRA_SYSCALL_HANDLE_DUPLICATE, 0x301u,
                             ASTRA_RIGHT_WRITE, 0, 0, 0,
                             ASTRA_SYSCALL_ACCESS_DENIED, 0, 0);
    assert(astra_handle_duplicate(0x301u, ASTRA_RIGHT_WRITE, &duplicate) ==
           ASTRA_ERROR_PERMISSION);
    assert(duplicate == ASTRA_INVALID_HANDLE);
    astra_ndk_syscall_script_done();
}

static void test_validation_and_error_preservation(void)
{
    AstraArea area = ASTRA_AREA_INIT;
    AstraHandle occupied = 0x401u;

    astra_ndk_syscall_script_reset();
    assert(astra_area_create(0u, full_area_rights, &area) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_area_create(ASTRA_AREA_SIZE_MAX + 1u,
                             full_area_rights, &area) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_area_create(4096u, 0u, &area) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_area_map(&area, ASTRA_AREA_MAP_READ) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_area_unmap(&area) == ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_area_close(&area) == ASTRA_ERROR_INVALID_HANDLE);
    assert(astra_handle_duplicate(0x401u, ASTRA_RIGHT_READ, &occupied) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    astra_ndk_syscall_script_done();

    astra_ndk_syscall_script_reset();
    astra_ndk_expect_syscall(ASTRA_SYSCALL_AREA_CREATE, 4096u,
                             full_area_rights, 0, 0, 0,
                             ASTRA_SYSCALL_OUT_OF_MEMORY, 0, 0);
    assert(astra_area_create(4096u, full_area_rights, &area) ==
           ASTRA_ERROR_OUT_OF_MEMORY);
    assert(area.handle == ASTRA_INVALID_HANDLE);
    astra_ndk_syscall_script_done();
}

int main(void)
{
    test_create_map_unmap_close();
    test_close_orders_unmap_before_handle();
    test_reduced_right_duplicate();
    test_validation_and_error_preservation();
    puts("NDK area tests passed");
    return 0;
}
