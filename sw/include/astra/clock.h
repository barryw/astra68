#ifndef ASTRA_CLOCK_H
#define ASTRA_CLOCK_H

#include <stdint.h>

#define ASTRA_DEVICE_CLASS_CLOCK UINT32_C(0x434c4f43) /* CLOC */
#define ASTRA_DEVICE_ID_CLOCK0   UINT32_C(0x434c0001)
#define ASTRA_CAPABILITY_CLOCK   "CLOCK"

/* The monotonic counter advances once per 12.5 MHz MC68040 cycle. The
 * realtime device stores nanoseconds directly. These are hardware
 * resolutions, not scheduler policy or accuracy claims. */
#define ASTRA_CLOCK_MONOTONIC_RESOLUTION_NS UINT32_C(80)
#define ASTRA_CLOCK_REALTIME_RESOLUTION_NS  UINT32_C(1)

#endif
