#ifndef ASTRA_INTERFACE_KIT_H
#define ASTRA_INTERFACE_KIT_H

/** @file interface_kit.h @brief Interface Kit shared-library identity. */

#include <astra/control.h>
#include <astra/interface.h>
#include <astra/pointer.h>
#include <astra/interface_library.h>
#include <astra/scroll.h>

/** Logical name of the Interface Kit shared library. */
#define ASTRA_INTERFACE_LIBRARY_NAME "interface.library"
/** Minimum compatible Interface Kit major version. */
#define ASTRA_INTERFACE_LIBRARY_VERSION ASTRA_INTERFACE_LIBRARY_ABI_MAJOR
#endif
