/** @file events_kit.h @brief Events Kit shared-library identity. */
#ifndef ASTRA_EVENTS_KIT_H
#define ASTRA_EVENTS_KIT_H

#include <astra/events_library.h>
#include <astra/shared_library.h>

/** Logical name resolved beneath `LIBS:` by OpenLibrary(). */
#define ASTRA_EVENTS_LIBRARY_NAME "events.library"
/** Minimum compatible Events Kit ABI major. */
#define ASTRA_EVENTS_LIBRARY_VERSION 1u

#endif
