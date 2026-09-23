#include <astra/ndk.h>
#include <astra/vfs_client.h>

#include <stddef.h>

_Static_assert(sizeof(AstraSyscallResult) == 20u,
               "installed NDK lost the runtime contract");
_Static_assert(_Alignof(AstraVfsClient) >= 4u &&
                   offsetof(AstraVfsClient, port_direct_lock) % 4u == 0u &&
                   offsetof(AstraVfsClient, port_connecting) % 4u == 0u &&
                   offsetof(AstraVfsClient, port_inflight) % 4u == 0u &&
                   offsetof(AstraVfsClient, port_lifecycle) % 4u == 0u &&
                   offsetof(AstraVfsClient, port_thread_lock) % 4u == 0u &&
                   offsetof(AstraVfsClient, port_lane_lock) % 4u == 0u,
               "VFS futex words need four-byte alignment on MC68040");

int astra_ndk_c_header_contract(void)
{
    return ASTRA_FILESYSTEM_LIBRARY_VERSION == 4u ? 0 : 1;
}
