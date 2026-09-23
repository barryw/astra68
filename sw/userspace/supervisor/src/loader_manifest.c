#include <loader.h>

#include <astra/bytes.h>
#include <astra/manifest.h>
#include <astra/runtime.h>


static int copy(char *out, uint32_t capacity, const char *text)
{
    uint32_t at = 0u;

    while (text[at] != '\0') {
        if (at + 1u >= capacity)
            return 0;
        out[at] = text[at];
        ++at;
    }
    out[at] = '\0';
    return 1;
}

static int authority_name_valid(const char *name)
{
    if (name[0] == '\0')
        return 0;
    for (; *name != '\0'; ++name)
        if (*name == '/')
            return 0;
    return 1;
}

int supervisor_manifest_authority(char *token, char *name, uint32_t *rights,
                                  int allow_raw)
{
    char *colon = token;

    while (*colon != '\0' && *colon != ':')
        ++colon;
    if (*colon == '\0') {
        *rights = 0u;
        return allow_raw && copy(name, ASTRA_CAPABILITY_NAME_MAX, token) &&
               authority_name_valid(name);
    }
    *colon++ = '\0';
    if (!copy(name, ASTRA_CAPABILITY_NAME_MAX, token) ||
        !authority_name_valid(name))
        return 0;
    if (strcmp(colon, "r") == 0) {
        *rights = ASTRA_RIGHT_READ;
        return 1;
    }
    if (strcmp(colon, "rw") == 0) {
        *rights = ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE;
        return 1;
    }
    return 0;
}

int supervisor_manifest_grant(char *text, SupervisorManifestGrant *grant)
{
    if (text == NULL || grant == NULL)
        return 0;
    (void)memset(grant, 0, sizeof(*grant));
    if (!supervisor_manifest_authority(text, grant->name, &grant->rights, 1))
        return 0;
    grant->is_namespace = grant->rights != 0u;
    return 1;
}

static int parse_line(char *line, SupervisorManifestEntry *entry)
{
    char *token[3u + SUPERVISOR_MANIFEST_GRANT_MAX +
                SUPERVISOR_MANIFEST_PUBLICATION_MAX + 4u];
    uint32_t count = astra_manifest_words(
        line, token, sizeof(token) / sizeof(token[0]));
    uint32_t at = 0u;

    if (count == 0u)
        return 2;
    if (count > sizeof(token) / sizeof(token[0]) || count < 3u)
        return 0;
    (void)memset(entry, 0, sizeof(*entry));
    if (strcmp(token[at], "service") == 0)
        entry->resident = 1u;
    else if (strcmp(token[at], "application") != 0)
        return 0;
    ++at;
    if (!copy(entry->path, sizeof(entry->path), token[at++]) ||
        strcmp(entry->path, "") == 0 || strcmp(token[at++], "grants") != 0)
        return 0;
    {
        const char *separator = entry->path + 1u;

        if (entry->path[0] != '/' || entry->path[1] == '/')
            return 0;
        while (*separator != '\0' && *separator != '/') {
            if (*separator == ':')
                return 0;
            ++separator;
        }
        if (*separator != '/' || separator[1] == '\0')
            return 0;
    }

    while (at < count && strcmp(token[at], "serves") != 0 &&
           strcmp(token[at], "delegates") != 0 &&
           strcmp(token[at], "required") != 0) {
        SupervisorManifestGrant *grant;

        if (entry->grant_count == SUPERVISOR_MANIFEST_GRANT_MAX)
            return 0;
        grant = &entry->grants[entry->grant_count];
        if (!supervisor_manifest_grant(token[at], grant))
            return 0;
        ++entry->grant_count;
        ++at;
    }
    if (at < count && strcmp(token[at], "serves") == 0) {
        ++at;
        while (at < count && strcmp(token[at], "delegates") != 0 &&
               strcmp(token[at], "required") != 0) {
            SupervisorManifestPublication *publication;

            if (entry->serves_count ==
                    SUPERVISOR_MANIFEST_PUBLICATION_MAX)
                return 0;
            publication = &entry->serves[entry->serves_count];
            if (!supervisor_manifest_authority(
                    token[at++], publication->name,
                    &publication->rights, 1))
                return 0;
            ++entry->serves_count;
        }
        if (entry->serves_count == 0u)
            return 0;
    }
    if (at < count && strcmp(token[at], "delegates") == 0) {
        entry->delegates = 1u;
        ++at;
    }
    if (at < count && strcmp(token[at], "required") == 0) {
        if (entry->resident == 0u)
            return 0;
        entry->required = 1u;
        ++at;
    }
    return at == count;
}

void supervisor_manifest_destroy(SupervisorManifest *manifest)
{
    if (manifest == NULL)
        return;
    astra_runtime_deallocate(manifest->entries);
    manifest->entries = NULL;
    manifest->count = 0u;
    manifest->capacity = 0u;
}

static int manifest_reserve(SupervisorManifest *manifest)
{
    SupervisorManifestEntry *grown;
    uint32_t capacity = manifest->capacity == 0u ? 8u :
                        manifest->capacity * 2u;

    if (capacity <= manifest->capacity)
        return 0;
#if SIZE_MAX < UINT32_MAX
    if (capacity > SIZE_MAX / sizeof(*manifest->entries))
        return 0;
#endif
    grown = astra_runtime_reallocate(
        manifest->entries, (size_t)capacity * sizeof(*manifest->entries));
    if (grown == NULL)
        return 0;
    manifest->entries = grown;
    manifest->capacity = capacity;
    return 1;
}

int supervisor_manifest_parse(char *text, uint32_t length,
                              SupervisorManifest *manifest)
{
    uint32_t start = 0u;

    if (text == NULL || manifest == NULL || length == 0u)
        return 0;
    supervisor_manifest_destroy(manifest);
    while (start < length) {
        uint32_t at = start;
        SupervisorManifestEntry entry;
        char *line;
        char *owned = NULL;
        int result;

        while (at < length && text[at] != '\n' && text[at] != '\r')
            ++at;
        if (at < length) {
            char separator = text[at];

            text[at] = '\0';
            line = &text[start];
            if (separator == '\r' && at + 1u < length &&
                text[at + 1u] == '\n')
                ++at;
        } else {
            uint32_t tail = length - start;

            owned = astra_runtime_reallocate(NULL, (size_t)tail + 1u);
            if (owned == NULL) {
                supervisor_manifest_destroy(manifest);
                return 0;
            }
            (void)memcpy(owned, &text[start], tail);
            owned[tail] = '\0';
            line = owned;
        }
        result = parse_line(line, &entry);
        astra_runtime_deallocate(owned);
        if (result == 0) {
            supervisor_manifest_destroy(manifest);
            return 0;
        }
        if (result == 1) {
            if (manifest->count == manifest->capacity &&
                !manifest_reserve(manifest)) {
                supervisor_manifest_destroy(manifest);
                return 0;
            }
            manifest->entries[manifest->count] = entry;
            ++manifest->count;
        }
        start = at + (at < length ? 1u : 0u);
    }
    return manifest->count != 0u;
}
