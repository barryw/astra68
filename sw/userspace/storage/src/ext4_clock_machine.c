/*
 * The machine's wall clock, in the seconds ext4 stores.
 *
 * Every process that mounts the volume binds the same one -- the storage
 * service, which is what a program's writes reach, and the supervisor, which
 * writes its own boot check. They had a copy each for exactly as long as it
 * took to notice that fixing one left the other stamping nothing.
 *
 * Target only: it calls the runtime, and the host mount test binds the host's
 * clock instead. Zero when the machine does not know the date, which is what
 * lwext4 wrote before it had a clock at all.
 */

#include <astra/ext4_time.h>

#include <astra/runtime.h>
#include <astra/syscall.h>

uint32_t astra_ext4_clock_machine(void)
{
    uint64_t nanoseconds = 0u;

    if (astra_clock_realtime(&nanoseconds) != ASTRA_SYSCALL_OK)
        return 0u;
    return (uint32_t)(nanoseconds / 1000000000u);
}
