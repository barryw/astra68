/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ASTRA_DE25_MEDIA_H
#define ASTRA_DE25_MEDIA_H

/* The DE25 media RAM: FPGA-attached memory behind the HPS-to-FPGA bridge.
   The graphics arena, the framebuffer and the capture frame all live here. */
#define ASTRA_DE25_MEDIA_BASE 0x40000000ULL
#define ASTRA_DE25_MEDIA_BYTES 0x20000000ULL

#endif
