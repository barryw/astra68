#ifndef ASTRA_APPLICATION_H
#define ASTRA_APPLICATION_H

/** @file application.h @brief Launch installed application bundles. */

#include <stdint.h>

#include <astra/application_service.h>
#include <astra/attributes.h>
#include <astra/resource.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/** Ask the system launcher to start one validated bundle beneath `APPS:`. */
ASTRA_NODISCARD AstraResult astra_application_launch(
    AstraHandle launcher, const char *bundle_path, uint16_t path_length,
    uint32_t *process_id);

/**
 * Launch a bundle with arguments after argv[0], which is always the bundle.
 * Desktop file drops pass their paths here and use DESKTOP as the source;
 * shells use SHELL. The bounded launch ABI rejects the whole request rather
 * than truncating an argument.
 */
ASTRA_NODISCARD AstraResult astra_application_launch_with_arguments(
    AstraHandle launcher, const char *bundle_path, uint16_t path_length,
    AstraLaunchSource source, const char *const *arguments,
    uint16_t argument_count, uint32_t *process_id);

ASTRA_EXTERN_C_END

#endif
