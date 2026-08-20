#ifndef ASTRA_SHELL_H
#define ASTRA_SHELL_H

#include <stddef.h>
#include <stdint.h>

#define ASTRA_SHELL_LINE_CAPACITY 512u
#define ASTRA_SHELL_HISTORY_CAPACITY 32u
#define ASTRA_SHELL_ARG_CAPACITY 32u
#define ASTRA_SHELL_COMPLETION_CAPACITY 32u

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
astra_shell_result_t astra_shell_parse(const char *line, astra_shell_words_t *words);
astra_shell_result_t astra_shell_format_prompt(
    char *output, size_t capacity, const char *format,
    const astra_shell_prompt_t *prompt);
astra_shell_result_t astra_shell_dispatch(
    const astra_shell_builtin_t *builtins, size_t builtin_count,
    void *context, const astra_shell_words_t *words, int *command_result);

#endif
