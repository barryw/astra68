#include <astra/shell.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void type(astra_shell_editor_t *editor, const char *text)
{
    while (*text != '\0') {
        astra_shell_input_t input = { ASTRA_SHELL_KEY_CHARACTER, (uint8_t)*text++ };
        assert(astra_shell_editor_input(editor, input, NULL, NULL) == ASTRA_SHELL_CHANGED);
    }
}

static size_t complete(void *context, const char *prefix, size_t prefix_length,
                       const char **matches, size_t capacity)
{
    static const char *const commands[] = { "mount", "move", "ps" };
    size_t count = 0u;
    size_t command;
    (void)context;
    for (command = 0u; command < 3u && count < capacity; ++command) {
        if (strncmp(commands[command], prefix, prefix_length) == 0) {
            matches[count++] = commands[command];
        }
    }
    return count;
}

static int echo_builtin(void *context, int argc, char *const argv[])
{
    int *seen = context;
    *seen = argc;
    return strcmp(argv[1], "hello world") == 0 ? 7 : -1;
}

static void test_editor(void)
{
    astra_shell_editor_t editor;
    astra_shell_input_t key = { ASTRA_SHELL_KEY_HOME, 0u };
    astra_shell_editor_init(&editor);
    type(&editor, "bc");
    assert(astra_shell_editor_input(&editor, key, NULL, NULL) == ASTRA_SHELL_CHANGED);
    key.key = ASTRA_SHELL_KEY_CHARACTER; key.character = 'a';
    assert(astra_shell_editor_input(&editor, key, NULL, NULL) == ASTRA_SHELL_CHANGED);
    assert(strcmp(editor.line, "abc") == 0);
    key.key = ASTRA_SHELL_KEY_END;
    assert(astra_shell_editor_input(&editor, key, NULL, NULL) == ASTRA_SHELL_CHANGED);
    key.key = ASTRA_SHELL_KEY_BACKSPACE;
    assert(astra_shell_editor_input(&editor, key, NULL, NULL) == ASTRA_SHELL_CHANGED);
    assert(strcmp(editor.line, "ab") == 0);
    astra_shell_editor_commit(&editor);
    type(&editor, "second");
    astra_shell_editor_commit(&editor);
    key.key = ASTRA_SHELL_KEY_HISTORY_PREVIOUS;
    assert(astra_shell_editor_input(&editor, key, NULL, NULL) == ASTRA_SHELL_CHANGED);
    assert(strcmp(editor.line, "second") == 0);
    assert(astra_shell_editor_input(&editor, key, NULL, NULL) == ASTRA_SHELL_CHANGED);
    assert(strcmp(editor.line, "ab") == 0);
    key.key = ASTRA_SHELL_KEY_HISTORY_NEXT;
    assert(astra_shell_editor_input(&editor, key, NULL, NULL) == ASTRA_SHELL_CHANGED);
    assert(strcmp(editor.line, "second") == 0);
}

static void test_completion(void)
{
    astra_shell_editor_t editor;
    astra_shell_input_t key = { ASTRA_SHELL_KEY_COMPLETE, 0u };
    astra_shell_editor_init(&editor);
    type(&editor, "mou");
    assert(astra_shell_editor_input(&editor, key, complete, NULL) == ASTRA_SHELL_CHANGED);
    assert(strcmp(editor.line, "mount ") == 0);
    astra_shell_editor_init(&editor);
    type(&editor, "m");
    assert(astra_shell_editor_input(&editor, key, complete, NULL) == ASTRA_SHELL_CHANGED);
    assert(strcmp(editor.line, "mo") == 0);
}

/*
 * Redirection is parsing, so it is proved here rather than on the machine.
 * The cases that matter are the ones a shell gets wrong: a `>` with no space
 * around it, a quoted `>` that is text, and a name that never arrives.
 */
/* The old calls, before variables existed: no lookup, so `$` is text. */
static astra_shell_result_t astra_shell_parse_plain(const char *line,
                                                    astra_shell_words_t *words)
{
    return astra_shell_parse(line, words, NULL, NULL);
}

static const char *lookup(void *context, const char *name, size_t length)
{
    return astra_shell_variable_get(context, name, length);
}

static void test_variables(void)
{
    astra_shell_variables_t variables;
    const char *name;
    const char *value;

    astra_shell_variables_init(&variables);
    assert(astra_shell_variable_get(&variables, "HOME", 4u) == NULL);
    assert(astra_shell_variable_set(&variables, "HOME", "WORK:") == ASTRA_SHELL_OK);
    assert(astra_shell_variable_set(&variables, "?", "0") == ASTRA_SHELL_OK);
    assert(strcmp(astra_shell_variable_get(&variables, "HOME", 4u), "WORK:") == 0);
    assert(strcmp(astra_shell_variable_get(&variables, "?", 1u), "0") == 0);
    /* A prefix is not a name: `HOM` must not find `HOME`. */
    assert(astra_shell_variable_get(&variables, "HOME", 3u) == NULL);

    /* Replacing keeps the arena packed rather than growing it. */
    assert(astra_shell_variable_set(&variables, "?", "7") == ASTRA_SHELL_OK);
    assert(strcmp(astra_shell_variable_get(&variables, "?", 1u), "7") == 0);
    assert(variables.count == 2u);
    assert(astra_shell_variable_at(&variables, 0u, &name, &value) == 1);
    assert(strcmp(name, "HOME") == 0 && strcmp(value, "WORK:") == 0);
    assert(astra_shell_variable_at(&variables, 2u, &name, &value) == 0);

    assert(astra_shell_variable_unset(&variables, "HOME") == ASTRA_SHELL_OK);
    assert(astra_shell_variable_get(&variables, "HOME", 4u) == NULL);
    assert(strcmp(astra_shell_variable_get(&variables, "?", 1u), "7") == 0);
    assert(astra_shell_variable_unset(&variables, "HOME") == ASTRA_SHELL_ERR_NOT_FOUND);

    /* A name with `=` could never be read back, so it is never stored. */
    assert(astra_shell_variable_set(&variables, "A=B", "x") == ASTRA_SHELL_ERR_INVALID);
    assert(astra_shell_variable_exportable("PATH") == 1);
    assert(astra_shell_variable_exportable("_a9") == 1);
    assert(astra_shell_variable_exportable("?") == 0);
    assert(astra_shell_variable_exportable("9lives") == 0);
}

static void test_expansion(void)
{
    astra_shell_variables_t variables;
    astra_shell_words_t words;

    astra_shell_variables_init(&variables);
    assert(astra_shell_variable_set(&variables, "?", "7") == ASTRA_SHELL_OK);
    assert(astra_shell_variable_set(&variables, "who", "a b") == ASTRA_SHELL_OK);

    assert(astra_shell_parse("echo $?", &words, lookup, &variables) == ASTRA_SHELL_OK);
    assert(words.argc == 2 && strcmp(words.argv[1], "7") == 0);
    assert(astra_shell_parse("echo ${who}x", &words, lookup, &variables) == ASTRA_SHELL_OK);
    assert(words.argc == 2 && strcmp(words.argv[1], "a bx") == 0);
    /* A value is one word however many spaces it holds. */
    assert(astra_shell_parse("echo $who", &words, lookup, &variables) == ASTRA_SHELL_OK);
    assert(words.argc == 2 && strcmp(words.argv[1], "a b") == 0);
    /* Double quotes expand, single quotes and a backslash do not. */
    assert(astra_shell_parse("echo \"$?\" '$?' \\$?", &words, lookup,
                             &variables) == ASTRA_SHELL_OK);
    assert(words.argc == 4);
    assert(strcmp(words.argv[1], "7") == 0);
    assert(strcmp(words.argv[2], "$?") == 0);
    assert(strcmp(words.argv[3], "$?") == 0);
    /* An unset name is nothing, and a `$` naming nothing is a dollar sign. */
    assert(astra_shell_parse("echo $nothing- $ x", &words, lookup,
                             &variables) == ASTRA_SHELL_OK);
    assert(words.argc == 4);
    assert(strcmp(words.argv[1], "-") == 0);
    assert(strcmp(words.argv[2], "$") == 0);
    assert(astra_shell_parse("echo ${who", &words, lookup,
                             &variables) == ASTRA_SHELL_ERR_SYNTAX);
    /* A redirect's name expands like anything else. */
    assert(astra_shell_parse("ls > $who", &words, lookup, &variables) == ASTRA_SHELL_OK);
    assert(words.redirect != NULL && strcmp(words.redirect, "a b") == 0);
}

static void test_assignments(void)
{
    astra_shell_variables_t variables;
    astra_shell_words_t words;

    astra_shell_variables_init(&variables);
    assert(astra_shell_variable_set(&variables, "pair", "a=b") == ASTRA_SHELL_OK);

    assert(astra_shell_parse("TZ=America/New_York", &words, lookup,
                             &variables) == ASTRA_SHELL_OK);
    assert(words.argc == 0 && words.assignments == 1);
    assert(strcmp(words.assignment[0], "TZ=America/New_York") == 0);

    /* Ahead of a command, both: one name set and one command to run. */
    assert(astra_shell_parse("A=1 B=2 date -u", &words, lookup,
                             &variables) == ASTRA_SHELL_OK);
    assert(words.assignments == 2 && words.argc == 2);
    assert(strcmp(words.assignment[1], "B=2") == 0);
    assert(strcmp(words.argv[0], "date") == 0);

    /* After the command it is an ordinary argument. */
    assert(astra_shell_parse("rm a=b", &words, lookup, &variables) == ASTRA_SHELL_OK);
    assert(words.assignments == 0 && words.argc == 2);
    assert(strcmp(words.argv[1], "a=b") == 0);

    /* A `=` that arrived inside a value never assigns. */
    assert(astra_shell_parse("$pair", &words, lookup, &variables) == ASTRA_SHELL_OK);
    assert(words.assignments == 0 && words.argc == 1);
    assert(strcmp(words.argv[0], "a=b") == 0);

    /* Quoting the value still assigns; quoting the `=` does not. */
    assert(astra_shell_parse("A=\"1 2\"", &words, lookup, &variables) == ASTRA_SHELL_OK);
    assert(words.assignments == 1 && words.argc == 0);
    assert(strcmp(words.assignment[0], "A=1 2") == 0);
    assert(astra_shell_parse("\"A=1\"", &words, lookup, &variables) == ASTRA_SHELL_OK);
    assert(words.assignments == 0 && words.argc == 1);
    assert(strcmp(words.argv[0], "A=1") == 0);
    assert(astra_shell_parse("A\\=1", &words, lookup, &variables) == ASTRA_SHELL_OK);
    assert(words.assignments == 0 && words.argc == 1);

    /* And a word that is not a name is a command, `=` or not. */
    assert(astra_shell_parse("9x=1", &words, lookup, &variables) == ASTRA_SHELL_OK);
    assert(words.assignments == 0 && words.argc == 1);
    assert(astra_shell_parse("=1", &words, lookup, &variables) == ASTRA_SHELL_OK);
    assert(words.assignments == 0 && words.argc == 1);
}

static void test_parse_redirect(void)
{
    astra_shell_words_t words;

    assert(astra_shell_parse_plain("ls -l > out.txt", &words) == ASTRA_SHELL_OK);
    assert(words.argc == 2);
    assert(strcmp(words.argv[0], "ls") == 0);
    assert(strcmp(words.argv[1], "-l") == 0);
    assert(words.redirect != NULL && strcmp(words.redirect, "out.txt") == 0);
    assert(words.redirect_append == 0);

    /* No space is the same line. */
    assert(astra_shell_parse_plain("ls>out.txt", &words) == ASTRA_SHELL_OK);
    assert(words.argc == 1 && strcmp(words.argv[0], "ls") == 0);
    assert(words.redirect != NULL && strcmp(words.redirect, "out.txt") == 0);

    assert(astra_shell_parse_plain("date >> log", &words) == ASTRA_SHELL_OK);
    assert(words.argc == 1);
    assert(words.redirect != NULL && strcmp(words.redirect, "log") == 0);
    assert(words.redirect_append == 1);

    /* A name may be quoted, and a quoted `>` is text. */
    assert(astra_shell_parse_plain("ls > \"a b\"", &words) == ASTRA_SHELL_OK);
    assert(words.redirect != NULL && strcmp(words.redirect, "a b") == 0);
    assert(astra_shell_parse_plain("echo \">\" x", &words) == ASTRA_SHELL_OK);
    assert(words.redirect == NULL);
    assert(words.argc == 3 && strcmp(words.argv[1], ">") == 0);
    assert(astra_shell_parse_plain("echo \\> x", &words) == ASTRA_SHELL_OK);
    assert(words.redirect == NULL && words.argc == 3);

    /* And the line that says nothing complete. */
    assert(astra_shell_parse_plain("ls >", &words) == ASTRA_SHELL_ERR_SYNTAX);
    assert(astra_shell_parse_plain("ls > a > b", &words) == ASTRA_SHELL_ERR_SYNTAX);
    assert(astra_shell_parse_plain("ls > > b", &words) == ASTRA_SHELL_ERR_SYNTAX);

    /* An ordinary line still says it redirects nowhere. */
    assert(astra_shell_parse_plain("ls -l", &words) == ASTRA_SHELL_OK);
    assert(words.redirect == NULL && words.redirect_append == 0);
}

static void test_parse_prompt_dispatch(void)
{
    astra_shell_words_t words;
    astra_shell_prompt_t prompt = { "barry", "astra", "SYS:Tools", 0 };
    astra_shell_builtin_t builtins[] = { { "echo", echo_builtin } };
    char output[64];
    int seen = 0;
    int result = 0;
    assert(astra_shell_parse_plain("echo 'hello world' escaped\\ value", &words) == ASTRA_SHELL_OK);
    assert(words.argc == 3);
    assert(strcmp(words.argv[1], "hello world") == 0);
    assert(strcmp(words.argv[2], "escaped value") == 0);
    assert(astra_shell_parse_plain("echo 'unterminated", &words) == ASTRA_SHELL_ERR_SYNTAX);
    assert(astra_shell_format_prompt(output, sizeof(output), "%u@%h:%d %# ", &prompt)
           == ASTRA_SHELL_OK);
    assert(strcmp(output, "barry@astra:SYS:Tools > ") == 0);
    assert(astra_shell_parse_plain("echo 'hello world'", &words) == ASTRA_SHELL_OK);
    assert(astra_shell_dispatch(builtins, 1u, &seen, &words, &result) == ASTRA_SHELL_OK);
    assert(seen == 2 && result == 7);
}

static void test_vim_arguments(void)
{
    astra_shell_words_t words;
    static const char *const expected[] = {
        "vim", "-R", "+42", "--cmd", "set number", "--",
        "WORK:notes.txt"
    };

    assert(astra_shell_parse_plain(
               "vim -R +42 --cmd \"set number\" -- WORK:notes.txt",
               &words) == ASTRA_SHELL_OK);
    assert(words.argc == (int)(sizeof(expected) / sizeof(expected[0])));
    for (int index = 0; index < words.argc; ++index)
        assert(strcmp(words.argv[index], expected[index]) == 0);
    assert(words.argv[words.argc] == NULL);
}

int main(void)
{
    test_editor();
    test_completion();
    test_parse_prompt_dispatch();
    test_parse_redirect();
    test_variables();
    test_expansion();
    test_assignments();
    test_vim_arguments();
    puts("astra shell core: PASS");
    return 0;
}
