#ifndef ASTRA_INTERFACE_KIT_H
#define ASTRA_INTERFACE_KIT_H

/** @file interface_kit.h @brief Interface Kit shared-library identity. */

#include <astra/control.h>
#include <astra/interface.h>
#include <astra/input_library.h>
#include <astra/interface_library.h>
#include <astra/shared_library.h>

/** Logical name of the Interface Kit shared library. */
#define ASTRA_INTERFACE_LIBRARY_NAME "interface.library"
/** Minimum compatible Interface Kit major version. */
#define ASTRA_INTERFACE_LIBRARY_VERSION 2u
/** Logical name of the Input Kit shared library. */
#define ASTRA_INPUT_LIBRARY_NAME "input.library"
/** Minimum compatible Input Kit major version. */
#define ASTRA_INPUT_LIBRARY_VERSION 1u

#endif
