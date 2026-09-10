#include <astra/ndk.h>

_Static_assert(sizeof(AstraSyscallResult) == 20u,
               "installed NDK lost the runtime contract");

int astra_ndk_c_header_contract(void)
{
    return ASTRA_FILESYSTEM_LIBRARY_VERSION == 2u ? 0 : 1;
}
