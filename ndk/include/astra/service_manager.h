#ifndef ASTRA_SERVICE_MANAGER_H
#define ASTRA_SERVICE_MANAGER_H

/** @file service_manager.h @brief Canonical Astra service lifecycle API. */

#include <stdint.h>

#include <astra/attributes.h>
#include <astra/resource.h>
#include <astra/service_manager_abi.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/**
 * Test whether a name is a valid stable service identifier.
 * @param name NUL-terminated UTF-8 service name.
 * @return Nonzero when valid; otherwise zero.
 */
int astra_service_name_valid(const char *name);

/**
 * Initialize a definition with manual, enabled, no-restart defaults.
 * @param definition Receives initialized definition state.
 * @param name Stable UTF-8 service identifier.
 * @param executable Assign-qualified executable path.
 * @param runs_on ASTRA_SERVICE_RUNS_ON_* execution location.
 * @return ASTRA_RESULT_* status.
 */
ASTRA_NODISCARD AstraResult astra_service_definition_init(
    AstraServiceDefinition *definition, const char *name,
    const char *executable, uint32_t runs_on);

/**
 * Append one UTF-8 argv entry to a definition.
 * @param definition Definition being constructed.
 * @param argument NUL-terminated argument text.
 * @return ASTRA_RESULT_* status.
 */
ASTRA_NODISCARD AstraResult astra_service_definition_add_argument(
    AstraServiceDefinition *definition, const char *argument);

/**
 * Append one launch capability to a definition.
 * @param definition Definition being constructed.
 * @param name Capability or namespace name.
 * @param rights ASTRA_RIGHT_* mask granted to the launched service.
 * @param is_namespace Nonzero when @p name identifies a namespace.
 * @return ASTRA_RESULT_* status.
 */
ASTRA_NODISCARD AstraResult astra_service_definition_add_grant(
    AstraServiceDefinition *definition, const char *name, uint32_t rights,
    int is_namespace);

/**
 * Append one capability published when the service becomes ready.
 * @param definition Definition being constructed.
 * @param name Published capability name.
 * @param rights ASTRA_RIGHT_* mask exposed to consumers.
 * @return ASTRA_RESULT_* status.
 */
ASTRA_NODISCARD AstraResult astra_service_definition_add_publication(
    AstraServiceDefinition *definition, const char *name, uint32_t rights);

/**
 * Append one required published capability.
 * @param definition Definition being constructed.
 * @param name Required published capability name.
 * @return ASTRA_RESULT_* status.
 */
ASTRA_NODISCARD AstraResult astra_service_definition_add_dependency(
    AstraServiceDefinition *definition, const char *name);

/**
 * Validate a complete service definition before installation.
 * @param definition Definition to validate.
 * @return ASTRA_RESULT_* status.
 */
ASTRA_NODISCARD AstraResult astra_service_definition_validate(
    const AstraServiceDefinition *definition);

/**
 * Return one configured service and the cursor for the next.
 * Start with ASTRA_SERVICE_LIST_CURSOR_INIT and stop when the returned
 * cursor's source is ASTRA_SERVICE_LIST_SOURCE_DONE.
 * @param manager Service-manager capability handle.
 * @param cursor Cursor selecting the next result.
 * @param info Receives service identity and runtime state.
 * @param next_cursor Receives the cursor for the following result.
 * @return ASTRA_RESULT_* status.
 */
ASTRA_NODISCARD AstraResult astra_service_list(
    AstraHandle manager, const AstraServiceListCursor *cursor,
    AstraServiceInfo *info, AstraServiceListCursor *next_cursor);

/**
 * Return runtime information and the complete stored definition.
 * @param manager Service-manager capability handle.
 * @param name Stable service identifier.
 * @param info Receives service identity and runtime state.
 * @param definition Receives the stored service definition.
 * @return ASTRA_RESULT_* status.
 */
ASTRA_NODISCARD AstraResult astra_service_inspect(
    AstraHandle manager, const char *name, AstraServiceInfo *info,
    AstraServiceDefinition *definition);

/**
 * Atomically install a validated service definition.
 * @param manager Service-manager capability handle.
 * @param definition Complete definition to install.
 * @return ASTRA_RESULT_* status.
 */
ASTRA_NODISCARD AstraResult astra_service_add(
    AstraHandle manager, const AstraServiceDefinition *definition);

/**
 * Apply one lifecycle operation to a named service.
 * @param manager Service-manager capability handle.
 * @param operation ASTRA_SERVICE_OPERATION_* value.
 * @param name Stable service identifier.
 * @param info Receives state after the operation.
 * @return ASTRA_RESULT_* status.
 */
ASTRA_NODISCARD AstraResult astra_service_control(
    AstraHandle manager, uint32_t operation, const char *name,
    AstraServiceInfo *info);

/** Request normal DE25 shutdown. Acceptance does not mean completion;
 * applications may still cancel, and force shutdown is a separate action.
 * @param manager Service-manager capability handle.
 * @return ASTRA_RESULT_* status for accepting the request.
 */
ASTRA_NODISCARD AstraResult astra_system_shutdown_request(
    AstraHandle manager);

/** Request normal DE25 restart. Uses the same vetoable process and filesystem
 * shutdown sequence as Shut Down. Acceptance does not mean completion.
 * @param manager Service-manager capability handle.
 * @return ASTRA_RESULT_* status for accepting the request.
 */
ASTRA_NODISCARD AstraResult astra_system_restart_request(
    AstraHandle manager);

ASTRA_EXTERN_C_END

#endif
