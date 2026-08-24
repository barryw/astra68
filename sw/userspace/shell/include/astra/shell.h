#ifndef ASTRA_SHELL_H
#define ASTRA_SHELL_H

#include <stddef.h>
#include <stdint.h>

#include <astra/process.h>

#define ASTRA_SHELL_LINE_CAPACITY 512u
#define ASTRA_SHELL_HISTORY_CAPACITY 32u
#define ASTRA_SHELL_ARG_CAPACITY 32u
#define ASTRA_SHELL_COMPLETION_CAPACITY 32u
/*
 * The variable arena. One block of `NAME\0VALUE\0` pairs rather than a table
 * of fixed slots: a machine with 128 MB and no allocator in this library still
 * should not decide in advance that a name is 32 bytes and there are 16 of
 * them. It is also the shape a launch wants, so exporting is a copy and not a
 * second representation.
 */
#define ASTRA_SHELL_VARIABLE_BYTES ASTRA_LAUNCH_ENVIRONMENT_BYTES

typedef enum astra_shell_result {
    ASTRA_SHELL_OK = 0,
    ASTRA_SHELL_SUBMIT = 1,
    ASTRA_SHELL_CHANGED = 2,
    ASTRA_SHELL_NO_CHANGE = 3,
    ASTRA_SHELL_ERR_INVALID = -1,
    ASTRA_SHELL_ERR_LIMIT = -2,
    ASTRA_SHELL_ERR_SYNTAX = -3,
    ASTRA_SHELL_ERR_NOT_FOUND = -4
} astra_shell_result_t;

typedef enum astra_shell_key {
    ASTRA_SHELL_KEY_CHARACTER,
    ASTRA_SHELL_KEY_ENTER,
    ASTRA_SHELL_KEY_BACKSPACE,
    ASTRA_SHELL_KEY_DELETE,
    ASTRA_SHELL_KEY_LEFT,
    ASTRA_SHELL_KEY_RIGHT,
    ASTRA_SHELL_KEY_HOME,
    ASTRA_SHELL_KEY_END,
    ASTRA_SHELL_KEY_HISTORY_PREVIOUS,
    ASTRA_SHELL_KEY_HISTORY_NEXT,
    ASTRA_SHELL_KEY_COMPLETE,
    ASTRA_SHELL_KEY_CANCEL
} astra_shell_key_t;

typedef struct astra_shell_input {
    astra_shell_key_t key;
    uint8_t character;
} astra_shell_input_t;

typedef struct astra_shell_editor {
    char line[ASTRA_SHELL_LINE_CAPACITY];
    size_t length;
    size_t cursor;
    char history[ASTRA_SHELL_HISTORY_CAPACITY][ASTRA_SHELL_LINE_CAPACITY];
    size_t history_count;
    size_t history_head;
    size_t history_offset;
    char saved_line[ASTRA_SHELL_LINE_CAPACITY];
    size_t saved_length;
} astra_shell_editor_t;

typedef size_t (*astra_shell_complete_fn)(
    void *context, const char *prefix, size_t prefix_length,
    const char **matches, size_t match_capacity);

/*
 * The shell's variables, and a child's environment.
 *
 * `$?` lives here like any other name, so the expander has no special case and
 * `set` lists it. It is not exported: only a name a program could have read
 * from its own environment -- `[A-Za-z_][A-Za-z0-9_]*` -- crosses a launch,
 * which is the same rule that makes `?` unexportable everywhere else.
 */
typedef struct astra_shell_variables {
    char storage[ASTRA_SHELL_VARIABLE_BYTES];
    size_t length;
    size_t count;
} astra_shell_variables_t;

void astra_shell_variables_init(astra_shell_variables_t *variables);
/* Replaces a name that is there, appends one that is not. */
astra_shell_result_t astra_shell_variable_set(
    astra_shell_variables_t *variables, const char *name, const char *value);
astra_shell_result_t astra_shell_variable_unset(
    astra_shell_variables_t *variables, const char *name);
/*
 * `name` need not be NUL-terminated: the expander has a pointer into the line
 * and a length, and copying it out to ask would be a buffer this does not need.
 */
const char *astra_shell_variable_get(const astra_shell_variables_t *variables,
                                     const char *name, size_t length);
/* Walks the arena in insertion order. Zero when `index` is past the end. */
int astra_shell_variable_at(const astra_shell_variables_t *variables,
                            size_t index, const char **name,
                            const char **value);
/* Non-zero when a program could have read this name from its environment. */
int astra_shell_variable_exportable(const char *name);

/*
 * A name in a line, answered by whoever holds the variables.
 *
 * `name` is not NUL-terminated. NULL from a lookup is an unset name, which
 * expands to nothing -- the same answer every shell gives, and the reason
 * `echo $nothing` prints a blank line rather than failing.
 */
typedef const char *(*astra_shell_value_fn)(void *context, const char *name,
                                            size_t length);

typedef struct astra_shell_words {
    int argc;
    char *argv[ASTRA_SHELL_ARG_CAPACITY];
    /*
     * Where the command's output goes, when the line said so. `>` truncates
     * and `>>` keeps what is there; NULL is the ordinary case and means the
     * terminal. It is one name, not a list: a second `>` on a line is a
     * syntax error rather than a silent winner, because a reader cannot tell
     * from the text which of two names the bytes went to.
     *
     * Only the output. `2>` and `<` are not parsed, so those characters are
     * still ordinary text -- an omission that says so rather than one that
     * half-works.
     */
    char *redirect;
    int redirect_append;
    /*
     * `NAME=VALUE` words at the head of the line, which are assignments and
     * not arguments. `argv` begins after them, so `FOO=bar ls` runs `ls` with
     * one argument and `FOO=bar` on its own runs nothing and sets a name.
     */
    int assignments;
    char *assignment[ASTRA_SHELL_ARG_CAPACITY];
    char storage[ASTRA_SHELL_LINE_CAPACITY];
} astra_shell_words_t;

typedef struct astra_shell_prompt {
    const char *user;
    const char *host;
    const char *directory;
    int privileged;
} astra_shell_prompt_t;

typedef int (*astra_shell_builtin_fn)(void *context, int argc, char *const argv[]);

typedef struct astra_shell_builtin {
    const char *name;
    astra_shell_builtin_fn function;
} astra_shell_builtin_t;

void astra_shell_editor_init(astra_shell_editor_t *editor);
astra_shell_result_t astra_shell_editor_input(
    astra_shell_editor_t *editor, astra_shell_input_t input,
    astra_shell_complete_fn complete, void *complete_context);
void astra_shell_editor_commit(astra_shell_editor_t *editor);
/*
 * Splits a line into words, expanding `$NAME` and `${NAME}` through `value`.
 * A NULL lookup leaves `$` as ordinary text rather than expanding every name
 * to nothing, because a shell with no variables should print what was typed.
 */
astra_shell_result_t astra_shell_parse(const char *line,
                                       astra_shell_words_t *words,
                                       astra_shell_value_fn value,
                                       void *value_context);
astra_shell_result_t astra_shell_format_prompt(
    char *output, size_t capacity, const char *format,
    const astra_shell_prompt_t *prompt);
astra_shell_result_t astra_shell_dispatch(
    const astra_shell_builtin_t *builtins, size_t builtin_count,
    void *context, const astra_shell_words_t *words, int *command_result);

#endif
