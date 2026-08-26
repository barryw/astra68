#include <astra/shell.h>
#include <astra/bytes.h>

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
    size_t length = strlen(line);
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
    common = strlen(matches[0]);
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
        if (strcmp(editor->history[previous], editor->line) == 0) {
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

/*
 * The variable arena.
 *
 * `NAME\0VALUE\0` pairs, packed, in the order they were first set. Setting a
 * name that is there removes its pair and appends the new one, so a value that
 * grows costs a move of what follows it and never a reallocation -- there is
 * no allocator in this library and there should not need to be one for a
 * handful of names.
 */
static int name_equal(const char *left, const char *right, size_t length)
{
    size_t index;

    for (index = 0u; index < length; ++index) {
        if (left[index] == '\0' || left[index] != right[index]) {
            return 0;
        }
    }
    return left[length] == '\0';
}

void astra_shell_variables_init(astra_shell_variables_t *variables)
{
    if (variables == NULL) {
        return;
    }
    variables->length = 0u;
    variables->count = 0u;
}

int astra_shell_variable_at(const astra_shell_variables_t *variables,
                            size_t index, const char **name,
                            const char **value)
{
    size_t at = 0u;
    size_t seen = 0u;

    if (variables == NULL || name == NULL || value == NULL) {
        return 0;
    }
    while (at < variables->length) {
        const char *pair_name = &variables->storage[at];
        const char *pair_value;

        at += strlen(pair_name) + 1u;
        pair_value = &variables->storage[at];
        at += strlen(pair_value) + 1u;
        if (seen == index) {
            *name = pair_name;
            *value = pair_value;
            return 1;
        }
        ++seen;
    }
    return 0;
}

const char *astra_shell_variable_get(const astra_shell_variables_t *variables,
                                     const char *name, size_t length)
{
    size_t at = 0u;

    if (variables == NULL || name == NULL) {
        return NULL;
    }
    while (at < variables->length) {
        const char *pair_name = &variables->storage[at];
        size_t name_length = strlen(pair_name);

        if (name_equal(pair_name, name, length)) {
            return &variables->storage[at + name_length + 1u];
        }
        at += name_length + 1u;
        at += strlen(&variables->storage[at]) + 1u;
    }
    return NULL;
}

astra_shell_result_t astra_shell_variable_unset(
    astra_shell_variables_t *variables, const char *name)
{
    size_t at = 0u;

    if (variables == NULL || name == NULL) {
        return ASTRA_SHELL_ERR_INVALID;
    }
    while (at < variables->length) {
        const char *pair_name = &variables->storage[at];
        size_t name_length = strlen(pair_name);
        size_t pair = name_length + 1u;

        pair += strlen(&variables->storage[at + pair]) + 1u;
        if (name_equal(pair_name, name, strlen(name))) {
            size_t index;

            for (index = at + pair; index < variables->length; ++index) {
                variables->storage[index - pair] = variables->storage[index];
            }
            variables->length -= pair;
            --variables->count;
            return ASTRA_SHELL_OK;
        }
        at += pair;
    }
    return ASTRA_SHELL_ERR_NOT_FOUND;
}

astra_shell_result_t astra_shell_variable_set(
    astra_shell_variables_t *variables, const char *name, const char *value)
{
    size_t name_length;
    size_t value_length;
    size_t index;

    if (variables == NULL || name == NULL || value == NULL ||
        name[0] == '\0') {
        return ASTRA_SHELL_ERR_INVALID;
    }
    /*
     * A name with `=` in it could never be read back, because the arena and a
     * child's environment both separate on it. Refusing is the only honest
     * answer; storing it would be a name nothing can name.
     */
    for (index = 0u; name[index] != '\0'; ++index) {
        if (name[index] == '=') {
            return ASTRA_SHELL_ERR_INVALID;
        }
    }
    name_length = strlen(name);
    value_length = strlen(value);
    (void)astra_shell_variable_unset(variables, name);
    if (variables->length + name_length + value_length + 2u >
        ASTRA_SHELL_VARIABLE_BYTES) {
        return ASTRA_SHELL_ERR_LIMIT;
    }
    shell_copy(&variables->storage[variables->length], name, name_length);
    variables->length += name_length + 1u;
    shell_copy(&variables->storage[variables->length], value, value_length);
    variables->length += value_length + 1u;
    ++variables->count;
    return ASTRA_SHELL_OK;
}

int astra_shell_variable_exportable(const char *name)
{
    size_t index;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    for (index = 0u; name[index] != '\0'; ++index) {
        char ch = name[index];
        int letter = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                     ch == '_';
        int digit = ch >= '0' && ch <= '9';

        if (!letter && !(digit && index != 0u)) {
            return 0;
        }
    }
    return 1;
}

/*
 * Where the next word belongs. A word ordinarily becomes an argument; the one
 * after a `>` becomes the redirect's name instead, and a `NAME=VALUE` word
 * ahead of the command becomes an assignment -- which is decided when the word
 * ends, so this only reserves the slot.
 */
static astra_shell_result_t words_begin(astra_shell_words_t *words,
                                        size_t output, int *target)
{
    if (*target != 0) {
        words->redirect = &words->storage[output];
        *target = 0;
        return ASTRA_SHELL_OK;
    }
    if (words->argc >= (int)ASTRA_SHELL_ARG_CAPACITY) return ASTRA_SHELL_ERR_LIMIT;
    words->argv[words->argc++] = &words->storage[output];
    return ASTRA_SHELL_OK;
}

/* `[A-Za-z_][A-Za-z0-9_]*`, the shape a name has to have to be one. */
static int name_character(char ch, int first)
{
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_') {
        return 1;
    }
    return first == 0 && ch >= '0' && ch <= '9';
}

/*
 * A finished word, judged.
 *
 * `NAME=VALUE` ahead of the command is an assignment and not an argument, so
 * it comes back out of `argv` and goes into `assignment`. After the first word
 * that is not one, `=` is an ordinary character again -- `rm a=b` removes a
 * file, and `ls FOO=bar` passes an argument, both of which every shell agrees
 * on and neither of which a rule about `=` alone would get right.
 *
 * Only a `=` the line carried counts. One that arrived inside a value cannot,
 * or `$pair` holding `a=b` would set a name nobody wrote.
 */
static int words_finish_word(astra_shell_words_t *words, size_t word_start,
                             size_t equals_at, int *commanded)
{
    char *word = &words->storage[word_start];
    size_t index;

    if (words->argc == 0 || words->argv[words->argc - 1] != word) {
        return 1;   /* it was the redirect's name, and never in argv */
    }
    if (*commanded != 0) {
        return 1;
    }
    if (equals_at == 0u) {
        *commanded = 1;   /* no `=`: this word is the command */
        return 1;
    }
    for (index = 0u; index < equals_at - word_start; ++index) {
        if (!name_character(word[index], index == 0u)) {
            *commanded = 1;
            return 1;
        }
    }
    if (words->assignments >= (int)ASTRA_SHELL_ARG_CAPACITY) {
        return 0;
    }
    words->assignment[words->assignments++] = word;
    --words->argc;
    return 1;
}

/*
 * One `$...` in the line, copied out as its value.
 *
 * `$?` and `$$` are names of one character that are not names anywhere else,
 * which is why the special case is here and not in the arena: the arena holds
 * `?` the way it holds `PATH`, and only the spelling rules differ.
 */
static astra_shell_result_t expand(const char *line, size_t *input,
                                   astra_shell_words_t *words, size_t *output,
                                   astra_shell_value_fn value, void *context)
{
    size_t at = *input;
    size_t begin;
    size_t length = 0u;
    int braced = 0;
    const char *found;

    if (line[at] == '{') {
        braced = 1;
        ++at;
    }
    begin = at;
    if (!braced && (line[at] == '?' || line[at] == '$')) {
        length = 1u;
        at += 1u;
    } else {
        while (name_character(line[at], length == 0u)) {
            ++length;
            ++at;
        }
    }
    if (length == 0u) {
        /* `$` naming nothing is a dollar sign, which is what was typed. */
        if (*output + 1u >= ASTRA_SHELL_LINE_CAPACITY) return ASTRA_SHELL_ERR_LIMIT;
        words->storage[(*output)++] = '$';
        return ASTRA_SHELL_OK;
    }
    if (braced != 0) {
        if (line[at] != '}') return ASTRA_SHELL_ERR_SYNTAX;
        ++at;
    }
    *input = at;
    found = value(context, &line[begin], length);
    if (found == NULL) {
        /* An unset name is nothing, not an error -- every shell agrees. */
        return ASTRA_SHELL_OK;
    }
    while (*found != '\0') {
        if (*output + 1u >= ASTRA_SHELL_LINE_CAPACITY) return ASTRA_SHELL_ERR_LIMIT;
        words->storage[(*output)++] = *found++;
    }
    return ASTRA_SHELL_OK;
}

astra_shell_result_t astra_shell_parse(const char *line,
                                       astra_shell_words_t *words,
                                       astra_shell_value_fn value,
                                       void *value_context)
{
    size_t input = 0u;
    size_t output = 0u;
    size_t word_start = 0u;
    size_t equals_at = 0u;   /* zero means "no literal `=` in this word yet" */
    char quote = '\0';
    int in_word = 0;
    int target = 0;      /* a `>` has been seen and its name has not */
    int commanded = 0;   /* the leading run of assignments is over */
    if (line == NULL || words == NULL) return ASTRA_SHELL_ERR_INVALID;
    words->argc = 0;
    words->redirect = NULL;
    words->redirect_append = 0;
    words->assignments = 0;
    while (line[input] != '\0') {
        char ch = line[input++];
        int literal = 0;
        if (quote == '\0' && (ch == ' ' || ch == '\t')) {
            if (in_word != 0) {
                words->storage[output++] = '\0';
                in_word = 0;
                if (!words_finish_word(words, word_start, equals_at,
                                       &commanded))
                    return ASTRA_SHELL_ERR_LIMIT;
            }
            continue;
        }
        if (ch == '\\') {
            if (line[input] == '\0') return ASTRA_SHELL_ERR_SYNTAX;
            ch = line[input++];
            literal = 1;
        } else if (ch == '\'' || ch == '"') {
            if (quote == '\0') {
                astra_shell_result_t begun;
                quote = ch;
                if (in_word == 0) {
                    begun = words_begin(words, output, &target);
                    if (begun != ASTRA_SHELL_OK) return begun;
                    word_start = output;
                    equals_at = 0u;
                    in_word = 1;
                }
                continue;
            }
            if (quote == ch) {
                quote = '\0';
                continue;
            }
        } else if (quote == '\0' && ch == '>') {
            /*
             * It ends the word it touches, so `ls>out` is `ls` and `out` the
             * way every other shell reads it. A redirect already named, or one
             * still waiting for its name, is the line saying two things about
             * one stream.
             */
            if (in_word != 0) {
                words->storage[output++] = '\0';
                in_word = 0;
                if (!words_finish_word(words, word_start, equals_at,
                                       &commanded))
                    return ASTRA_SHELL_ERR_LIMIT;
            }
            if (words->redirect != NULL || target != 0) return ASTRA_SHELL_ERR_SYNTAX;
            if (line[input] == '>') {
                words->redirect_append = 1;
                ++input;
            }
            target = 1;
            continue;
        }
        if (in_word == 0) {
            astra_shell_result_t begun = words_begin(words, output, &target);
            if (begun != ASTRA_SHELL_OK) return begun;
            word_start = output;
            equals_at = 0u;
            in_word = 1;
        }
        /*
         * Expansion, everywhere a shell does it: inside double quotes and bare
         * text, never inside single quotes and never after a backslash. The
         * value is one word however many spaces it holds -- this machine does
         * not field-split what a variable expanded to, which is the behaviour
         * `"$name"` has to be written for elsewhere and the one that surprises
         * nobody here.
         */
        if (ch == '$' && quote != '\'' && literal == 0 && value != NULL) {
            astra_shell_result_t expanded =
                expand(line, &input, words, &output, value, value_context);

            if (expanded != ASTRA_SHELL_OK) return expanded;
            continue;
        }
        if (output + 1u >= ASTRA_SHELL_LINE_CAPACITY) return ASTRA_SHELL_ERR_LIMIT;
        if (ch == '=' && quote == '\0' && literal == 0 && equals_at == 0u &&
            output > word_start) {
            equals_at = output;
        }
        words->storage[output++] = ch;
    }
    if (quote != '\0') return ASTRA_SHELL_ERR_SYNTAX;
    /* `ls >` named nothing, and a redirect with no name is not a command. */
    if (target != 0) return ASTRA_SHELL_ERR_SYNTAX;
    if (in_word != 0) {
        words->storage[output++] = '\0';
        if (!words_finish_word(words, word_start, equals_at, &commanded))
            return ASTRA_SHELL_ERR_LIMIT;
    }
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
        if (strcmp(builtins[index].name, words->argv[0]) == 0) {
            *command_result = builtins[index].function(context, words->argc, words->argv);
            return ASTRA_SHELL_OK;
        }
    }
    return ASTRA_SHELL_ERR_NOT_FOUND;
}
