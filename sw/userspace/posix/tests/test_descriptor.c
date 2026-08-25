#include <assert.h>
#include <stdint.h>
#include <astra/posix.h>
#include <astra/posix_descriptor.h>

uint32_t astra_yield(void) { return 0u; }

uint32_t
astra_stream_write(uint32_t handle, const void *bytes, uint32_t length,
                   uint32_t *written)
{
    (void)handle;
    (void)bytes;
    *written = length;
    return 0u;
}

uint32_t
astra_stream_read(uint32_t handle, void *bytes, uint32_t capacity,
                  uint32_t *length)
{
    (void)handle;
    (void)bytes;
    (void)capacity;
    *length = 0u;
    return 0u;
}

void
astra_process_exit(uint32_t status)
{
    (void)status;
    for (;;) {
    }
}

int
main(void)
{
    astra_posix_start(NULL);
    for (uint32_t slot = 0u; slot < 64u; ++slot) {
        int fd = astra_posix_descriptor_file(slot);

        assert(fd == (int)slot);
        assert(astra_posix_descriptor_slot(fd) == (int)slot);
    }
    astra_posix_start(NULL);
    assert(astra_posix_descriptor_file(99u) == 0);
    return 0;
}
