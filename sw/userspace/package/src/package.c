#include <astra/package_library.h>

#include <astra/bundle.h>
#include <astra/bytes.h>
#include <astra/library.h>
#include <astra/manifest.h>
#include <astra/result.h>
#include <astra/runtime.h>
#include <astra/sha256.h>
#include <astra/string.h>
#include <astra/utf8.h>

#include <zstd.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PACKAGE_CHUNK 32768u
#define PACKAGE_DEPTH_MAX 32u
#define PACKAGE_ENTRIES_MAX 65536u

ASTRA_DYNAMIC_LIBRARY("package.library.1", 1, 0, 0, 1, 0,
    "Astra68 contributors", "Copyright 2026 Astra68 contributors");

typedef struct PackageEntry {
    char *source;
    char *path;
    char *link;
    uint64_t length;
    uint16_t mode;
    uint16_t kind;
    uint8_t digest[32];
} PackageEntry;

typedef struct PackageList {
    PackageEntry *entries;
    uint32_t count;
    uint32_t capacity;
    AstraFilesystem *filesystem;
} PackageList;

typedef struct PackageOutput {
    AstraFile *file;
    ZSTD_CCtx *codec;
    uint8_t *compressed;
    uint32_t capacity;
} PackageOutput;

static int valid_component(const char *name)
{
    if (name == NULL || name[0] == '\0' ||
        strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return 0;
    for (const unsigned char *at = (const unsigned char *)name; *at; ++at)
        if (*at < 0x20u || *at == 0x7fu || *at == '/' ||
            *at == '\\' || *at == ':')
            return 0;
    return astra_utf8_validate(name, (uint32_t)strlen(name), 0u);
}

static char *joined(const char *parent, const char *name)
{
    size_t left = strlen(parent), right = strlen(name);
    char *path;
    if (left + right + 2u >= ASTRA_VFS_PATH_MAX) return NULL;
    path = astra_runtime_allocate(left + right + 2u);
    if (path == NULL) return NULL;
    memcpy(path, parent, left);
    path[left] = '/';
    memcpy(path + left + 1u, name, right + 1u);
    return path;
}

static void destroy_entries(PackageList *list)
{
    for (uint32_t at = 0u; at < list->count; ++at) {
        astra_runtime_deallocate(list->entries[at].source);
        astra_runtime_deallocate(list->entries[at].path);
        astra_runtime_deallocate(list->entries[at].link);
    }
    astra_runtime_deallocate(list->entries);
    list->entries = NULL;
    list->count = list->capacity = 0u;
}

static uint32_t digest_file(AstraFilesystem *filesystem, const char *path,
                            uint64_t length, uint8_t digest[32])
{
    AstraFile file = ASTRA_FILE_INIT;
    AstraSha256 hash;
    uint8_t buffer[PACKAGE_CHUNK];
    uint64_t total = 0u;
    uint32_t status = astra_filesystem_open(filesystem, path,
        ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_NOFOLLOW, &file);
    if (status != ASTRA_VFS_OK) return status;
    astra_sha256_init(&hash);
    while (status == ASTRA_VFS_OK && total < length) {
        uint32_t count = 0u;
        uint32_t wanted = length - total > sizeof(buffer) ?
            sizeof(buffer) : (uint32_t)(length - total);
        status = astra_filesystem_read(&file, buffer, wanted, &count);
        if (status == ASTRA_VFS_OK && count == 0u) status = ASTRA_VFS_ERR_IO;
        if (status == ASTRA_VFS_OK) {
            astra_sha256_update(&hash, buffer, count);
            total += count;
        }
    }
    if (status == ASTRA_VFS_OK) {
        uint32_t count = 0u;
        status = astra_filesystem_read(&file, buffer, 1u, &count);
        if (status == ASTRA_VFS_OK && count != 0u) status = ASTRA_VFS_ERR_IO;
    }
    uint32_t closed = astra_filesystem_close(&file);
    if (status == ASTRA_VFS_OK) status = closed;
    if (status == ASTRA_VFS_OK) astra_sha256_final(&hash, digest);
    return status;
}

/* Link resolution starts beside the link and may never escape bundle root. */
static int safe_link(const char *relative, const char *target)
{
    size_t length = strlen(relative), depth = 0u;
    if (target[0] == '\0' || target[0] == '/' ||
        strlen(target) >= ASTRA_VFS_PATH_MAX ||
        !astra_utf8_validate(target, (uint32_t)strlen(target), 0u)) return 0;
    for (size_t at = 0u; at < length; ++at)
        if (relative[at] == '/') ++depth;
    if (depth == 0u) return 0; /* First component is bundle name. */
    for (const char *at = target; *at != '\0';) {
        const char *end = at;
        while (*end != '\0' && *end != '/') ++end;
        size_t part = (size_t)(end - at);
        if (part == 0u || memchr(at, '\\', part) != NULL ||
            memchr(at, ':', part) != NULL) return 0;
        if (part == 1u && at[0] == '.') {
            /* Current directory. */
        } else if (part == 2u && at[0] == '.' && at[1] == '.') {
            if (depth <= 1u) return 0;
            --depth;
        } else {
            for (size_t i = 0u; i < part; ++i)
                if ((unsigned char)at[i] < 0x20u ||
                    (unsigned char)at[i] == 0x7fu) return 0;
            ++depth;
        }
        at = *end == '/' ? end + 1u : end;
        if (*end == '/' && *at == '\0') return 0;
    }
    return 1;
}

static uint32_t add_entry(PackageList *list, const char *source,
                          const char *path)
{
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    PackageEntry entry = {0};
    uint32_t status = astra_filesystem_lstat(list->filesystem, source, &info);
    if (status != ASTRA_VFS_OK) return status;
    if (info.kind != ASTRA_VFS_KIND_FILE &&
        info.kind != ASTRA_VFS_KIND_DIRECTORY &&
        info.kind != ASTRA_VFS_KIND_SYMLINK)
        return ASTRA_VFS_ERR_UNSUPPORTED;
    if ((info.mode & 06000u) != 0u || list->count >= PACKAGE_ENTRIES_MAX)
        return ASTRA_VFS_ERR_LIMIT;
    entry.kind = info.kind;
    entry.mode = info.kind == ASTRA_VFS_KIND_DIRECTORY ? 0755u :
        info.kind == ASTRA_VFS_KIND_SYMLINK ? 0777u :
        (info.mode & 0111u) != 0u ? 0755u : 0644u;
    entry.length = info.kind == ASTRA_VFS_KIND_FILE ? info.byte_size : 0u;
    entry.source = astra_runtime_allocate(strlen(source) + 1u);
    entry.path = astra_runtime_allocate(strlen(path) + 1u);
    if (entry.source == NULL || entry.path == NULL) goto no_memory;
    strcpy(entry.source, source);
    strcpy(entry.path, path);
    if (entry.kind == ASTRA_VFS_KIND_FILE)
        status = digest_file(list->filesystem, source, entry.length,
                             entry.digest);
    else if (entry.kind == ASTRA_VFS_KIND_SYMLINK) {
        char target[ASTRA_VFS_PATH_MAX];
        uint32_t count = 0u;
        status = astra_filesystem_readlink(list->filesystem, source,
            target, sizeof(target) - 1u, &count);
        if (status == ASTRA_VFS_OK) {
            if (count == 0u || count >= sizeof(target))
                status = ASTRA_VFS_ERR_INVALID;
            else {
                target[count] = '\0';
                if (!safe_link(path + sizeof("payload/") - 1u, target))
                    status = ASTRA_VFS_ERR_INVALID;
            }
        }
        if (status == ASTRA_VFS_OK) {
            entry.link = astra_runtime_allocate(count + 1u);
            if (entry.link == NULL) status = ASTRA_VFS_ERR_LIMIT;
            else {
                memcpy(entry.link, target, count + 1u);
                AstraSha256 hash;
                astra_sha256_init(&hash);
                astra_sha256_update(&hash, target, count);
                astra_sha256_final(&hash, entry.digest);
            }
        }
    } else {
        AstraSha256 hash;
        astra_sha256_init(&hash);
        astra_sha256_final(&hash, entry.digest);
    }
    if (status != ASTRA_VFS_OK) goto fail;
    if (list->count == list->capacity) {
        uint32_t grown = list->capacity == 0u ? 16u : list->capacity * 2u;
        PackageEntry *entries = astra_runtime_reallocate(list->entries,
            (size_t)grown * sizeof(*entries));
        if (entries == NULL) goto no_memory;
        list->entries = entries;
        list->capacity = grown;
    }
    list->entries[list->count++] = entry;
    return ASTRA_VFS_OK;
no_memory:
    status = ASTRA_VFS_ERR_LIMIT;
fail:
    astra_runtime_deallocate(entry.source);
    astra_runtime_deallocate(entry.path);
    astra_runtime_deallocate(entry.link);
    return status;
}

static uint32_t collect(PackageList *list, const char *source,
                        const char *path, uint32_t depth)
{
    AstraDirectory scan = ASTRA_DIRECTORY_INIT;
    uint32_t status;
    if (depth > PACKAGE_DEPTH_MAX) return ASTRA_VFS_ERR_LIMIT;
    status = add_entry(list, source, path);
    if (status != ASTRA_VFS_OK ||
        list->entries[list->count - 1u].kind != ASTRA_VFS_KIND_DIRECTORY)
        return status;
    status = astra_filesystem_directory_open(list->filesystem, source, &scan);
    while (status == ASTRA_VFS_OK) {
        AstraDirectoryEntry batch[8];
        uint32_t count = 0u;
        status = astra_filesystem_directory_read(&scan, batch, 8u, &count);
        if (status != ASTRA_VFS_OK || count == 0u) break;
        for (uint32_t at = 0u; at < count; ++at) {
            if (strcmp(batch[at].name, ".") == 0 ||
                strcmp(batch[at].name, "..") == 0) continue;
            if (!valid_component(batch[at].name)) {
                status = ASTRA_VFS_ERR_INVALID;
                break;
            }
            char *child_source = joined(source, batch[at].name);
            char *child_path = joined(path, batch[at].name);
            if (child_source == NULL || child_path == NULL)
                status = ASTRA_VFS_ERR_LIMIT;
            else
                status = collect(list, child_source, child_path, depth + 1u);
            astra_runtime_deallocate(child_source);
            astra_runtime_deallocate(child_path);
            if (status != ASTRA_VFS_OK) break;
        }
    }
    astra_filesystem_directory_close(&scan);
    return status;
}

static int compare_entries(const void *left, const void *right)
{
    return strcmp(((const PackageEntry *)left)->path,
                  ((const PackageEntry *)right)->path);
}

static void sort_entries(PackageList *list)
{
    qsort(list->entries, list->count, sizeof(list->entries[0]),
          compare_entries);
}

static uint32_t read_blob(AstraFilesystem *filesystem, const char *path,
                          void **bytes, uint32_t *length)
{
    AstraFile file = ASTRA_FILE_INIT;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint32_t status;
    uint8_t *data = NULL;
    int opened = 0;
    *bytes = NULL;
    *length = 0u;
    status = astra_filesystem_open(filesystem, path,
        ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_NOFOLLOW, &file);
    if (status == ASTRA_VFS_OK) {
        opened = 1;
        status = astra_filesystem_file_info(&file, &info);
    }
    if (status == ASTRA_VFS_OK &&
        (info.kind != ASTRA_VFS_KIND_FILE || info.byte_size >= UINT32_MAX))
        status = ASTRA_VFS_ERR_LIMIT;
    if (status == ASTRA_VFS_OK) {
        data = astra_runtime_allocate((size_t)info.byte_size + 1u);
        if (data == NULL) status = ASTRA_VFS_ERR_LIMIT;
    }
    while (status == ASTRA_VFS_OK && *length < info.byte_size) {
        uint32_t count = 0u;
        status = astra_filesystem_read(&file, data + *length,
            (uint32_t)info.byte_size - *length, &count);
        if (status == ASTRA_VFS_OK && count == 0u) status = ASTRA_VFS_ERR_IO;
        *length += count;
    }
    if (opened) {
        uint32_t closed = astra_filesystem_close(&file);
        if (status == ASTRA_VFS_OK) status = closed;
    }
    if (status != ASTRA_VFS_OK) {
        astra_runtime_deallocate(data);
        *length = 0u;
        return status;
    }
    data[*length] = '\0';
    *bytes = data;
    return ASTRA_VFS_OK;
}

static uint32_t bundle_manifest(AstraFilesystem *filesystem,
    const char *bundle, AstraBundleManifest *manifest)
{
    char *path = joined(bundle, "manifest");
    char *bytes = NULL;
    uint32_t length = 0u, line = 0u;
    if (path == NULL) return ASTRA_VFS_ERR_LIMIT;
    uint32_t status = read_blob(filesystem, path, (void **)&bytes, &length);
    astra_runtime_deallocate(path);
    if (status != ASTRA_VFS_OK) return status;
    uint32_t parsed = astra_bundle_manifest_parse(bytes, length, manifest,
                                                   &line);
    astra_runtime_deallocate(bytes);
    if (parsed != ASTRA_BUNDLE_OK ||
        manifest->kind != ASTRA_BUNDLE_APPLICATION ||
        manifest->executable == NULL || manifest->icon == NULL) {
        astra_bundle_manifest_destroy(manifest);
        return ASTRA_VFS_ERR_INVALID;
    }
    path = joined(bundle, manifest->executable);
    if (path == NULL) status = ASTRA_VFS_ERR_LIMIT;
    else {
        AstraFileInfo info = ASTRA_FILE_INFO_INIT;
        status = astra_filesystem_lstat(filesystem, path, &info);
        if (status == ASTRA_VFS_OK && info.kind != ASTRA_VFS_KIND_FILE)
            status = ASTRA_VFS_ERR_INVALID;
    }
    astra_runtime_deallocate(path);
    if (status == ASTRA_VFS_OK) {
        AstraAicon icon;
        void *icon_data = NULL;
        uint32_t icon_length = 0u;
        path = joined(bundle, manifest->icon);
        status = path == NULL ? ASTRA_VFS_ERR_LIMIT :
            read_blob(filesystem, path, &icon_data, &icon_length);
        if (status == ASTRA_VFS_OK &&
            astra_aicon_open(icon_data, icon_length, &icon) != ASTRA_BUNDLE_OK)
            status = ASTRA_VFS_ERR_INVALID;
        astra_runtime_deallocate(icon_data);
        astra_runtime_deallocate(path);
    }
    if (status != ASTRA_VFS_OK) astra_bundle_manifest_destroy(manifest);
    return status;
}

static int append_quoted(AstraString *text, const char *value)
{
    if (!astra_string_append_char(text, '"')) return 0;
    for (const char *at = value; *at; ++at) {
        if ((*at == '\\' || *at == '"') &&
            !astra_string_append_char(text, '\\')) return 0;
        if (!astra_string_append_char(text, *at)) return 0;
    }
    return astra_string_append_char(text, '"');
}

static uint32_t make_manifest(const AstraBundleManifest *bundle,
    const char *leaf, char text[1024], uint32_t *length)
{
    AstraString out;
    astra_string_init(&out, text, 1024u);
    if (!astra_string_append(&out, "astra-package 1\nid ") ||
        !astra_string_append(&out, bundle->id) ||
        !astra_string_append(&out, "\nversion ") ||
        !astra_string_append_u64(&out, bundle->version.major) ||
        !astra_string_append_char(&out, '.') ||
        !astra_string_append_u64(&out, bundle->version.minor) ||
        !astra_string_append_char(&out, '.') ||
        !astra_string_append_u64(&out, bundle->version.patch) ||
        !astra_string_append(&out, "\narchitecture m68k-68040\nbundle ") ||
        !append_quoted(&out, leaf) ||
        !astra_string_append_char(&out, '\n'))
        return ASTRA_VFS_ERR_LIMIT;
    *length = out.length;
    return ASTRA_VFS_OK;
}

static int append_mode(AstraString *text, uint16_t mode)
{
    char octal[4];
    for (uint32_t at = 0u; at < 4u; ++at)
        octal[at] = (char)('0' + ((mode >> (3u * (3u - at))) & 7u));
    return astra_string_append_bytes(text, octal, sizeof(octal));
}

/* Canonical ledger: type, mode, length, SHA-256, path and optional link. */
static uint32_t make_ledger(const PackageList *list, char **text,
                            uint32_t *length)
{
    uint64_t capacity = 1u;
    for (uint32_t at = 0u; at < list->count; ++at)
        capacity += strlen(list->entries[at].path) +
            (list->entries[at].link == NULL ? 0u :
             strlen(list->entries[at].link)) + 128u;
    if (capacity > UINT32_MAX) return ASTRA_VFS_ERR_LIMIT;
    char *bytes = astra_runtime_allocate((size_t)capacity);
    if (bytes == NULL) return ASTRA_VFS_ERR_LIMIT;
    AstraString out;
    astra_string_init(&out, bytes, (uint32_t)capacity);
    for (uint32_t at = 0u; at < list->count; ++at) {
        const PackageEntry *entry = &list->entries[at];
        char kind = entry->kind == ASTRA_VFS_KIND_DIRECTORY ? 'd' :
                    entry->kind == ASTRA_VFS_KIND_SYMLINK ? 'l' : 'f';
        if (!astra_string_append_char(&out, kind) ||
            !astra_string_append_char(&out, '\t') ||
            !append_mode(&out, entry->mode) ||
            !astra_string_append_char(&out, '\t') ||
            !astra_string_append_u64(&out, entry->length) ||
            !astra_string_append_char(&out, '\t')) goto fail;
        for (uint32_t byte = 0u; byte < 32u; ++byte)
            if (!astra_string_append_hex(&out, entry->digest[byte], 2u))
                goto fail;
        if (!astra_string_append_char(&out, '\t') ||
            !astra_string_append(&out, entry->path)) goto fail;
        if (entry->link != NULL &&
            (!astra_string_append_char(&out, '\t') ||
             !astra_string_append(&out, entry->link))) goto fail;
        if (!astra_string_append_char(&out, '\n')) goto fail;
    }
    *text = bytes;
    *length = out.length;
    return ASTRA_VFS_OK;
fail:
    astra_runtime_deallocate(bytes);
    return ASTRA_VFS_ERR_LIMIT;
}

static uint32_t compressed_write(PackageOutput *output, const void *bytes,
                                 uint32_t length, ZSTD_EndDirective end)
{
    ZSTD_inBuffer input = {bytes, length, 0u};
    size_t remaining;
    size_t filled;
    do {
        ZSTD_outBuffer encoded = {output->compressed, output->capacity, 0u};
        remaining = ZSTD_compressStream2(output->codec, &encoded, &input, end);
        if (ZSTD_isError(remaining)) return ASTRA_VFS_ERR_IO;
        filled = encoded.pos;
        for (size_t at = 0u; at < encoded.pos;) {
            uint32_t written = 0u;
            uint32_t status = astra_filesystem_write(output->file, UINT64_MAX,
                output->compressed + at, (uint32_t)(encoded.pos - at),
                &written);
            if (status != ASTRA_VFS_OK) return status;
            if (written == 0u) return ASTRA_VFS_ERR_IO;
            at += written;
        }
    } while (input.pos < input.size || filled == output->capacity ||
             (end == ZSTD_e_end && remaining != 0u));
    return ASTRA_VFS_OK;
}

static int octal_field(uint8_t *field, uint32_t width, uint64_t value)
{
    if (width < 2u) return 0;
    memset(field, '0', width - 1u);
    field[width - 1u] = 0u;
    for (uint32_t at = width - 1u; at > 0u && value != 0u;) {
        field[--at] = (uint8_t)('0' + (value & 7u));
        value >>= 3;
    }
    return value == 0u;
}

static uint32_t tar_build_header(uint8_t header[512], const char *name,
    uint16_t mode, uint64_t length, char type, const char *link)
{
    memset(header, 0, 512u);
    size_t name_length = strlen(name);
    size_t link_length = link == NULL ? 0u : strlen(link);
    if (name_length > 100u || link_length > 100u ||
        !octal_field(header + 100u, 8u, mode) ||
        !octal_field(header + 108u, 8u, 0u) ||
        !octal_field(header + 116u, 8u, 0u) ||
        !octal_field(header + 124u, 12u, length) ||
        !octal_field(header + 136u, 12u, 0u))
        return ASTRA_VFS_ERR_LIMIT;
    memcpy(header, name, name_length);
    if (link != NULL) memcpy(header + 157u, link, link_length);
    memset(header + 148u, ' ', 8u);
    header[156] = (uint8_t)type;
    memcpy(header + 257u, "ustar", 5u);
    header[262u] = 0u;
    header[263u] = '0'; header[264u] = '0';
    uint32_t sum = 0u;
    for (uint32_t at = 0u; at < 512u; ++at) sum += header[at];
    if (!octal_field(header + 148u, 7u, sum)) return ASTRA_VFS_ERR_LIMIT;
    header[155u] = ' ';
    return ASTRA_VFS_OK;
}

static uint32_t tar_header(PackageOutput *output, const char *name,
    uint16_t mode, uint64_t length, char type, const char *link)
{
    uint8_t header[512];
    uint32_t status = tar_build_header(header, name, mode, length, type,
                                       link);
    if (status != ASTRA_VFS_OK) return status;
    return compressed_write(output, header, sizeof(header), ZSTD_e_continue);
}

static uint32_t tar_padding(PackageOutput *output, uint64_t length)
{
    static const uint8_t zero[512] = {0};
    uint32_t padding = (uint32_t)((512u - length % 512u) % 512u);
    return padding == 0u ? ASTRA_VFS_OK :
        compressed_write(output, zero, padding, ZSTD_e_continue);
}

static uint32_t pax_record(char *buffer, uint32_t capacity,
    const char *key, const char *value, uint32_t *length)
{
    uint32_t payload = (uint32_t)(strlen(key) + strlen(value) + 3u);
    uint32_t digits = 1u;
    uint32_t total;
    for (;;) {
        uint32_t candidate = payload + digits;
        uint32_t next_digits = 1u;
        for (uint32_t number = candidate; number >= 10u; number /= 10u)
            ++next_digits;
        if (next_digits == digits) { total = candidate; break; }
        digits = next_digits;
    }
    if (total >= capacity) return ASTRA_VFS_ERR_LIMIT;
    AstraString out;
    astra_string_init(&out, buffer, capacity);
    if (!astra_string_append_u64(&out, total) ||
        !astra_string_append_char(&out, ' ') ||
        !astra_string_append(&out, key) ||
        !astra_string_append_char(&out, '=') ||
        !astra_string_append(&out, value) ||
        !astra_string_append_char(&out, '\n') || out.length != total)
        return ASTRA_VFS_ERR_LIMIT;
    *length = total;
    return ASTRA_VFS_OK;
}

static uint32_t tar_entry_header(PackageOutput *output, const char *path,
    uint16_t mode, uint64_t length, char type, const char *link)
{
    uint32_t status;
    const char *header_path = path;
    const char *header_link = link;
    uint32_t pax_length = 0u;
    char *pax = NULL;
    if (strlen(path) > 100u || (link != NULL && strlen(link) > 100u)) {
        pax = astra_runtime_allocate(2u * ASTRA_VFS_PATH_MAX + 128u);
        if (pax == NULL) return ASTRA_VFS_ERR_LIMIT;
        if (strlen(path) > 100u) {
            status = pax_record(pax, 2u * ASTRA_VFS_PATH_MAX + 128u,
                "path", path, &pax_length);
            if (status != ASTRA_VFS_OK) goto done;
            header_path = "entry";
        }
        if (link != NULL && strlen(link) > 100u) {
            uint32_t added = 0u;
            status = pax_record(pax + pax_length,
                2u * ASTRA_VFS_PATH_MAX + 128u - pax_length,
                "linkpath", link, &added);
            if (status != ASTRA_VFS_OK) goto done;
            pax_length += added;
            header_link = "";
        }
        status = tar_header(output, "PaxHeaders/entry", 0644u,
            pax_length, 'x', NULL);
        if (status == ASTRA_VFS_OK)
            status = compressed_write(output, pax, pax_length,
                                      ZSTD_e_continue);
        if (status == ASTRA_VFS_OK) status = tar_padding(output, pax_length);
        if (status != ASTRA_VFS_OK) goto done;
    }
    status = tar_header(output, header_path, mode, length, type, header_link);
done:
    astra_runtime_deallocate(pax);
    return status;
}

static uint32_t tar_blob(PackageOutput *output, const char *path,
    const void *bytes, uint32_t length)
{
    uint32_t status = tar_entry_header(output, path, 0644u, length, '0', NULL);
    if (status == ASTRA_VFS_OK)
        status = compressed_write(output, bytes, length, ZSTD_e_continue);
    if (status == ASTRA_VFS_OK) status = tar_padding(output, length);
    return status;
}

static uint32_t tar_file(PackageOutput *output, AstraFilesystem *filesystem,
    const PackageEntry *entry)
{
    AstraFile source = ASTRA_FILE_INIT;
    AstraSha256 hash;
    uint8_t buffer[PACKAGE_CHUNK], digest[32];
    uint64_t total = 0u;
    uint32_t status = tar_entry_header(output, entry->path, entry->mode,
                                      entry->length, '0', NULL);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_open(filesystem, entry->source,
        ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_NOFOLLOW, &source);
    if (status != ASTRA_VFS_OK) return status;
    astra_sha256_init(&hash);
    while (status == ASTRA_VFS_OK && total < entry->length) {
        uint32_t wanted = entry->length - total > sizeof(buffer) ?
            sizeof(buffer) : (uint32_t)(entry->length - total);
        uint32_t count = 0u;
        status = astra_filesystem_read(&source, buffer, wanted, &count);
        if (status == ASTRA_VFS_OK && count == 0u) status = ASTRA_VFS_ERR_IO;
        if (status == ASTRA_VFS_OK) {
            astra_sha256_update(&hash, buffer, count);
            status = compressed_write(output, buffer, count, ZSTD_e_continue);
            total += count;
        }
    }
    if (status == ASTRA_VFS_OK) {
        uint32_t count = 0u;
        status = astra_filesystem_read(&source, buffer, 1u, &count);
        if (status == ASTRA_VFS_OK && count != 0u) status = ASTRA_VFS_ERR_IO;
    }
    uint32_t closed = astra_filesystem_close(&source);
    if (status == ASTRA_VFS_OK) status = closed;
    if (status == ASTRA_VFS_OK) {
        astra_sha256_final(&hash, digest);
        if (memcmp(digest, entry->digest, sizeof(digest)) != 0)
            status = ASTRA_VFS_ERR_IO;
    }
    if (status == ASTRA_VFS_OK) status = tar_padding(output, entry->length);
    return status;
}

static uint32_t write_archive(PackageOutput *output,
    const PackageList *list, const char *manifest, uint32_t manifest_length,
    const char *ledger, uint32_t ledger_length,
    const void *icon, uint32_t icon_length)
{
    static const uint8_t zero[1024] = {0};
    uint32_t status = tar_blob(output, "files.sha256", ledger, ledger_length);
    if (status == ASTRA_VFS_OK)
        status = tar_blob(output, "icon.aicon", icon, icon_length);
    if (status == ASTRA_VFS_OK)
        status = tar_blob(output, "manifest", manifest, manifest_length);
    if (status == ASTRA_VFS_OK)
        status = tar_entry_header(output, "payload/", 0755u, 0u, '5', NULL);
    for (uint32_t at = 0u; status == ASTRA_VFS_OK && at < list->count; ++at) {
        const PackageEntry *entry = &list->entries[at];
        if (entry->kind == ASTRA_VFS_KIND_FILE)
            status = tar_file(output, list->filesystem, entry);
        else
            status = tar_entry_header(output, entry->path, entry->mode, 0u,
                entry->kind == ASTRA_VFS_KIND_DIRECTORY ? '5' : '2',
                entry->link);
    }
    if (status == ASTRA_VFS_OK)
        status = compressed_write(output, zero, sizeof(zero), ZSTD_e_continue);
    if (status == ASTRA_VFS_OK)
        status = compressed_write(output, NULL, 0u, ZSTD_e_end);
    return status;
}

typedef struct PackageInput {
    AstraFile *file;
    ZSTD_DCtx *codec;
    uint8_t *compressed;
    uint8_t *plain;
    size_t compressed_at;
    size_t compressed_size;
    size_t plain_at;
    size_t plain_size;
    uint64_t total;
    int frame_done;
} PackageInput;

static AstraResult package_result(uint32_t status);

typedef struct TarEntry {
    char path[ASTRA_VFS_PATH_MAX];
    char link[ASTRA_VFS_PATH_MAX];
    uint64_t length;
    uint16_t mode;
    char type;
} TarEntry;

static uint32_t input_fill(PackageInput *input)
{
    while (input->plain_at == input->plain_size) {
        if (input->frame_done) return ASTRA_VFS_ERR_NOT_FOUND;
        if (input->compressed_at == input->compressed_size) {
            uint32_t count = 0u;
            uint32_t status = astra_filesystem_read(input->file,
                input->compressed, PACKAGE_CHUNK, &count);
            if (status != ASTRA_VFS_OK) return status;
            if (count == 0u) return ASTRA_VFS_ERR_PROTOCOL;
            input->compressed_at = 0u;
            input->compressed_size = count;
        }
        ZSTD_inBuffer source = {input->compressed,
            input->compressed_size, input->compressed_at};
        ZSTD_outBuffer target = {input->plain, PACKAGE_CHUNK, 0u};
        size_t remaining = ZSTD_decompressStream(input->codec,
                                                  &target, &source);
        if (ZSTD_isError(remaining)) return ASTRA_VFS_ERR_PROTOCOL;
        input->compressed_at = source.pos;
        input->plain_at = 0u;
        input->plain_size = target.pos;
        if (remaining == 0u) input->frame_done = 1;
    }
    return ASTRA_VFS_OK;
}

static uint32_t input_read(PackageInput *input, void *bytes, uint32_t length)
{
    uint8_t *target = bytes;
    for (uint32_t at = 0u; at < length;) {
        uint32_t status = input_fill(input);
        if (status != ASTRA_VFS_OK) return status;
        size_t count = input->plain_size - input->plain_at;
        if (count > length - at) count = length - at;
        if (input->total + count > UINT32_MAX)
            return ASTRA_VFS_ERR_LIMIT;
        if (target != NULL)
            memcpy(target + at, input->plain + input->plain_at, count);
        input->plain_at += count;
        input->total += count;
        at += (uint32_t)count;
    }
    return ASTRA_VFS_OK;
}

static uint32_t input_padding(PackageInput *input, uint64_t length)
{
    uint8_t bytes[512];
    uint32_t count = (uint32_t)((512u - length % 512u) % 512u);
    uint32_t status = input_read(input, bytes, count);
    for (uint32_t at = 0u; status == ASTRA_VFS_OK && at < count; ++at)
        if (bytes[at] != 0u) status = ASTRA_VFS_ERR_PROTOCOL;
    return status;
}

static int parse_octal(const uint8_t *bytes, uint32_t length,
                       uint64_t *value)
{
    uint64_t result = 0u;
    uint32_t at = 0u, digits = 0u;
    while (at < length && bytes[at] == ' ') ++at;
    for (; at < length && bytes[at] >= '0' && bytes[at] <= '7'; ++at) {
        if (result > (UINT64_MAX >> 3)) return 0;
        result = (result << 3) | (uint64_t)(bytes[at] - '0');
        ++digits;
    }
    while (at < length && (bytes[at] == 0u || bytes[at] == ' ')) ++at;
    if (digits == 0u || at != length) return 0;
    *value = result;
    return 1;
}

static uint32_t tar_next(PackageInput *input, TarEntry *entry,
                         int *end_of_archive)
{
    uint8_t header[512];
    uint64_t mode, size, checksum;
    uint32_t status = input_read(input, header, sizeof(header));
    if (status != ASTRA_VFS_OK) return status;
    int zero = 1;
    for (uint32_t at = 0u; at < sizeof(header); ++at)
        if (header[at] != 0u) { zero = 0; break; }
    *end_of_archive = zero;
    if (zero) {
        status = input_read(input, header, sizeof(header));
        if (status != ASTRA_VFS_OK) return status;
        for (uint32_t at = 0u; at < sizeof(header); ++at)
            if (header[at] != 0u) return ASTRA_VFS_ERR_PROTOCOL;
        return ASTRA_VFS_OK;
    }
    if (memcmp(header + 257u, "ustar\0", 6u) != 0 ||
        header[263u] != '0' || header[264u] != '0' ||
        !parse_octal(header + 100u, 8u, &mode) || mode > 0777u ||
        !parse_octal(header + 124u, 12u, &size) ||
        !parse_octal(header + 148u, 8u, &checksum))
        return ASTRA_VFS_ERR_PROTOCOL;
    uint32_t sum = 0u;
    for (uint32_t at = 0u; at < sizeof(header); ++at)
        sum += at >= 148u && at < 156u ? (uint8_t)' ' : header[at];
    if (sum != checksum || memchr(header, 0, 100u) == NULL ||
        memchr(header + 157u, 0, 100u) == NULL)
        return ASTRA_VFS_ERR_PROTOCOL;
    for (uint32_t at = 345u; at < 500u; ++at)
        if (header[at] != 0u) return ASTRA_VFS_ERR_PROTOCOL;
    for (uint32_t at = 108u; at < 124u; at += 8u) {
        uint64_t owner;
        if (!parse_octal(header + at, 8u, &owner) || owner != 0u)
            return ASTRA_VFS_ERR_PROTOCOL;
    }
    uint64_t mtime;
    if (!parse_octal(header + 136u, 12u, &mtime) || mtime != 0u)
        return ASTRA_VFS_ERR_PROTOCOL;
    memset(entry, 0, sizeof(*entry));
    strcpy(entry->path, (const char *)header);
    strcpy(entry->link, (const char *)header + 157u);
    entry->length = size;
    entry->mode = (uint16_t)mode;
    entry->type = (char)header[156u];
    return ASTRA_VFS_OK;
}

static uint32_t pax_apply(const char *data, uint32_t length,
                          TarEntry *entry)
{
    uint32_t at = 0u;
    int seen_path = 0, seen_link = 0;
    while (at < length) {
        uint64_t record = 0u;
        uint32_t start = at;
        while (at < length && data[at] >= '0' && data[at] <= '9') {
            record = record * 10u + (uint32_t)(data[at++] - '0');
            if (record > length) return ASTRA_VFS_ERR_PROTOCOL;
        }
        if (at == start || at >= length || data[at++] != ' ' ||
            record < 5u || record > length - start ||
            data[start + record - 1u] != '\n')
            return ASTRA_VFS_ERR_PROTOCOL;
        const char *field = data + at;
        uint32_t field_length = (uint32_t)(start + record - at - 1u);
        const char *equal = memchr(field, '=', field_length);
        if (equal == NULL) return ASTRA_VFS_ERR_PROTOCOL;
        uint32_t key_length = (uint32_t)(equal - field);
        const char *value = equal + 1u;
        uint32_t value_length = field_length - key_length - 1u;
        char *target = NULL;
        int *seen = NULL;
        if (key_length == 4u && memcmp(field, "path", 4u) == 0) {
            target = entry->path; seen = &seen_path;
        } else if (key_length == 8u &&
                   memcmp(field, "linkpath", 8u) == 0) {
            target = entry->link; seen = &seen_link;
        } else return ASTRA_VFS_ERR_PROTOCOL;
        if (*seen || value_length == 0u ||
            value_length >= ASTRA_VFS_PATH_MAX ||
            memchr(value, 0, value_length) != NULL)
            return ASTRA_VFS_ERR_PROTOCOL;
        memcpy(target, value, value_length);
        target[value_length] = '\0';
        *seen = 1;
        at = (uint32_t)(start + record);
    }
    return at == length ? ASTRA_VFS_OK : ASTRA_VFS_ERR_PROTOCOL;
}

static uint32_t next_entry(PackageInput *input, TarEntry *entry,
                           int *end_of_archive)
{
    uint32_t status = tar_next(input, entry, end_of_archive);
    if (status != ASTRA_VFS_OK || *end_of_archive || entry->type != 'x')
        return status;
    if (entry->length == 0u || entry->length > 2u * ASTRA_VFS_PATH_MAX + 128u)
        return ASTRA_VFS_ERR_PROTOCOL;
    uint32_t size = (uint32_t)entry->length;
    char *pax = astra_runtime_allocate(size);
    if (pax == NULL) return ASTRA_VFS_ERR_LIMIT;
    status = input_read(input, pax, size);
    if (status == ASTRA_VFS_OK) status = input_padding(input, size);
    TarEntry actual;
    if (status == ASTRA_VFS_OK)
        status = tar_next(input, &actual, end_of_archive);
    if (status == ASTRA_VFS_OK && (*end_of_archive || actual.type == 'x'))
        status = ASTRA_VFS_ERR_PROTOCOL;
    if (status == ASTRA_VFS_OK) status = pax_apply(pax, size, &actual);
    astra_runtime_deallocate(pax);
    if (status == ASTRA_VFS_OK) *entry = actual;
    return status;
}

typedef struct PackageMetadata {
    char id[ASTRA_BUNDLE_ID_MAX];
    char bundle[ASTRA_VFS_NAME_MAX];
    AstraBundleVersion version;
} PackageMetadata;

static int decimal(const char *text, uint16_t *number)
{
    uint32_t value = 0u;
    if (*text == '\0') return 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return 0;
        value = value * 10u + (uint32_t)(*text - '0');
        if (value > UINT16_MAX) return 0;
    }
    *number = (uint16_t)value;
    return 1;
}

static int parse_version(char *text, AstraBundleVersion *version)
{
    char *first = text;
    char *second = NULL;
    char *third = NULL;
    for (char *at = text; *at; ++at) {
        if (*at != '.') continue;
        *at = '\0';
        if (second == NULL) second = at + 1u;
        else if (third == NULL) third = at + 1u;
        else return 0;
    }
    return second != NULL && third != NULL &&
        decimal(first, &version->major) &&
        decimal(second, &version->minor) &&
        decimal(third, &version->patch) &&
        (version->major | version->minor | version->patch) != 0u;
}

static uint32_t parse_package_manifest(char *text, uint32_t length,
                                       PackageMetadata *meta)
{
    uint32_t seen = 0u;
    if (!astra_manifest_text_valid(text, length))
        return ASTRA_VFS_ERR_PROTOCOL;
    memset(meta, 0, sizeof(*meta));
    for (uint32_t at = 0u; at < length;) {
        char *line = text + at;
        uint32_t end = at;
        while (end < length && text[end] != '\n') ++end;
        text[end] = '\0';
        if (end > at && text[end - 1u] == '\r') text[end - 1u] = '\0';
        char *word[3];
        uint32_t count = astra_manifest_words(line, word, 3u);
        if (count == 0u) { at = end + 1u; continue; }
        if (count != 2u) return ASTRA_VFS_ERR_PROTOCOL;
        uint32_t bit = 0u;
        if (strcmp(word[0], "astra-package") == 0 &&
            strcmp(word[1], "1") == 0) bit = 1u;
        else if (strcmp(word[0], "id") == 0 &&
                 astra_string_copy(meta->id, sizeof(meta->id), word[1]))
            bit = 2u;
        else if (strcmp(word[0], "version") == 0 &&
                 parse_version(word[1], &meta->version)) bit = 4u;
        else if (strcmp(word[0], "architecture") == 0 &&
                 strcmp(word[1], "m68k-68040") == 0) bit = 8u;
        else if (strcmp(word[0], "bundle") == 0 &&
                 valid_component(word[1]) &&
                 astra_string_has_suffix(word[1], ".app") &&
                 astra_string_copy(meta->bundle, sizeof(meta->bundle),
                                   word[1])) bit = 16u;
        if (bit == 0u || (seen & bit) != 0u) return ASTRA_VFS_ERR_PROTOCOL;
        seen |= bit;
        at = end + 1u;
    }
    return seen == 31u ? ASTRA_VFS_OK : ASTRA_VFS_ERR_PROTOCOL;
}

static int ledger_mode(const char *text, uint16_t *mode)
{
    if (strlen(text) != 4u) return 0;
    uint16_t value = 0u;
    for (uint32_t at = 0u; at < 4u; ++at) {
        if (text[at] < '0' || text[at] > '7') return 0;
        value = (uint16_t)((value << 3) | (text[at] - '0'));
    }
    *mode = value;
    return 1;
}

static int ledger_length(const char *text, uint64_t *length)
{
    uint64_t value = 0u;
    if (*text == '\0') return 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9' ||
            value > (UINT32_MAX - (uint32_t)(*text - '0')) / 10u)
            return 0;
        value = value * 10u + (uint32_t)(*text - '0');
    }
    *length = value;
    return 1;
}

static int hex_digest(const char *text, uint8_t digest[32])
{
    if (strlen(text) != 64u) return 0;
    for (uint32_t at = 0u; at < 32u; ++at) {
        uint32_t value = 0u;
        for (uint32_t half = 0u; half < 2u; ++half) {
            char digit = text[at * 2u + half];
            if (digit >= '0' && digit <= '9') value = (value << 4) | (digit - '0');
            else if (digit >= 'a' && digit <= 'f') value = (value << 4) | (digit - 'a' + 10u);
            else return 0;
        }
        digest[at] = (uint8_t)value;
    }
    return 1;
}

static int payload_path(const char *path, const char *bundle)
{
    if (strncmp(path, "payload/", 8u) != 0) return 0;
    const char *at = path + 8u;
    size_t root = strlen(bundle);
    if (strncmp(at, bundle, root) != 0 ||
        (at[root] != '\0' && at[root] != '/')) return 0;
    while (*at != '\0') {
        const char *end = at;
        while (*end != '\0' && *end != '/') ++end;
        char name[ASTRA_VFS_NAME_MAX];
        size_t count = (size_t)(end - at);
        if (count == 0u || count >= sizeof(name)) return 0;
        memcpy(name, at, count); name[count] = '\0';
        if (!valid_component(name)) return 0;
        at = *end == '/' ? end + 1u : end;
        if (*end == '/' && *at == '\0') return 0;
    }
    return 1;
}

static uint32_t parse_ledger(char *text, uint32_t length,
    const PackageMetadata *meta, PackageList *list)
{
    for (uint32_t at = 0u; at < length;) {
        if (list->count >= PACKAGE_ENTRIES_MAX) return ASTRA_VFS_ERR_LIMIT;
        char *line = text + at;
        uint32_t end = at;
        while (end < length && text[end] != '\n') ++end;
        if (end == length) return ASTRA_VFS_ERR_PROTOCOL;
        text[end] = '\0';
        char *fields[6] = {line};
        uint32_t count = 1u;
        for (char *scan = line; *scan; ++scan)
            if (*scan == '\t') {
                if (count >= 6u) return ASTRA_VFS_ERR_PROTOCOL;
                *scan = '\0'; fields[count++] = scan + 1u;
            }
        PackageEntry entry = {0};
        if (count < 5u || strlen(fields[0]) != 1u ||
            !ledger_mode(fields[1], &entry.mode) ||
            !ledger_length(fields[2], &entry.length) ||
            !hex_digest(fields[3], entry.digest) ||
            !payload_path(fields[4], meta->bundle) ||
            (list->count != 0u &&
             strcmp(list->entries[list->count - 1u].path, fields[4]) >= 0))
            return ASTRA_VFS_ERR_PROTOCOL;
        entry.kind = fields[0][0] == 'd' ? ASTRA_VFS_KIND_DIRECTORY :
                     fields[0][0] == 'l' ? ASTRA_VFS_KIND_SYMLINK :
                     fields[0][0] == 'f' ? ASTRA_VFS_KIND_FILE : 0u;
        if (entry.kind == 0u ||
            (entry.kind == ASTRA_VFS_KIND_DIRECTORY &&
             (count != 5u || entry.mode != 0755u || entry.length != 0u)) ||
            (entry.kind == ASTRA_VFS_KIND_FILE &&
             (count != 5u ||
              (entry.mode != 0644u && entry.mode != 0755u))) ||
            (entry.kind == ASTRA_VFS_KIND_SYMLINK &&
             (count != 6u || entry.mode != 0777u ||
              entry.length != 0u ||
              !safe_link(fields[4] + 8u, fields[5]))))
            return ASTRA_VFS_ERR_PROTOCOL;
        if (list->count == 0u &&
            (entry.kind != ASTRA_VFS_KIND_DIRECTORY ||
             strcmp(fields[4] + 8u, meta->bundle) != 0))
            return ASTRA_VFS_ERR_PROTOCOL;
        if (list->count != 0u) {
            char *slash = NULL;
            for (char *scan = fields[4]; *scan; ++scan)
                if (*scan == '/') slash = scan;
            if (slash == NULL) return ASTRA_VFS_ERR_PROTOCOL;
            *slash = '\0';
            uint32_t low = 0u, high = list->count;
            while (low < high) {
                uint32_t middle = low + (high - low) / 2u;
                int order = strcmp(list->entries[middle].path, fields[4]);
                if (order < 0) low = middle + 1u;
                else high = middle;
            }
            int parent_ok = low < list->count &&
                strcmp(list->entries[low].path, fields[4]) == 0 &&
                list->entries[low].kind == ASTRA_VFS_KIND_DIRECTORY;
            *slash = '/';
            if (!parent_ok) return ASTRA_VFS_ERR_PROTOCOL;
        }
        AstraSha256 hash;
        if (entry.kind != ASTRA_VFS_KIND_FILE) {
            astra_sha256_init(&hash);
            if (entry.kind == ASTRA_VFS_KIND_SYMLINK)
                astra_sha256_update(&hash, fields[5], strlen(fields[5]));
            uint8_t digest[32];
            astra_sha256_final(&hash, digest);
            if (memcmp(digest, entry.digest, 32u) != 0)
                return ASTRA_VFS_ERR_PROTOCOL;
        }
        if (list->count == list->capacity) {
            uint32_t grown = list->capacity == 0u ? 16u : list->capacity * 2u;
            PackageEntry *entries = astra_runtime_reallocate(list->entries,
                (size_t)grown * sizeof(*entries));
            if (entries == NULL) return ASTRA_VFS_ERR_LIMIT;
            list->entries = entries;
            list->capacity = grown;
        }
        entry.path = astra_runtime_allocate(strlen(fields[4]) + 1u);
        if (count == 6u)
            entry.link = astra_runtime_allocate(strlen(fields[5]) + 1u);
        if (entry.path == NULL || (count == 6u && entry.link == NULL)) {
            astra_runtime_deallocate(entry.path);
            astra_runtime_deallocate(entry.link);
            return ASTRA_VFS_ERR_LIMIT;
        }
        strcpy(entry.path, fields[4]);
        if (count == 6u) strcpy(entry.link, fields[5]);
        list->entries[list->count++] = entry;
        at = end + 1u;
    }
    return list->count != 0u ? ASTRA_VFS_OK : ASTRA_VFS_ERR_PROTOCOL;
}

static uint32_t read_metadata(PackageInput *input, const char *name,
    char **text, uint32_t *length)
{
    TarEntry entry;
    int end = 0;
    uint32_t status = next_entry(input, &entry, &end);
    if (status != ASTRA_VFS_OK) return status;
    if (end || strcmp(entry.path, name) != 0 || entry.type != '0' ||
        entry.mode != 0644u || entry.link[0] != '\0' ||
        entry.length == 0u || entry.length > 4u * 1024u * 1024u)
        return ASTRA_VFS_ERR_PROTOCOL;
    char *bytes = astra_runtime_allocate((size_t)entry.length + 1u);
    if (bytes == NULL) return ASTRA_VFS_ERR_LIMIT;
    status = input_read(input, bytes, (uint32_t)entry.length);
    if (status == ASTRA_VFS_OK)
        status = input_padding(input, entry.length);
    if (status != ASTRA_VFS_OK) {
        astra_runtime_deallocate(bytes);
        return status;
    }
    bytes[entry.length] = '\0';
    *text = bytes;
    *length = (uint32_t)entry.length;
    return ASTRA_VFS_OK;
}

static uint32_t input_finish(PackageInput *input)
{
    for (;;) {
        uint32_t status = input_fill(input);
        if (status == ASTRA_VFS_ERR_NOT_FOUND) break;
        if (status != ASTRA_VFS_OK) return status;
        while (input->plain_at < input->plain_size)
            if (input->plain[input->plain_at++] != 0u)
                return ASTRA_VFS_ERR_PROTOCOL;
    }
    if (input->compressed_at != input->compressed_size)
        return ASTRA_VFS_ERR_PROTOCOL;
    uint8_t extra;
    uint32_t count = 0u;
    uint32_t status = astra_filesystem_read(input->file, &extra, 1u, &count);
    return status == ASTRA_VFS_OK && count == 0u ?
        ASTRA_VFS_OK : ASTRA_VFS_ERR_PROTOCOL;
}

static uint32_t extract_entry(PackageInput *input, AstraFilesystem *filesystem,
    const AstraFile *stage, const PackageEntry *expected, const TarEntry *tar)
{
    char type = expected->kind == ASTRA_VFS_KIND_DIRECTORY ? '5' :
                expected->kind == ASTRA_VFS_KIND_SYMLINK ? '2' : '0';
    if (strcmp(tar->path, expected->path) != 0 ||
        tar->mode != expected->mode || tar->length != expected->length ||
        tar->type != type ||
        strcmp(tar->link, expected->link == NULL ? "" : expected->link) != 0)
        return ASTRA_VFS_ERR_PROTOCOL;
    const char *relative = expected->path + sizeof("payload/") - 1u;
    if (expected->kind == ASTRA_VFS_KIND_DIRECTORY)
        return astra_filesystem_mkdir_at(filesystem, stage, relative,
                                          expected->mode);
    if (expected->kind == ASTRA_VFS_KIND_SYMLINK)
        return astra_filesystem_symlink_at(filesystem, expected->link,
                                            stage, relative);
    AstraFile output = ASTRA_FILE_INIT;
    uint32_t status = astra_filesystem_open_at_mode(filesystem, stage,
        relative, ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
        ASTRA_VFS_OPEN_EXCLUSIVE | ASTRA_VFS_OPEN_NOFOLLOW,
        expected->mode, &output);
    if (status != ASTRA_VFS_OK) return status;
    AstraSha256 hash;
    astra_sha256_init(&hash);
    uint8_t buffer[PACKAGE_CHUNK];
    for (uint64_t left = expected->length; status == ASTRA_VFS_OK && left;) {
        uint32_t count = left > sizeof(buffer) ? sizeof(buffer) :
                         (uint32_t)left;
        status = input_read(input, buffer, count);
        if (status != ASTRA_VFS_OK) break;
        astra_sha256_update(&hash, buffer, count);
        for (uint32_t at = 0u; at < count;) {
            uint32_t written = 0u;
            status = astra_filesystem_write(&output, UINT64_MAX, buffer + at,
                                            count - at, &written);
            if (status != ASTRA_VFS_OK || written == 0u) {
                if (status == ASTRA_VFS_OK) status = ASTRA_VFS_ERR_IO;
                break;
            }
            at += written;
        }
        left -= count;
    }
    if (status == ASTRA_VFS_OK) status = astra_filesystem_sync(&output);
    uint32_t closed = astra_filesystem_close(&output);
    if (status == ASTRA_VFS_OK) status = closed;
    if (status == ASTRA_VFS_OK) {
        uint8_t digest[32];
        astra_sha256_final(&hash, digest);
        if (memcmp(digest, expected->digest, 32u) != 0)
            status = ASTRA_VFS_ERR_PROTOCOL;
    }
    if (status == ASTRA_VFS_OK)
        status = input_padding(input, expected->length);
    return status;
}

static void remove_stage(AstraFilesystem *filesystem, const char *stage,
                         const PackageList *list)
{
    for (uint32_t at = list->count; at != 0u; --at) {
        const PackageEntry *entry = &list->entries[at - 1u];
        char *path = joined(stage, entry->path + 8u);
        if (path != NULL) {
            (void)astra_filesystem_unlink(filesystem, path,
                entry->kind == ASTRA_VFS_KIND_DIRECTORY ?
                    ASTRA_VFS_UNLINK_DIRECTORY : 0u);
            astra_runtime_deallocate(path);
        }
    }
    (void)astra_filesystem_unlink(filesystem, stage,
                                  ASTRA_VFS_UNLINK_DIRECTORY);
}

AstraResult astra_package_read_icon(AstraFilesystem *filesystem,
    const char *archive, void **bytes, uint32_t *length)
{
    AstraFile file = ASTRA_FILE_INIT;
    PackageInput input = {0};
    char *ledger = NULL, *preview = NULL;
    uint32_t ledger_bytes = 0u, preview_bytes = 0u;
    uint32_t status;
    int opened = 0;
    if (bytes == NULL || length == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    *bytes = NULL;
    *length = 0u;
    if (filesystem == NULL || archive == NULL || archive[0] != '/' ||
        !astra_string_has_suffix(archive, ".apkg"))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    status = astra_filesystem_open(filesystem, archive,
        ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_NOFOLLOW, &file);
    if (status != ASTRA_VFS_OK) goto done;
    opened = 1;
    input.file = &file;
    input.codec = ZSTD_createDCtx();
    input.compressed = astra_runtime_allocate(PACKAGE_CHUNK);
    input.plain = astra_runtime_allocate(PACKAGE_CHUNK);
    if (input.codec == NULL || input.compressed == NULL || input.plain == NULL) {
        status = ASTRA_VFS_ERR_LIMIT; goto done;
    }
    status = read_metadata(&input, "files.sha256", &ledger, &ledger_bytes);
    if (status == ASTRA_VFS_OK)
        status = read_metadata(&input, "icon.aicon", &preview,
                               &preview_bytes);
    if (status == ASTRA_VFS_OK &&
        (preview_bytes > 65536u ||
         astra_aicon_open(preview, preview_bytes, &(AstraAicon){0}) !=
             ASTRA_BUNDLE_OK))
        status = ASTRA_VFS_ERR_PROTOCOL;
    if (status == ASTRA_VFS_OK) {
        *bytes = preview;
        *length = preview_bytes;
        preview = NULL;
    }
done:
    astra_runtime_deallocate(ledger);
    astra_runtime_deallocate(preview);
    if (input.codec != NULL) ZSTD_freeDCtx(input.codec);
    astra_runtime_deallocate(input.compressed);
    astra_runtime_deallocate(input.plain);
    if (opened) {
        uint32_t closed = astra_filesystem_close(&file);
        if (status == ASTRA_VFS_OK) status = closed;
    }
    if (status != ASTRA_VFS_OK) {
        astra_runtime_deallocate(*bytes);
        *bytes = NULL;
        *length = 0u;
    }
    return package_result(status);
}

AstraResult astra_package_install_bundle(AstraFilesystem *filesystem,
    const char *archive, const char *apps_directory)
{
    AstraFile file = ASTRA_FILE_INIT;
    AstraFile stage_directory = ASTRA_FILE_INIT;
    PackageInput input = {0};
    PackageList list = {.filesystem = filesystem};
    PackageMetadata meta;
    AstraBundleManifest bundle_info = ASTRA_BUNDLE_MANIFEST_INIT;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    char *ledger = NULL, *manifest = NULL, *preview = NULL;
    char *stage = NULL, *destination = NULL, *source = NULL;
    uint32_t ledger_bytes = 0u, manifest_bytes = 0u, preview_bytes = 0u;
    uint32_t status = ASTRA_VFS_OK;
    int stage_created = 0, published = 0, file_opened = 0;
    int stage_opened = 0;
    if (filesystem == NULL || archive == NULL || apps_directory == NULL ||
        archive[0] != '/' || apps_directory[0] != '/' ||
        !astra_string_has_suffix(archive, ".apkg"))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    status = astra_filesystem_open(filesystem, archive,
        ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_NOFOLLOW, &file);
    if (status != ASTRA_VFS_OK) goto done;
    file_opened = 1;
    status = astra_filesystem_file_info(&file, &info);
    if (status != ASTRA_VFS_OK) goto done;
    if (info.kind != ASTRA_VFS_KIND_FILE || info.byte_size > UINT32_MAX) {
        status = ASTRA_VFS_ERR_LIMIT; goto done;
    }
    input.file = &file;
    input.codec = ZSTD_createDCtx();
    input.compressed = astra_runtime_allocate(PACKAGE_CHUNK);
    input.plain = astra_runtime_allocate(PACKAGE_CHUNK);
    if (input.codec == NULL || input.compressed == NULL || input.plain == NULL) {
        status = ASTRA_VFS_ERR_LIMIT; goto done;
    }
    status = read_metadata(&input, "files.sha256", &ledger, &ledger_bytes);
    if (status == ASTRA_VFS_OK)
        status = read_metadata(&input, "icon.aicon", &preview,
                               &preview_bytes);
    if (status == ASTRA_VFS_OK &&
        (preview_bytes > 65536u ||
         astra_aicon_open(preview, preview_bytes, &(AstraAicon){0}) !=
             ASTRA_BUNDLE_OK))
        status = ASTRA_VFS_ERR_PROTOCOL;
    if (status == ASTRA_VFS_OK)
        status = read_metadata(&input, "manifest", &manifest,
                               &manifest_bytes);
    if (status == ASTRA_VFS_OK)
        status = parse_package_manifest(manifest, manifest_bytes, &meta);
    if (status == ASTRA_VFS_OK)
        status = parse_ledger(ledger, ledger_bytes, &meta, &list);
    if (status != ASTRA_VFS_OK) goto done;
    destination = joined(apps_directory, meta.bundle);
    if (destination == NULL) { status = ASTRA_VFS_ERR_LIMIT; goto done; }
    status = astra_filesystem_lstat(filesystem, destination, &info);
    if (status == ASTRA_VFS_OK) { status = ASTRA_VFS_ERR_EXISTS; goto done; }
    if (status != ASTRA_VFS_ERR_NOT_FOUND) goto done;
    stage = astra_runtime_allocate(strlen(apps_directory) + 48u);
    if (stage == NULL) { status = ASTRA_VFS_ERR_LIMIT; goto done; }
    for (uint32_t attempt = 0u; attempt < 16u; ++attempt) {
        AstraString path;
        astra_string_init(&path, stage, (uint32_t)strlen(apps_directory) + 48u);
        if (!astra_string_append(&path, apps_directory) ||
            !astra_string_append(&path, "/.apkg-staging-") ||
            !astra_string_append_hex(&path,
                astra_clock_monotonic() ^ attempt, 16u)) {
            status = ASTRA_VFS_ERR_LIMIT; break;
        }
        status = astra_filesystem_mkdir_mode(filesystem, stage, 0700u);
        if (status == ASTRA_VFS_OK) { stage_created = 1; break; }
        if (status != ASTRA_VFS_ERR_EXISTS) break;
    }
    if (!stage_created) goto done;
    status = astra_filesystem_open(filesystem, stage,
        ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY |
        ASTRA_VFS_OPEN_NOFOLLOW, &stage_directory);
    if (status != ASTRA_VFS_OK) goto done;
    stage_opened = 1;
    TarEntry tar;
    int end = 0;
    status = next_entry(&input, &tar, &end);
    if (status != ASTRA_VFS_OK || end || strcmp(tar.path, "payload/") != 0 ||
        tar.type != '5' || tar.length != 0u || tar.mode != 0755u ||
        tar.link[0] != '\0') {
        if (status == ASTRA_VFS_OK) status = ASTRA_VFS_ERR_PROTOCOL;
        goto done;
    }
    for (uint32_t at = 0u; at < list.count && status == ASTRA_VFS_OK; ++at) {
        status = next_entry(&input, &tar, &end);
        if (status == ASTRA_VFS_OK && end) status = ASTRA_VFS_ERR_PROTOCOL;
        if (status == ASTRA_VFS_OK)
            status = extract_entry(&input, filesystem, &stage_directory,
                                   &list.entries[at], &tar);
    }
    if (status == ASTRA_VFS_OK) {
        status = next_entry(&input, &tar, &end);
        if (status == ASTRA_VFS_OK && !end) status = ASTRA_VFS_ERR_PROTOCOL;
    }
    if (status == ASTRA_VFS_OK) status = input_finish(&input);
    if (status != ASTRA_VFS_OK) goto done;
    source = joined(stage, meta.bundle);
    if (source == NULL) { status = ASTRA_VFS_ERR_LIMIT; goto done; }
    status = bundle_manifest(filesystem, source, &bundle_info);
    if (status != ASTRA_VFS_OK) goto done;
    if (strcmp(bundle_info.id, meta.id) != 0 ||
        bundle_info.version.major != meta.version.major ||
        bundle_info.version.minor != meta.version.minor ||
        bundle_info.version.patch != meta.version.patch) {
        status = ASTRA_VFS_ERR_PROTOCOL; goto done;
    }
    char icon_path[ASTRA_VFS_PATH_MAX];
    AstraString icon_name;
    astra_string_init(&icon_name, icon_path, sizeof(icon_path));
    if (!astra_string_append(&icon_name, "payload/") ||
        !astra_string_append(&icon_name, meta.bundle) ||
        !astra_string_append_char(&icon_name, '/') ||
        !astra_string_append(&icon_name, bundle_info.icon)) {
        status = ASTRA_VFS_ERR_LIMIT; goto done;
    }
    AstraSha256 preview_hash;
    uint8_t preview_digest[32];
    astra_sha256_init(&preview_hash);
    astra_sha256_update(&preview_hash, preview, preview_bytes);
    astra_sha256_final(&preview_hash, preview_digest);
    int icon_matches = 0;
    for (uint32_t at = 0u; at < list.count; ++at)
        if (strcmp(list.entries[at].path, icon_path) == 0 &&
            list.entries[at].kind == ASTRA_VFS_KIND_FILE &&
            list.entries[at].length == preview_bytes &&
            memcmp(list.entries[at].digest, preview_digest, 32u) == 0)
            icon_matches = 1;
    if (!icon_matches) { status = ASTRA_VFS_ERR_PROTOCOL; goto done; }
    status = astra_filesystem_lstat(filesystem, destination, &info);
    if (status == ASTRA_VFS_OK) { status = ASTRA_VFS_ERR_EXISTS; goto done; }
    if (status != ASTRA_VFS_ERR_NOT_FOUND) goto done;
    status = astra_filesystem_rename(filesystem, source, destination);
    if (status == ASTRA_VFS_OK) published = 1;
done:
    if (stage_opened)
        (void)astra_filesystem_close(&stage_directory);
    if (stage_created) remove_stage(filesystem, stage, &list);
    if (file_opened) {
        uint32_t closed = astra_filesystem_close(&file);
        if (status == ASTRA_VFS_OK) status = closed;
    }
    if (input.codec != NULL) ZSTD_freeDCtx(input.codec);
    astra_runtime_deallocate(input.compressed);
    astra_runtime_deallocate(input.plain);
    astra_runtime_deallocate(ledger);
    astra_runtime_deallocate(manifest);
    astra_runtime_deallocate(preview);
    astra_runtime_deallocate(stage);
    astra_runtime_deallocate(destination);
    astra_runtime_deallocate(source);
    astra_bundle_manifest_destroy(&bundle_info);
    destroy_entries(&list);
    if (published && status != ASTRA_VFS_OK) status = ASTRA_VFS_OK;
    return package_result(status);
}

static AstraResult package_result(uint32_t status)
{
    if (status == ASTRA_VFS_ERR_EXISTS) return ASTRA_ERROR_BUSY;
    if (status == ASTRA_VFS_ERR_NOT_FOUND) return ASTRA_ERROR_NOT_PRESENT;
    return astra_result_from_service(status);
}

AstraResult astra_package_create_bundle(AstraFilesystem *filesystem,
    const char *bundle, const char *archive)
{
    AstraBundleManifest bundle_info = ASTRA_BUNDLE_MANIFEST_INIT;
    PackageList list = {.filesystem = filesystem};
    PackageOutput output = {0};
    AstraFile file = ASTRA_FILE_INIT;
    AstraFileInfo existing = ASTRA_FILE_INFO_INIT;
    const char *leaf;
    char *root_path = NULL, *staging = NULL, *ledger = NULL, *icon_path = NULL;
    void *icon = NULL;
    uint32_t ledger_length = 0u, manifest_length = 0u, icon_length = 0u;
    char manifest[1024];
    uint32_t status = ASTRA_VFS_OK;
    int staging_created = 0;

    if (filesystem == NULL || bundle == NULL || archive == NULL ||
        bundle[0] != '/' || archive[0] != '/' ||
        !astra_string_has_suffix(bundle, ".app") ||
        !astra_string_has_suffix(archive, ".apkg"))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    leaf = bundle;
    for (const char *at = bundle; *at; ++at)
        if (*at == '/') leaf = at + 1u;
    if (!valid_component(leaf) ||
        strlen(bundle) >= ASTRA_VFS_PATH_MAX ||
        strlen(archive) + sizeof(".astra-packaging") >= ASTRA_VFS_PATH_MAX ||
        (strncmp(archive, bundle, strlen(bundle)) == 0 &&
         archive[strlen(bundle)] == '/'))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    status = astra_filesystem_lstat(filesystem, archive, &existing);
    if (status == ASTRA_VFS_OK) return ASTRA_ERROR_BUSY;
    if (status != ASTRA_VFS_ERR_NOT_FOUND) return package_result(status);
    status = bundle_manifest(filesystem, bundle, &bundle_info);
    if (status != ASTRA_VFS_OK) goto done;
    icon_path = joined(bundle, bundle_info.icon);
    status = icon_path == NULL ? ASTRA_VFS_ERR_LIMIT :
        read_blob(filesystem, icon_path, &icon, &icon_length);
    if (status != ASTRA_VFS_OK) goto done;
    if (icon_length > 65536u) {
        status = ASTRA_VFS_ERR_LIMIT; goto done;
    }
    status = make_manifest(&bundle_info, leaf, manifest, &manifest_length);
    if (status != ASTRA_VFS_OK) goto done;
    root_path = joined("payload", leaf);
    if (root_path == NULL) { status = ASTRA_VFS_ERR_LIMIT; goto done; }
    status = collect(&list, bundle, root_path, 0u);
    if (status != ASTRA_VFS_OK) goto done;
    sort_entries(&list);
    for (uint32_t at = 1u; at < list.count; ++at)
        if (strcmp(list.entries[at - 1u].path,
                   list.entries[at].path) == 0) {
            status = ASTRA_VFS_ERR_INVALID;
            goto done;
        }
    status = make_ledger(&list, &ledger, &ledger_length);
    if (status != ASTRA_VFS_OK) goto done;
    staging = astra_runtime_allocate(strlen(archive) +
                                     sizeof(".astra-packaging"));
    if (staging == NULL) { status = ASTRA_VFS_ERR_LIMIT; goto done; }
    strcpy(staging, archive);
    memcpy(staging + strlen(archive), ".astra-packaging",
           sizeof(".astra-packaging"));
    status = astra_filesystem_open_mode(filesystem, staging,
        ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
        ASTRA_VFS_OPEN_EXCLUSIVE, 0644u, &file);
    if (status != ASTRA_VFS_OK) goto done;
    staging_created = 1;
    output.file = &file;
    output.codec = ZSTD_createCCtx();
    output.capacity = PACKAGE_CHUNK;
    output.compressed = astra_runtime_allocate(output.capacity);
    if (output.codec == NULL || output.compressed == NULL ||
        ZSTD_isError(ZSTD_CCtx_setParameter(output.codec,
            ZSTD_c_compressionLevel, 1)) ||
        ZSTD_isError(ZSTD_CCtx_setParameter(output.codec,
            ZSTD_c_checksumFlag, 1)))
        status = ASTRA_VFS_ERR_LIMIT;
    if (status == ASTRA_VFS_OK)
        status = write_archive(&output, &list, manifest, manifest_length,
                               ledger, ledger_length, icon, icon_length);
    if (status == ASTRA_VFS_OK) status = astra_filesystem_sync(&file);
    if (staging_created) {
        uint32_t closed = astra_filesystem_close(&file);
        if (status == ASTRA_VFS_OK) status = closed;
    }
    if (status == ASTRA_VFS_OK)
        status = astra_filesystem_link(filesystem, staging, archive);
done:
    if (staging_created)
        (void)astra_filesystem_unlink(filesystem, staging, 0u);
    astra_runtime_deallocate(staging);
    astra_runtime_deallocate(root_path);
    astra_runtime_deallocate(icon_path);
    astra_runtime_deallocate(icon);
    astra_runtime_deallocate(ledger);
    astra_runtime_deallocate(output.compressed);
    if (output.codec != NULL) ZSTD_freeCCtx(output.codec);
    destroy_entries(&list);
    astra_bundle_manifest_destroy(&bundle_info);
    return package_result(status);
}
