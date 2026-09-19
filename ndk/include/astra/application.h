#ifndef ASTRA_APPLICATION_H
#define ASTRA_APPLICATION_H

/** @file application.h @brief Launch installed application bundles. */

#include <stdint.h>

#include <astra/application_service.h>
#include <astra/attributes.h>
#include <astra/resource.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/** Ask the system launcher to start one validated bundle beneath `APPS:`.
 * The call returns after the application reports successful startup; process
 * exit is asynchronous.
 * @param launcher Launcher service handle.
 * @param bundle_path UTF-8 bundle path.
 * @param path_length Bytes in @p bundle_path.
 * @param process_id Receives the new process PID.
 * @return ASTRA_OK on success or an AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_application_launch(
    AstraHandle launcher, const char *bundle_path, uint16_t path_length,
    uint32_t *process_id);

/**
 * Launch a bundle with arguments after argv[0], which is always the bundle.
 * Desktop file drops pass their paths here and use DESKTOP as the source;
 * shells use SHELL. The bounded launch ABI rejects the whole request rather
 * than truncating an argument.
 * The call returns after the application reports successful startup; process
 * exit is asynchronous.
 * @param launcher Launcher service handle.
 * @param bundle_path UTF-8 bundle path.
 * @param path_length Bytes in @p bundle_path.
 * @param source Origin of the launch request.
 * @param arguments Argument strings following argv[0].
 * @param argument_count Entries in @p arguments.
 * @param process_id Receives the new process PID.
 * @return ASTRA_OK on success or an AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_application_launch_with_arguments(
    AstraHandle launcher, const char *bundle_path, uint16_t path_length,
    AstraLaunchSource source, const char *const *arguments,
    uint16_t argument_count, uint32_t *process_id);

/**
 * Launch a bundle and retain a wait-only capability for its process.
 *
 * This has the same startup-confirmed contract and argument encoding as
 * astra_application_launch_with_arguments(). The returned capability can be
 * passed to astra_process_wait() and must be closed by the caller.
 * @param launcher Launcher service handle.
 * @param bundle_path UTF-8 bundle path.
 * @param path_length Bytes in @p bundle_path.
 * @param source Origin of the launch request.
 * @param arguments Argument strings following argv[0].
 * @param argument_count Entries in @p arguments.
 * @param process_handle Receives a waitable child-process capability.
 * @param process_id Receives the new process PID.
 * @return ASTRA_OK on successful startup or an AstraResult error.
 */
ASTRA_NODISCARD AstraResult astra_application_launch_waitable(
    AstraHandle launcher, const char *bundle_path, uint16_t path_length,
    AstraLaunchSource source, const char *const *arguments,
    uint16_t argument_count, AstraHandle *process_handle,
    uint32_t *process_id);

ASTRA_EXTERN_C_END

#endif
