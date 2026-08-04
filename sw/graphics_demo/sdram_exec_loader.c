// BRAM-resident loader for the SDRAM-execution graphics regression.
#include "vesta.h"

typedef void (*entry_point)(void);

void kmain(void)
{
    while ((VESTA->SYS_STATUS & SYS_SDRAM_READY) == 0u) {}

    VESTA->SYS_CTRL = SYS_BOOT_SDRAM;
    while ((VESTA->SYS_STATUS & SYS_BOOT_OVERLAY) != 0u) {}

    ((entry_point)0xffe00400u)();
    for (;;) {}
}
