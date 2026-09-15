// SPDX-License-Identifier: MIT
#ifndef ASTRA_DISPLAY_CAPTURE_UAPI_H
#define ASTRA_DISPLAY_CAPTURE_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

/** Metadata returned with each completed final-display capture. */
struct astra_display_capture_info {
	__u32 width; /**< Physical frame width in pixels. */
	__u32 height; /**< Physical frame height in pixels. */
	__u32 stride; /**< Bytes between consecutive RGB888 rows. */
	__u32 frame_bytes; /**< Total readable frame bytes. */
	__u32 generation; /**< Hardware capture generation. */
	__u32 hardware_cycles; /**< Capture cycles in the 165 MHz domain. */
	__u32 dma_channels; /**< Physical DMA channels used for readback. */
	__u32 reserved;
	__u64 capture_nanoseconds; /**< Wall time spent acquiring the frame. */
	__u64 dma_nanoseconds; /**< Wall time spent moving it to Linux RAM. */
};

#define ASTRA_DISPLAY_CAPTURE_IOC_MAGIC 'A'
#define ASTRA_DISPLAY_CAPTURE_IOC_CAPTURE \
	_IOR(ASTRA_DISPLAY_CAPTURE_IOC_MAGIC, 0, \
	     struct astra_display_capture_info)

#endif
