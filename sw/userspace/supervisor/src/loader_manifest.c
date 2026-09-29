#include <loader.h>

#include <astra/application_policy.h>
#include <astra/bytes.h>
#include <astra/manifest.h>
#include <astra/service_manager_abi.h>
#include <astra/runtime.h>
#include <astra/string.h>


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
        return allow_raw && astra_string_copy(name, ASTRA_CAPABILITY_NAME_MAX, token) &&
               authority_name_valid(name);
    }
    *colon++ = '\0';
    if (!astra_string_copy(name, ASTRA_CAPABILITY_NAME_MAX, token) ||
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

int supervisor_launch_admits(const SupervisorManifestEntry *ceiling,
                             const SupervisorManifestGrant *wanted)
{
#define RAW 0u
#define R ASTRA_RIGHT_READ
#define RW (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE)
#define ENTRY(name, rights) {name, rights},
    static const struct {
        const char *name;
        uint32_t rights;
    } application[] = { ASTRA_APPLICATION_CEILING(ENTRY) };
#undef ENTRY
#undef RW
#undef R
#undef RAW

    if (wanted == NULL)
        return 0;
    if (ceiling != NULL) {
        for (uint32_t at = 0u; at < ceiling->grant_count; ++at) {
            const SupervisorManifestGrant *allowed = &ceiling->grants[at];

            if (allowed->is_namespace == wanted->is_namespace &&
                strcmp(allowed->name, wanted->name) == 0 &&
                (wanted->rights & ~allowed->rights) == 0u)
                return 1;
        }
        return 0;
    }
    for (uint32_t at = 0u; at < sizeof(application) / sizeof(application[0]);
         ++at)
        if ((application[at].rights != 0u) == (wanted->is_namespace != 0u) &&
            strcmp(application[at].name, wanted->name) == 0 &&
            (wanted->rights & ~application[at].rights) == 0u)
            return 1;
    return 0;
}

uint32_t supervisor_service_start_policy(const char *word)
{
    return strcmp(word, "boot") == 0 ? ASTRA_SERVICE_START_BOOT :
           strcmp(word, "manual") == 0 ? ASTRA_SERVICE_START_MANUAL : 0u;
}

uint32_t supervisor_service_restart_policy(const char *word)
{
    return strcmp(word, "never") == 0 ? ASTRA_SERVICE_RESTART_NEVER :
           strcmp(word, "on-fault") == 0 ? ASTRA_SERVICE_RESTART_ON_FAULT :
           strcmp(word, "always") == 0 ? ASTRA_SERVICE_RESTART_ALWAYS :
                                         UINT32_MAX;
}

/* The words that end a service line's grant and serves lists. */
static int policy_word(const char *token)
{
    return strcmp(token, "required") == 0 ||
           strcmp(token, "critical") == 0 ||
           strncmp(token, "start=", 6u) == 0 ||
           strncmp(token, "restart=", 8u) == 0;
}

/*
 * `required`, `critical`, `start=boot|manual` and
 * `restart=never|on-fault|always`, in any order, each at most once, and
 * only on a service. A required or critical service has no start or restart
 * choice: it always runs, and what happens when it dies is fixed by its tier.
 */
static int parse_policy(char **token, uint32_t at, uint32_t count,
                        SupervisorManifestEntry *entry)
{
    int start_given = 0;
    int restart_given = 0;

    entry->start_policy = ASTRA_SERVICE_START_BOOT;
    entry->restart_policy = ASTRA_SERVICE_RESTART_ON_FAULT;
    for (; at < count; ++at) {
        if (strcmp(token[at], "required") == 0 && entry->required == 0u) {
            entry->required = 1u;
        } else if (strcmp(token[at], "critical") == 0 &&
                   entry->critical == 0u) {
            entry->critical = 1u;
        } else if (strncmp(token[at], "start=", 6u) == 0 && !start_given) {
            entry->start_policy =
                supervisor_service_start_policy(token[at] + 6u);
            if (entry->start_policy == 0u)
                return 0;
            start_given = 1;
        } else if (strncmp(token[at], "restart=", 8u) == 0 &&
                   !restart_given) {
            entry->restart_policy =
                supervisor_service_restart_policy(token[at] + 8u);
            if (entry->restart_policy == UINT32_MAX)
                return 0;
            restart_given = 1;
        } else {
            return 0;
        }
    }
    if (entry->resident == 0u &&
        (entry->required != 0u || entry->critical != 0u || start_given ||
         restart_given))
        return 0;
    if ((entry->required != 0u && entry->critical != 0u) ||
        ((entry->required != 0u || entry->critical != 0u) &&
         (start_given || restart_given)))
        return 0;
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
    else if (strcmp(token[at], "trusted") == 0)
        entry->trusted = 1u;
    else if (strcmp(token[at], "application") != 0)
        return 0;
    ++at;
    if (!astra_string_copy(entry->path, sizeof(entry->path), token[at++]) ||
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
           strcmp(token[at], "delegates") != 0 && !policy_word(token[at])) {
        SupervisorManifestGrant *grant;

        if (entry->grant_count == SUPERVISOR_MANIFEST_GRANT_MAX)
            return 0;
        grant = &entry->grants[entry->grant_count];
        if (!supervisor_manifest_grant(token[at], grant))
            return 0;
        ++entry->grant_count;
        ++at;
    }
    if (entry->trusted != 0u) {
        uint32_t length = (uint32_t)strlen(entry->path);

        /* A ceiling, not a process: it serves, delegates and requires
           nothing, and names exactly one application bundle. */
        if (at != count || strncmp(entry->path, "/apps/", 6u) != 0 ||
            length < 11u || strcmp(entry->path + length - 4u, ".app") != 0)
            return 0;
        for (uint32_t at_path = 6u; at_path < length; ++at_path)
            if (entry->path[at_path] == '/')
                return 0;
        return 1;
    }
    if (at < count && strcmp(token[at], "serves") == 0) {
        ++at;
        while (at < count && strcmp(token[at], "delegates") != 0 &&
               !policy_word(token[at])) {
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
    return parse_policy(token, at, count, entry);
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
