#ifndef ASTRA_APPLICATION_H
#define ASTRA_APPLICATION_H

/** @file application.h @brief Launch installed application bundles. */

#include <stdint.h>

#include <astra/application_service.h>
#include <astra/attributes.h>
#include <astra/resource.h>
#include <astra/string.h>
#include <astra/types.h>
#include <astra/volume_abi.h>

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

/** Launch a bundle with one exact volume target as option + decimal identity.
 * The receiving application parses epoch, ID, and generation with
 * astra_volume_reference_parse and checks them against a current catalog.
 * This helper shares argument ordering across Desktop and file browsers.
 * @param launcher Launcher service handle.
 * @param bundle_path Bundle path beneath APPS:.
 * @param path_length Bytes in bundle_path.
 * @param source Origin of launch request.
 * @param option Receiver's volume-target option, for example `--target`.
 * @param volume Exact nonzero provider epoch, ID, and generation.
 * @param process_id Receives new process PID.
 * @return ASTRA_OK on startup, or validation/launch error.
 */
static inline AstraResult astra_application_launch_for_volume(
    AstraHandle launcher, const char *bundle_path, uint16_t path_length,
    AstraLaunchSource source, const char *option,
    const AstraVolumeReference *volume, uint32_t *process_id)
{
    char epoch[21], id[21], generation[21];
    char *const text[] = {epoch, id, generation};
    const uint64_t values[] = {volume != NULL ? volume->epoch : 0u,
        volume != NULL ? volume->id : 0u,
        volume != NULL ? volume->generation : 0u};
    if (option == NULL || *option == '\0' || volume == NULL ||
        values[0] == 0u || values[1] == 0u || values[2] == 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t at = 0u; at < 3u; ++at) {
        AstraString number;
        astra_string_init(&number, text[at], 21u);
        if (!astra_string_append_u64(&number, values[at]))
            return ASTRA_ERROR_NO_RESOURCES;
    }
    const char *const arguments[] = {option, epoch, id, generation};
    return astra_application_launch_with_arguments(launcher, bundle_path,
        path_length, source, arguments, 4u, process_id);
}

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
