#include <astra/shell.h>

static size_t shell_strlen(const char *text)
{
    size_t length = 0u;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static int shell_equal(const char *left, const char *right)
{
    size_t index = 0u;
    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }
        ++index;
    }
    return left[index] == right[index];
}

static void shell_copy(char *output, const char *input, size_t length)
{
    size_t index;
    for (index = 0u; index < length; ++index) {
        output[index] = input[index];
    }
    output[length] = '\0';
}

void astra_shell_editor_init(astra_shell_editor_t *editor)
{
    size_t index;
    if (editor == NULL) {
        return;
    }
    editor->line[0] = '\0';
    editor->length = 0u;
    editor->cursor = 0u;
    editor->history_count = 0u;
    editor->history_head = 0u;
    editor->history_offset = 0u;
    editor->saved_line[0] = '\0';
    editor->saved_length = 0u;
    for (index = 0u; index < ASTRA_SHELL_HISTORY_CAPACITY; ++index) {
        editor->history[index][0] = '\0';
    }
}

static void editor_replace(astra_shell_editor_t *editor, const char *line)
{
    size_t length = shell_strlen(line);
    if (length >= ASTRA_SHELL_LINE_CAPACITY) {
        length = ASTRA_SHELL_LINE_CAPACITY - 1u;
    }
    shell_copy(editor->line, line, length);
    editor->length = length;
    editor->cursor = length;
}

static astra_shell_result_t editor_history(astra_shell_editor_t *editor, int older)
{
    size_t slot;
    if (older != 0) {
        if (editor->history_offset >= editor->history_count) {
            return ASTRA_SHELL_NO_CHANGE;
        }
        if (editor->history_offset == 0u) {
            shell_copy(editor->saved_line, editor->line, editor->length);
            editor->saved_length = editor->length;
        }
        ++editor->history_offset;
        slot = (editor->history_head + ASTRA_SHELL_HISTORY_CAPACITY
                - editor->history_offset) % ASTRA_SHELL_HISTORY_CAPACITY;
        editor_replace(editor, editor->history[slot]);
        return ASTRA_SHELL_CHANGED;
    }
    if (editor->history_offset == 0u) {
        return ASTRA_SHELL_NO_CHANGE;
    }
    --editor->history_offset;
    if (editor->history_offset == 0u) {
        shell_copy(editor->line, editor->saved_line, editor->saved_length);
        editor->length = editor->saved_length;
        editor->cursor = editor->length;
    } else {
        slot = (editor->history_head + ASTRA_SHELL_HISTORY_CAPACITY
                - editor->history_offset) % ASTRA_SHELL_HISTORY_CAPACITY;
        editor_replace(editor, editor->history[slot]);
    }
    return ASTRA_SHELL_CHANGED;
}

static astra_shell_result_t editor_complete(
    astra_shell_editor_t *editor, astra_shell_complete_fn complete, void *context)
{
    const char *matches[ASTRA_SHELL_COMPLETION_CAPACITY];
    size_t start = editor->cursor;
    size_t count;
    size_t common;
    size_t index;
    size_t replacement;
    size_t tail;
    if (complete == NULL) {
        return ASTRA_SHELL_NO_CHANGE;
    }
    while (start != 0u && editor->line[start - 1u] != ' '
           && editor->line[start - 1u] != '\t') {
        --start;
    }
    count = complete(context, &editor->line[start], editor->cursor - start,
                     matches, ASTRA_SHELL_COMPLETION_CAPACITY);
    if (count == 0u || count > ASTRA_SHELL_COMPLETION_CAPACITY) {
        return ASTRA_SHELL_NO_CHANGE;
    }
    common = shell_strlen(matches[0]);
    for (index = 1u; index < count; ++index) {
        size_t match_index = 0u;
        while (match_index < common && matches[index][match_index] != '\0'
               && matches[index][match_index] == matches[0][match_index]) {
            ++match_index;
        }
        common = match_index;
    }
    replacement = common;
    if (count == 1u) {
        ++replacement;
    }
    tail = editor->length - editor->cursor;
    if (start + replacement + tail >= ASTRA_SHELL_LINE_CAPACITY) {
        return ASTRA_SHELL_ERR_LIMIT;
    }
    for (index = tail + 1u; index != 0u; --index) {
        editor->line[start + replacement + index - 1u] =
            editor->line[editor->cursor + index - 1u];
    }
    for (index = 0u; index < common; ++index) {
        editor->line[start + index] = matches[0][index];
    }
    if (count == 1u) {
        editor->line[start + common] = ' ';
    }
    editor->length = start + replacement + tail;
    editor->cursor = start + replacement;
    return ASTRA_SHELL_CHANGED;
}

astra_shell_result_t astra_shell_editor_input(
    astra_shell_editor_t *editor, astra_shell_input_t input,
    astra_shell_complete_fn complete, void *complete_context)
{
    size_t index;
    if (editor == NULL) {
        return ASTRA_SHELL_ERR_INVALID;
    }
    switch (input.key) {
    case ASTRA_SHELL_KEY_CHARACTER:
        if (input.character < 0x20u || input.character == 0x7fu) {
            return ASTRA_SHELL_NO_CHANGE;
        }
        if (editor->length + 1u >= ASTRA_SHELL_LINE_CAPACITY) {
            return ASTRA_SHELL_ERR_LIMIT;
        }
        for (index = editor->length + 1u; index > editor->cursor; --index) {
            editor->line[index] = editor->line[index - 1u];
        }
        editor->line[editor->cursor++] = (char)input.character;
        ++editor->length;
        break;
    case ASTRA_SHELL_KEY_ENTER:
        return ASTRA_SHELL_SUBMIT;
    case ASTRA_SHELL_KEY_BACKSPACE:
        if (editor->cursor == 0u) return ASTRA_SHELL_NO_CHANGE;
        --editor->cursor;
        /* fall through */
    case ASTRA_SHELL_KEY_DELETE:
        if (editor->cursor == editor->length) return ASTRA_SHELL_NO_CHANGE;
        for (index = editor->cursor; index < editor->length; ++index) {
            editor->line[index] = editor->line[index + 1u];
        }
        --editor->length;
        break;
    case ASTRA_SHELL_KEY_LEFT:
        if (editor->cursor == 0u) return ASTRA_SHELL_NO_CHANGE;
        --editor->cursor;
        break;
    case ASTRA_SHELL_KEY_RIGHT:
        if (editor->cursor == editor->length) return ASTRA_SHELL_NO_CHANGE;
        ++editor->cursor;
        break;
    case ASTRA_SHELL_KEY_HOME:
        if (editor->cursor == 0u) return ASTRA_SHELL_NO_CHANGE;
        editor->cursor = 0u;
        break;
    case ASTRA_SHELL_KEY_END:
        if (editor->cursor == editor->length) return ASTRA_SHELL_NO_CHANGE;
        editor->cursor = editor->length;
        break;
    case ASTRA_SHELL_KEY_HISTORY_PREVIOUS:
        return editor_history(editor, 1);
    case ASTRA_SHELL_KEY_HISTORY_NEXT:
        return editor_history(editor, 0);
    case ASTRA_SHELL_KEY_COMPLETE:
        return editor_complete(editor, complete, complete_context);
    case ASTRA_SHELL_KEY_CANCEL:
        editor->line[0] = '\0';
        editor->length = 0u;
        editor->cursor = 0u;
        editor->history_offset = 0u;
        break;
    default:
        return ASTRA_SHELL_ERR_INVALID;
    }
    editor->history_offset = 0u;
    return ASTRA_SHELL_CHANGED;
}

void astra_shell_editor_commit(astra_shell_editor_t *editor)
{
    size_t previous;
    if (editor == NULL || editor->length == 0u) {
        return;
    }
    if (editor->history_count != 0u) {
        previous = (editor->history_head + ASTRA_SHELL_HISTORY_CAPACITY - 1u)
                   % ASTRA_SHELL_HISTORY_CAPACITY;
        if (shell_equal(editor->history[previous], editor->line)) {
            editor->line[0] = '\0';
            editor->length = 0u;
            editor->cursor = 0u;
            return;
        }
    }
    shell_copy(editor->history[editor->history_head], editor->line, editor->length);
    editor->history_head = (editor->history_head + 1u) % ASTRA_SHELL_HISTORY_CAPACITY;
    if (editor->history_count < ASTRA_SHELL_HISTORY_CAPACITY) {
        ++editor->history_count;
    }
    editor->line[0] = '\0';
    editor->length = 0u;
    editor->cursor = 0u;
    editor->history_offset = 0u;
}

astra_shell_result_t astra_shell_parse(const char *line, astra_shell_words_t *words)
{
    size_t input = 0u;
    size_t output = 0u;
    char quote = '\0';
    int in_word = 0;
    if (line == NULL || words == NULL) return ASTRA_SHELL_ERR_INVALID;
    words->argc = 0;
    while (line[input] != '\0') {
        char ch = line[input++];
        if (quote == '\0' && (ch == ' ' || ch == '\t')) {
            if (in_word != 0) {
                words->storage[output++] = '\0';
                in_word = 0;
            }
            continue;
        }
        if (ch == '\\') {
            if (line[input] == '\0') return ASTRA_SHELL_ERR_SYNTAX;
            ch = line[input++];
        } else if (ch == '\'' || ch == '"') {
            if (quote == '\0') {
                quote = ch;
                if (in_word == 0) {
                    if (words->argc >= (int)ASTRA_SHELL_ARG_CAPACITY) return ASTRA_SHELL_ERR_LIMIT;
                    words->argv[words->argc++] = &words->storage[output];
                    in_word = 1;
                }
                continue;
            }
            if (quote == ch) {
                quote = '\0';
                continue;
            }
        }
        if (in_word == 0) {
            if (words->argc >= (int)ASTRA_SHELL_ARG_CAPACITY) return ASTRA_SHELL_ERR_LIMIT;
            words->argv[words->argc++] = &words->storage[output];
            in_word = 1;
        }
        if (output + 1u >= ASTRA_SHELL_LINE_CAPACITY) return ASTRA_SHELL_ERR_LIMIT;
        words->storage[output++] = ch;
    }
    if (quote != '\0') return ASTRA_SHELL_ERR_SYNTAX;
    if (in_word != 0) words->storage[output++] = '\0';
    words->argv[words->argc] = NULL;
    return ASTRA_SHELL_OK;
}

static int prompt_append(char *output, size_t capacity, size_t *used, const char *text)
{
    size_t index = 0u;
    while (text[index] != '\0') {
        if (*used + 1u >= capacity) return 0;
        output[(*used)++] = text[index++];
    }
    return 1;
}

astra_shell_result_t astra_shell_format_prompt(
    char *output, size_t capacity, const char *format,
    const astra_shell_prompt_t *prompt)
{
    size_t used = 0u;
    size_t index = 0u;
    if (output == NULL || capacity == 0u || format == NULL || prompt == NULL)
        return ASTRA_SHELL_ERR_INVALID;
    while (format[index] != '\0') {
        const char *value = NULL;
        char literal[2];
        if (format[index] != '%') {
            literal[0] = format[index++]; literal[1] = '\0';
            value = literal;
        } else {
            ++index;
            switch (format[index]) {
            case 'u': value = prompt->user; break;
            case 'h': value = prompt->host; break;
            case 'd': value = prompt->directory; break;
            case '#': value = prompt->privileged != 0 ? "#" : ">"; break;
            case '%': value = "%"; break;
            case '\0': return ASTRA_SHELL_ERR_SYNTAX;
            default: return ASTRA_SHELL_ERR_SYNTAX;
            }
            ++index;
        }
        if (value == NULL || prompt_append(output, capacity, &used, value) == 0) {
            return ASTRA_SHELL_ERR_LIMIT;
        }
    }
    output[used] = '\0';
    return ASTRA_SHELL_OK;
}

astra_shell_result_t astra_shell_dispatch(
    const astra_shell_builtin_t *builtins, size_t builtin_count,
    void *context, const astra_shell_words_t *words, int *command_result)
{
    size_t index;
    if (builtins == NULL || words == NULL || command_result == NULL)
        return ASTRA_SHELL_ERR_INVALID;
    if (words->argc == 0) {
        *command_result = 0;
        return ASTRA_SHELL_OK;
    }
    for (index = 0u; index < builtin_count; ++index) {
        if (shell_equal(builtins[index].name, words->argv[0])) {
            *command_result = builtins[index].function(context, words->argc, words->argv);
            return ASTRA_SHELL_OK;
        }
    }
    return ASTRA_SHELL_ERR_NOT_FOUND;
}
