// Astra 68 chipset — umbrella header. Pulls in all four custom chips.
//
//   Vesta   (0xFFF00000)  system / region-MMU / IRQ / timers / UART / SD / input
//   Astraea (0xFFF10000)  DMA / blitter / copper / arbiter
//   Vega    (0xFFF20000)  video / tilemaps / sprites / palette
//   Lyra    (0xFFF30000)  audio (PCM + wavetable + mixer)
#ifndef ASTRA_H
#define ASTRA_H

#include "vesta.h"
#include "astraea.h"
#include "vega.h"
#include "lyra.h"

#endif // ASTRA_H
