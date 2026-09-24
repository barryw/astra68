#ifndef ASTRA_COMMAND_H
#define ASTRA_COMMAND_H

/** @file command.h @brief Caller-owned command responder chain. */

#include <stdint.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

typedef struct AstraCommandArguments {
    uint32_t type; /**< Application-defined argument schema; zero means none. */
    uint32_t version; /**< Version of that schema; zero means none. */
    const void *bytes; /**< Schema-validated document, borrowed for the call. */
    uint32_t length;
} AstraCommandArguments;

typedef struct AstraCommandState {
    const char *label; /**< Borrowed display label. */
    uint32_t label_length;
    uint8_t enabled;
    uint8_t checked;
} AstraCommandState;

typedef AstraResult (*AstraCommandQuery)(void *context,
                                         AstraCommandState *state);
typedef AstraResult (*AstraCommandInvoke)(void *context,
                                          const AstraCommandArguments *args);

typedef struct AstraCommand {
    const char *id; /**< Stable, counted application-local identifier. */
    uint32_t id_length;
    uint32_t argument_type;
    uint32_t argument_version;
    AstraCommandQuery query;
    AstraCommandInvoke invoke;
    void *context;
} AstraCommand;

typedef struct AstraCommandScope {
    const struct AstraCommandScope *parent; /**< Next responder. */
    const AstraCommand *commands;
    uint32_t command_count;
} AstraCommandScope;

/** Resolve from focused scope toward the workspace. A declaration shadows
 * ancestors even when disabled, so a disabled local action cannot trigger a
 * different ancestor action with the same ID. */
AstraResult astra_interface_command_query(const AstraCommandScope *scope,
                                           const char *id,
                                           uint32_t id_length,
                                           AstraCommandState *state);

/** Invoke the same resolved action used by menus, shortcuts and other clients.
 * Blocking work is the handler's responsibility to start asynchronously. */
AstraResult astra_interface_command_invoke(
    const AstraCommandScope *scope, const char *id, uint32_t id_length,
    const AstraCommandArguments *args);

ASTRA_EXTERN_C_END

#endif
