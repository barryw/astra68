#ifndef ASTRA_COMMAND_H
#define ASTRA_COMMAND_H

/** @file command.h @brief Caller-owned command responder chain. */

#include <stdint.h>
#include <astra/input_modifiers.h>
#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/** Caller-supplied argument document for one command invocation. */
typedef struct AstraCommandArguments {
    uint32_t type; /**< Application-defined argument schema; zero means none. */
    uint32_t version; /**< Version of that schema; zero means none. */
    const void *bytes; /**< Schema-validated document, borrowed for the call. */
    uint32_t length; /**< Byte length of @p bytes. */
} AstraCommandArguments;

/** A command's current enabled/checked state and display label. */
typedef struct AstraCommandState {
    const char *label; /**< Borrowed display label. */
    uint32_t label_length; /**< Byte length of @p label; zero when @p label is NULL. */
    uint8_t enabled; /**< Nonzero if the command may currently be invoked. */
    uint8_t checked; /**< Nonzero if the command is shown as checked/active. */
} AstraCommandState;

/** Report a command's current state. @param context AstraCommand::context.
 * @param state Receives the command's state. @return ASTRA_OK on success,
 * otherwise an error; a non-ASTRA_OK result clears @p state. */
typedef AstraResult (*AstraCommandQuery)(void *context,
                                         AstraCommandState *state);
/** Perform a command. @param context AstraCommand::context. @param args
 * Borrowed call arguments, matching the command's declared argument schema,
 * or NULL when it declares none. @return ASTRA_OK on success, otherwise an
 * error. */
typedef AstraResult (*AstraCommandInvoke)(void *context,
                                          const AstraCommandArguments *args);

/** One action a responder exposes to menus, shortcuts and other clients. */
typedef struct AstraCommand {
    const char *id; /**< Stable, counted application-local identifier. */
    uint32_t id_length; /**< Byte length of @p id. */
    uint32_t argument_type; /**< Required AstraCommandArguments::type, or zero for none. */
    uint32_t argument_version; /**< Required AstraCommandArguments::version when @p argument_type is set. */
    AstraCommandQuery query; /**< Reports enabled/checked state and label. */
    AstraCommandInvoke invoke; /**< Performs the command. */
    void *context; /**< Opaque value passed to @p query and @p invoke. */
} AstraCommand;

/** One responder's commands, chained toward the workspace. */
typedef struct AstraCommandScope {
    const struct AstraCommandScope *parent; /**< Next responder. */
    const AstraCommand *commands; /**< This scope's commands, or NULL if @p command_count is zero. */
    uint32_t command_count; /**< Number of entries in @p commands. */
} AstraCommandScope;

/** Primary command modifier; accepts Control, GUI or Meta. */
#define ASTRA_COMMAND_SHORTCUT_PRIMARY UINT32_C(1)
/** Either Shift key. */
#define ASTRA_COMMAND_SHORTCUT_SHIFT   UINT32_C(2)
/** Either Alt key. */
#define ASTRA_COMMAND_SHORTCUT_ALT     UINT32_C(4)

/** Caller-owned key binding for a command in the responder chain. */
typedef struct AstraCommandShortcut {
    uint32_t usage; /**< USB HID key usage. */
    uint32_t modifiers; /**< Exact normalized modifier combination. */
    const char *id; /**< Command ID borrowed for dispatch. */
    uint32_t id_length; /**< Byte length of @p id. */
} AstraCommandShortcut;

/** Resolve from focused scope toward the workspace. A declaration shadows
 * ancestors even when disabled, so a disabled local action cannot trigger a
 * different ancestor action with the same ID.
 * @param scope Innermost (focused) responder scope.
 * @param id Command identifier, not necessarily NUL-terminated.
 * @param id_length Byte length of @p id.
 * @param state Receives the resolved command's state.
 * @return ASTRA_OK on success; ASTRA_ERROR_NOT_PRESENT if no scope declares
 * @p id; ASTRA_ERROR_INVALID_ARGUMENT for a malformed call or chain.
 */
AstraResult astra_interface_command_query(const AstraCommandScope *scope,
                                           const char *id,
                                           uint32_t id_length,
                                           AstraCommandState *state);

/** Invoke the same resolved action used by menus, shortcuts and other clients.
 * Blocking work is the handler's responsibility to start asynchronously.
 * @param scope Innermost (focused) responder scope.
 * @param id Command identifier, not necessarily NUL-terminated.
 * @param id_length Byte length of @p id.
 * @param args Call arguments matching the resolved command's declared
 * schema, or NULL when it declares none.
 * @return ASTRA_OK on success; ASTRA_ERROR_NOT_PRESENT if no scope declares
 * @p id; ASTRA_ERROR_PERMISSION if the resolved command is disabled;
 * ASTRA_ERROR_INVALID_ARGUMENT for a malformed call, chain, or an @p args
 * schema mismatch; otherwise the handler's own error.
 */
AstraResult astra_interface_command_invoke(
    const AstraCommandScope *scope, const char *id, uint32_t id_length,
    const AstraCommandArguments *args);

/** Dispatch one non-repeating key-down to the same command used by menus.
 * Caps Lock is ignored. Disabled commands consume their binding without
 * invoking anything. Unknown keys do not consume an event.
 * @param scope Innermost command responder scope.
 * @param usage USB HID usage from a key-down event.
 * @param input_modifiers Input event's ASTRA_INPUT_MOD_* bit mask.
 * @param bindings Borrowed shortcut table.
 * @param binding_count Number of shortcut bindings.
 * @param handled Set to one for a matching binding, zero otherwise.
 * @return ASTRA_OK for an unmatched, disabled, or successful binding;
 * otherwise validation or command error.
 */
AstraResult astra_interface_command_shortcut(
    const AstraCommandScope *scope, uint32_t usage, uint32_t input_modifiers,
    const AstraCommandShortcut *bindings, uint32_t binding_count,
    uint8_t *handled);

ASTRA_EXTERN_C_END

#endif
