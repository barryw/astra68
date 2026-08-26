#include <loader.h>

#include <astra/bytes.h>
#include <astra/manifest.h>

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

static int authority(char *token, char *name, uint32_t *rights,
                     int allow_raw)
{
    char *colon = token;

    while (*colon != '\0' && *colon != ':')
        ++colon;
    if (*colon == '\0') {
        *rights = 0u;
        return allow_raw && copy(name, ASTRA_CAPABILITY_NAME_MAX, token);
    }
    *colon++ = '\0';
    if (!copy(name, ASTRA_CAPABILITY_NAME_MAX, token))
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
    if (!authority(text, grant->name, &grant->rights, 1))
        return 0;
    grant->is_namespace = grant->rights != 0u;
    return 1;
}

static int parse_line(char *line, SupervisorManifestEntry *entry)
{
    char *token[3u + SUPERVISOR_MANIFEST_GRANT_MAX + 4u];
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
        char *colon = entry->path;

        while (*colon != '\0' && *colon != ':')
            ++colon;
        if (colon == entry->path || *colon != ':' || colon[1] == '\0')
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
        if (at == count || !authority(token[at++], entry->serves,
                       &entry->serves_rights, 1))
            return 0;
    }
    if (at < count && strcmp(token[at], "delegates") == 0) {
        entry->delegates = 1u;
        ++at;
    }
    if (at < count && strcmp(token[at], "required") == 0) {
        entry->required = 1u;
        ++at;
    }
    return at == count;
}

int supervisor_manifest_parse(char *text, uint32_t length,
                              SupervisorManifest *manifest)
{
    SupervisorManifestEntry discard;
    uint32_t start = 0u;

    if (text == NULL || manifest == NULL || length == 0u)
        return 0;
    (void)memset(manifest, 0, sizeof(*manifest));
    for (uint32_t at = 0u; at <= length; ++at) {
        int result;
        char separator;

        if (at != length && text[at] != '\n' && text[at] != '\r')
            continue;
        separator = text[at];
        text[at] = '\0';
        result = parse_line(&text[start],
                            manifest->count < SUPERVISOR_MANIFEST_ENTRY_MAX ?
                                &manifest->entries[manifest->count] :
                                &discard);
        if (result == 0) {
            (void)memset(manifest, 0, sizeof(*manifest));
            return 0;
        }
        if (result == 1 &&
            manifest->count == SUPERVISOR_MANIFEST_ENTRY_MAX) {
            (void)memset(manifest, 0, sizeof(*manifest));
            return 0;
        }
        if (result == 1)
            ++manifest->count;
        if (separator == '\r' && at < length && text[at + 1u] == '\n')
            ++at;
        start = at + 1u;
    }
    return manifest->count != 0u;
}
