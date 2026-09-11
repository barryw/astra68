/** @file messaging_kit.h @brief Messaging Kit shared-library identity. */
#ifndef ASTRA_MESSAGING_KIT_H
#define ASTRA_MESSAGING_KIT_H

#include <astra/messaging_library.h>
#include <astra/shared_library.h>

/** Logical name resolved beneath `LIBS:` by OpenLibrary(). */
#define ASTRA_MESSAGING_LIBRARY_NAME "messaging.library"
/** Minimum compatible Messaging Kit ABI major. */
#define ASTRA_MESSAGING_LIBRARY_VERSION 1u

#endif
