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

static void test_parse_prompt_dispatch(void)
{
    astra_shell_words_t words;
    astra_shell_prompt_t prompt = { "barry", "astra", "SYS:Tools", 0 };
    astra_shell_builtin_t builtins[] = { { "echo", echo_builtin } };
    char output[64];
    int seen = 0;
    int result = 0;
    assert(astra_shell_parse("echo 'hello world' escaped\\ value", &words) == ASTRA_SHELL_OK);
    assert(words.argc == 3);
    assert(strcmp(words.argv[1], "hello world") == 0);
    assert(strcmp(words.argv[2], "escaped value") == 0);
    assert(astra_shell_parse("echo 'unterminated", &words) == ASTRA_SHELL_ERR_SYNTAX);
    assert(astra_shell_format_prompt(output, sizeof(output), "%u@%h:%d %# ", &prompt)
           == ASTRA_SHELL_OK);
    assert(strcmp(output, "barry@astra:SYS:Tools > ") == 0);
    assert(astra_shell_parse("echo 'hello world'", &words) == ASTRA_SHELL_OK);
    assert(astra_shell_dispatch(builtins, 1u, &seen, &words, &result) == ASTRA_SHELL_OK);
    assert(seen == 2 && result == 7);
}

int main(void)
{
    test_editor();
    test_completion();
    test_parse_prompt_dispatch();
    puts("astra shell core: PASS");
    return 0;
}
