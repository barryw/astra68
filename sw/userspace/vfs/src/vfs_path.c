/*
 * Splitting /name/rest, and normalising the rest. Neither function touches an
 * assign table or a backend: a path is refused here for what it says, and
 * refused later for what the process does not hold, and keeping the two apart
 * is what makes each one testable on its own.
 */

#include <astra/vfs_path.h>
#include <astra/vfs_path_payload.h>

#include <astra/ascii.h>
#include <astra/utf8.h>

#include <stddef.h>
#include <string.h>

/* One owner validates native text and transported operands. Length includes
 * NUL; component limits count bytes and do not fold dot/link semantics. */
static uint32_t path_operand_status(const char *text, uint32_t length)
{
    uint32_t component = 0u;

    if (text == NULL || length == 0u || text[length - 1u] != '\0')
        return ASTRA_VFS_ERR_INVALID;
    if (length > ASTRA_VFS_PATH_MAX)
        return ASTRA_VFS_ERR_NAME_TOO_LONG;
    if (!astra_utf8_validate(text, length - 1u, 0u))
        return ASTRA_VFS_ERR_INVALID;
    for (uint32_t at = 0u; at + 1u < length; ++at) {
        if (text[at] == '\0')
            return ASTRA_VFS_ERR_INVALID;
        if (text[at] == '/')
            component = 0u;
        else if (++component >= ASTRA_VFS_NAME_MAX)
            return ASTRA_VFS_ERR_NAME_TOO_LONG;
    }
    return ASTRA_VFS_OK;
}

static uint32_t path_operand_length(const char *text, uint32_t *length)
{
    if (text == NULL)
        return ASTRA_VFS_ERR_INVALID;
    for (uint32_t at = 0u; at < ASTRA_VFS_PATH_MAX; ++at) {
        if (text[at] == '\0') {
            *length = at + 1u;
            return path_operand_status(text, *length);
        }
    }
    return ASTRA_VFS_ERR_NAME_TOO_LONG;
}

uint32_t astra_vfs_path_measure(const char *path, uint32_t *length)
{
    uint32_t bytes = 0u;
    uint32_t status = path_operand_length(path, &bytes);

    if (length != NULL)
        *length = status == ASTRA_VFS_OK ? bytes - 1u : 0u;
    return status;
}

uint32_t astra_vfs_path_pack(const char *first, const char *second,
    void *bytes, uint32_t capacity, AstraVfsPathFields *fields, uint32_t *used)
{
    uint32_t first_length = 0u;
    uint32_t second_length = 0u;
    uint32_t status;

    if (fields == NULL || used == NULL || (bytes == NULL && capacity != 0u) ||
        (first == NULL && second != NULL))
        return ASTRA_VFS_ERR_INVALID;
    *fields = (AstraVfsPathFields){0};
    *used = 0u;
    if (first == NULL)
        return ASTRA_VFS_OK;
    status = path_operand_length(first, &first_length);
    if (status != ASTRA_VFS_OK)
        return status;
    if (second != NULL) {
        status = path_operand_length(second, &second_length);
        if (status != ASTRA_VFS_OK)
            return status;
    }
    if (first_length > capacity || second_length > capacity - first_length)
        return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    memmove(bytes, first, first_length);
    if (second_length != 0u)
        memmove((uint8_t *)bytes + first_length, second, second_length);
    *fields = (AstraVfsPathFields){0u, first_length,
        second_length != 0u ? first_length : 0u, second_length};
    *used = first_length + second_length;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_path_unpack(const AstraVfsPathFields *fields,
    const void *bytes, uint32_t size, uint32_t operands,
    AstraVfsPathOperands *paths)
{
    uint32_t status;

    if (fields == NULL || paths == NULL || operands > 2u)
        return ASTRA_VFS_ERR_PROTOCOL;
    *paths = (AstraVfsPathOperands){0};
    if (operands == 0u)
        return size == 0u && fields->first_offset == 0u &&
            fields->first_length == 0u && fields->second_offset == 0u &&
            fields->second_length == 0u ? ASTRA_VFS_OK : ASTRA_VFS_ERR_PROTOCOL;
    if (bytes == NULL || fields->first_offset != 0u ||
        fields->first_length == 0u || fields->first_length > size ||
        (operands == 1u && (fields->second_offset != 0u ||
                          fields->second_length != 0u)) ||
        (operands == 2u && (fields->second_offset != fields->first_length ||
                          fields->second_length == 0u)) ||
        fields->second_length != size - fields->first_length)
        return ASTRA_VFS_ERR_PROTOCOL;
    status = path_operand_status(bytes, fields->first_length);
    if (status != ASTRA_VFS_OK)
        return status == ASTRA_VFS_ERR_INVALID ? ASTRA_VFS_ERR_PROTOCOL : status;
    if (operands == 2u) {
        status = path_operand_status((const char *)bytes + fields->second_offset,
                                     fields->second_length);
        if (status != ASTRA_VFS_OK)
            return status == ASTRA_VFS_ERR_INVALID ? ASTRA_VFS_ERR_PROTOCOL : status;
    }
    paths->first = bytes;
    if (operands == 2u)
        paths->second = (const char *)bytes + fields->second_offset;
    return ASTRA_VFS_OK;
}

static uint32_t output_overflow(uint32_t capacity)
{
    return capacity >= ASTRA_VFS_PATH_MAX ? ASTRA_VFS_ERR_NAME_TOO_LONG :
        ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
}

static int
utf8_string(const char *text)
{
    uint32_t length = 0u;

    if (text == NULL)
        return 0;
    while (text[length] != '\0')
        ++length;
    return astra_utf8_validate(text, length, 0u);
}

uint32_t
astra_path_split(const char *path, char *name, uint32_t name_capacity,
                 char *rest, uint32_t rest_capacity)
{
    uint32_t index = 0u;
    uint32_t out = 0u;

    uint32_t input_status = astra_vfs_path_measure(path, NULL);
    if (input_status != ASTRA_VFS_OK)
        return input_status;
    if (name == NULL || rest == NULL ||
        name_capacity == 0u ||
        rest_capacity == 0u) {
        return ASTRA_VFS_ERR_INVALID;
    }
    if (path[0] != '/') {
        return ASTRA_VFS_ERR_INVALID;
    }
    ++index;
    while (path[index] != '/' && path[index] != '\0') {
        if (path[index] == ':' || out + 1u >= name_capacity) {
            return ASTRA_VFS_ERR_INVALID;
        }
        name[out++] = astra_ascii_upper(path[index]);
        ++index;
    }
    if (out == 0u) {
        return ASTRA_VFS_ERR_INVALID;   /* "/" names the virtual root */
    }
    name[out] = '\0';

    if (path[index] == '/')
        ++index;
    out = 0u;
    while (path[index] != '\0') {
        if (out + 1u >= rest_capacity) {
            return output_overflow(rest_capacity);
        }
        rest[out++] = path[index++];
    }
    rest[out] = '\0';
    return ASTRA_VFS_OK;
}

/* Appends with truncation refused; returns the new length, or `capacity`. */
static uint32_t
append(char *out, uint32_t length, uint32_t capacity, const char *text)
{
    uint32_t index = 0u;

    while (text[index] != '\0') {
        if (length + 1u >= capacity) {
            return capacity;
        }
        out[length++] = text[index++];
    }
    out[length] = '\0';
    return length;
}

/*
 * All absolute paths begin with a slash, including paths on mounted volumes.
 */
static int
is_absolute(const char *typed)
{
    return typed[0] == '/';
}

uint32_t
astra_path_qualify(const char *assign, const char *directory,
                   const char *typed, char *out, uint32_t capacity)
{
    uint32_t length = 0u;
    uint32_t input_status = astra_vfs_path_measure(directory, NULL);
    if (input_status == ASTRA_VFS_OK && typed != NULL)
        input_status = astra_vfs_path_measure(typed, NULL);
    if (input_status != ASTRA_VFS_OK)
        return input_status;

    if (!utf8_string(assign) || assign[0] == '\0' ||
        !utf8_string(directory) ||
        (typed != NULL && !utf8_string(typed)) || out == NULL ||
        capacity == 0u) {
        return ASTRA_VFS_ERR_INVALID;
    }
    out[0] = '\0';
    if (typed != NULL && is_absolute(typed)) {
        return append(out, 0u, capacity, typed) == capacity ?
            output_overflow(capacity) : ASTRA_VFS_OK;
    }
    length = append(out, length, capacity, "/");
    for (uint32_t index = 0u; length != capacity && assign[index] != '\0';
         ++index) {
        if (length + 1u >= capacity)
            length = capacity;
        else {
            out[length++] = astra_ascii_lower(assign[index]);
            out[length] = '\0';
        }
    }
    if (length != capacity) {
        if (directory[0] != '\0')
            length = append(out, length, capacity, "/");
        if (length != capacity)
            length = append(out, length, capacity, directory);
    }
    if (length != capacity && typed != NULL && typed[0] != '\0') {
        length = append(out, length, capacity, "/");
        if (length != capacity) {
            length = append(out, length, capacity, typed);
        }
    }
    return length == capacity ? output_overflow(capacity) : ASTRA_VFS_OK;
}

uint32_t astra_path_qualify_provider(const char *assign, const char *root,
    const char *directory, const char *typed, char *out, uint32_t capacity)
{
    const char *rest;
    size_t length;

    if (!utf8_string(root) || !utf8_string(directory))
        return ASTRA_VFS_ERR_INVALID;
    rest = directory[0] == '/' ? directory + 1 : directory;
    length = strlen(root);
    if (length != 0u) {
        if (strncmp(rest, root, length) != 0 ||
            (rest[length] != '\0' && rest[length] != '/'))
            return ASTRA_VFS_ERR_NOT_FOUND;
        rest += length;
        if (*rest == '/')
            ++rest;
    }
    return astra_path_qualify(assign, rest, typed, out, capacity);
}

/* True for the component between `start` and `end` being exactly ".." */
static int
is_parent(const char *rest, uint32_t start, uint32_t end)
{
    return end - start == 2u && rest[start] == '.' && rest[start + 1u] == '.';
}

static int
is_current(const char *rest, uint32_t start, uint32_t end)
{
    return end - start == 1u && rest[start] == '.';
}

int astra_path_requires_walk(const char *path)
{
    uint32_t at = 0u;

    if (path == NULL)
        return 1;
    while (path[at] != '\0') {
        uint32_t start;

        while (path[at] == '/')
            ++at;
        start = at;
        while (path[at] != '\0' && path[at] != '/')
            ++at;
        if (is_current(path, start, at) || is_parent(path, start, at))
            return 1;
    }
    return at != 0u && path[at - 1u] == '/';
}

uint32_t
astra_path_normalise(const char *rest, char *out, uint32_t capacity)
{
    uint32_t index = 0u;
    uint32_t length = 0u;

    uint32_t input_status = astra_vfs_path_measure(rest, NULL);
    if (input_status != ASTRA_VFS_OK)
        return input_status;
    if (out == NULL || capacity == 0u) {
        return ASTRA_VFS_ERR_INVALID;
    }
    out[0] = '\0';
    while (rest[index] != '\0') {
        uint32_t start;
        uint32_t end;

        while (rest[index] == '/') {
            ++index;                    /* empty components mean nothing */
        }
        start = index;
        while (rest[index] != '\0' && rest[index] != '/') {
            ++index;
        }
        end = index;
        if (end == start) {
            continue;
        }
        if (is_current(rest, start, end)) {
            continue;
        }
        if (is_parent(rest, start, end)) {
            if (length == 0u) {
                /* Above the assign's root there is nothing to name. */
                return ASTRA_VFS_ERR_NOT_FOUND;
            }
            while (length != 0u && out[length - 1u] != '/') {
                --length;
            }
            if (length != 0u) {
                --length;               /* drop the separator too */
            }
            out[length] = '\0';
            continue;
        }
        if (length != 0u) {
            if (length + 1u >= capacity) {
                return output_overflow(capacity);
            }
            out[length++] = '/';
        }
        for (uint32_t at = start; at < end; ++at) {
            if (length + 1u >= capacity) {
                return output_overflow(capacity);
            }
            out[length++] = rest[at];
        }
        out[length] = '\0';
    }
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_path_operand_count(uint32_t operation,
    const AstraVfsRequest *request)
{
    switch (operation) {
    case ASTRA_VFS_OP_RENAME:
    case ASTRA_VFS_OP_LINK:
    case ASTRA_VFS_OP_SYMLINK:
        return 2u;
    case ASTRA_VFS_OP_OPEN:
    case ASTRA_VFS_OP_OPEN_AT:
    case ASTRA_VFS_OP_READ_PATH:
    case ASTRA_VFS_OP_STAT:
    case ASTRA_VFS_OP_STAT_AT:
    case ASTRA_VFS_OP_LOOKUP:
    case ASTRA_VFS_OP_READDIR:
    case ASTRA_VFS_OP_READDIR_BATCH:
    case ASTRA_VFS_OP_READDIR_AREA:
    case ASTRA_VFS_OP_MKDIR:
    case ASTRA_VFS_OP_UNLINK:
    case ASTRA_VFS_OP_UNLINK_AT:
    case ASTRA_VFS_OP_CHMOD:
    case ASTRA_VFS_OP_CHMOD_AT:
    case ASTRA_VFS_OP_READLINK:
    case ASTRA_VFS_OP_GRANT_DERIVE:
        return 1u;
    case ASTRA_VFS_OP_FILESYSTEM_INFO:
        return request->file == ASTRA_VFS_FILE_INVALID ? 1u : 0u;
    default:
        return 0u;
    }
}

uint32_t astra_path_collapse(const char *path, char *out, uint32_t capacity)
{
    uint32_t used = 0u;

    uint32_t input_status = astra_vfs_path_measure(path, NULL);
    if (input_status != ASTRA_VFS_OK)
        return input_status;
    if (out == NULL || capacity == 0u)
        return ASTRA_VFS_ERR_INVALID;
    for (uint32_t at = 0u; path[at] != '\0'; ++at) {
        if (path[at] == '/' && used != 0u && out[used - 1u] == '/')
            continue;
        if (used + 1u >= capacity)
            return output_overflow(capacity);
        out[used++] = path[at];
    }
    out[used] = '\0';
    return ASTRA_VFS_OK;
}

/* Decode complete operands once, through the canonical payload validator. */
uint32_t astra_vfs_path_request(uint32_t operation,
    const AstraVfsRequest *request, const AstraVfsTransfer *transfer,
    AstraVfsPathOperands *paths)
{
    uint32_t operands = astra_vfs_path_operand_count(operation, request);

    if (request->size != ASTRA_VFS_REQUEST_SIZE || request->file_reserved != 0u ||
        (request->second_file != 0u && operation != ASTRA_VFS_OP_RENAME &&
         operation != ASTRA_VFS_OP_LINK))
        return ASTRA_VFS_ERR_PROTOCOL;
    if (operation == ASTRA_VFS_OP_HELLO && request->file != ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_ERR_PROTOCOL;
    if (operands == 0u) {
        *paths = (AstraVfsPathOperands){0};
        /* WRITE uses this union for binary bytes, never path descriptors. */
        if (operation == ASTRA_VFS_OP_WRITE)
            return ASTRA_VFS_OK;
    }
    return astra_vfs_path_unpack(&request->body.paths,
        transfer == NULL ? NULL : transfer->input,
        transfer == NULL ? 0u : transfer->input_size, operands, paths);
}
