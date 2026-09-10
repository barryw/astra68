#include <astra/ndk.h>

static_assert(sizeof(AstraSyscallResult) == 20u,
              "installed NDK lost the runtime contract");

int astra_ndk_cxx_header_contract()
{
    return ASTRA_FILESYSTEM_LIBRARY_VERSION == 2u ? 0 : 1;
}
