// SPDX-License-Identifier: MIT
#ifndef ASTRA_HOST_ARENA_UAPI_H
#define ASTRA_HOST_ARENA_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

/* The host arena is the display mailbox payload
   (ASTRA_DISPLAY_MAILBOX_PAYLOAD_BYTES): one physically contiguous block of
   HPS memory below 4 GiB that the render engine reads over F2SDRAM. */
#define ASTRA_HOST_ARENA_BYTES 0x00800000u

struct astra_host_arena_info {
	__u64 physical; /**< HPS physical base, the render host aperture base. */
	__u64 bytes; /**< Always ASTRA_HOST_ARENA_BYTES. */
};

#define ASTRA_HOST_ARENA_IOC_INFO \
	_IOR('A', 0x10, struct astra_host_arena_info)

#endif
