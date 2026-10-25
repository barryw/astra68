/** @file graphics_library.h @brief Graphics Kit direct-symbol API. */
#ifndef ASTRA_GRAPHICS_LIBRARY_H
#define ASTRA_GRAPHICS_LIBRARY_H

#include <astra/bundle.h>
#include <astra/graphics.h>
#include <astra/surface.h>

/** Breaking Graphics Kit ABI generation carried by graphics.library.3. */
#define ASTRA_GRAPHICS_LIBRARY_ABI_MAJOR 3u
/** Current backward-compatible Graphics Kit revision. */
#define ASTRA_GRAPHICS_LIBRARY_ABI_MINOR 0u

/** Upload one validated AICON strike as a palette-backed INDEX8 source surface.
 * @param display Window's graphics connection.
 * @param bytes Complete AICON file, borrowed until this call returns.
 * @param length File bytes.
 * @param pixel_size Native strike size requested from the AICON decoder.
 * @param surface Empty surface receiving the uploaded source.
 * @return ASTRA_OK or a validation/resource/graphics error. Failure closes any
 * surface created by this call and leaves the output empty.
 */
AstraResult astra_surface_from_aicon(AstraDisplay *display, const void *bytes,
    uint32_t length, uint16_t pixel_size, AstraSurface *surface);

#endif
