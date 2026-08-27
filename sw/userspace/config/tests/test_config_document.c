#include <astra/config_document.h>
#include <astra/config_library.h>

#include <assert.h>
#include <string.h>

static const char document[] =
    "# A person is expected to edit this.\r\n"
    "astra-config = 1\r\n"
    "schema 1\n"
    "\n"
    "pool pool.ntp.org\n"
    "server = time.example.net   # fallback\n"
    "label = \"office clock\\nprimary\"\n"
    "hash \"value # not a comment\"\n"
    "words = an unquoted value with spaces\n"
    "signed = -9223372036854775808\n"
    "unsigned = 18446744073709551615\n"
    "enabled = YeS\n"
    "retry = 3\n"
    "retry = 8\n"
    "feature = true\n"
    "feature = off\n";

static void reads_forgiving_syntax(void)
{
    AstraConfigDocumentError error;
    char value[64];
    uint32_t count = 0u, length = 0u;

    assert(astra_config_document_validate(
               document, sizeof(document) - 1u, 1u, &error) ==
           ASTRA_CONFIG_OK);
    assert(astra_config_document_count(document, sizeof(document) - 1u,
                                       "server", &count) == ASTRA_CONFIG_OK);
    assert(count == 1u);
    assert(astra_config_document_get(document, sizeof(document) - 1u,
                                     "server", 0u, value, sizeof(value),
                                     &length) == ASTRA_CONFIG_OK);
    assert(length == strlen("time.example.net"));
    assert(strcmp(value, "time.example.net") == 0);
    assert(astra_config_document_get(document, sizeof(document) - 1u,
                                     "label", 0u, value, sizeof(value),
                                     &length) == ASTRA_CONFIG_OK);
    assert(strcmp(value, "office clock\nprimary") == 0);
    assert(astra_config_document_get(document, sizeof(document) - 1u,
                                     "hash", 0u, value, sizeof(value),
                                     &length) == ASTRA_CONFIG_OK);
    assert(strcmp(value, "value # not a comment") == 0);
    assert(astra_config_document_get(document, sizeof(document) - 1u,
                                     "words", 0u, value, sizeof(value),
                                     &length) == ASTRA_CONFIG_OK);
    assert(strcmp(value, "an unquoted value with spaces") == 0);
}

static void reports_damage_without_guessing(void)
{
    AstraConfigDocumentError error;
    static const char bad_quote[] =
        "astra-config 1\nschema 1\nname \"unfinished\n";
    static const char future[] = "astra-config 1\nschema 2\n";
    static const char bad_escape[] =
        "astra-config 1\nschema 1\nname \"bad\\q\"\n";

    assert(astra_config_document_validate(
               bad_quote, sizeof(bad_quote) - 1u, 1u, &error) ==
           ASTRA_CONFIG_MALFORMED);
    assert(error.line == 3u);
    assert(astra_config_document_validate(
               bad_escape, sizeof(bad_escape) - 1u, 1u, &error) ==
           ASTRA_CONFIG_MALFORMED);
    assert(error.line == 3u);
    assert(astra_config_document_validate(
               future, sizeof(future) - 1u, 1u, &error) ==
           ASTRA_CONFIG_UNSUPPORTED_VERSION);
    assert(error.line == 2u && error.version == 2u);
}

static void edits_preserve_the_rest(void)
{
    char changed[512];
    char removed[512];
    char value[64];
    uint32_t required = 0u, length = 0u;

    assert(astra_config_document_replace(
               document, sizeof(document) - 1u, "server", 0u,
               "time two\nwith a #", changed, sizeof(changed), &required) ==
           ASTRA_CONFIG_OK);
    assert(strstr(changed, "# A person is expected to edit this.\r\n") !=
           NULL);
    assert(strstr(changed, "server = \"time two\\nwith a #\"\n") != NULL);
    assert(astra_config_document_get(changed, required - 1u, "server", 0u,
                                     value, sizeof(value), &length) ==
           ASTRA_CONFIG_OK);
    assert(strcmp(value, "time two\nwith a #") == 0);

    assert(astra_config_document_replace(
               changed, required - 1u, "server", 1u, "third.example",
               removed, sizeof(removed), &required) == ASTRA_CONFIG_OK);
    assert(astra_config_document_count(removed, required - 1u, "server",
                                       &length) == ASTRA_CONFIG_OK);
    assert(length == 2u);
    assert(astra_config_document_remove(
               removed, required - 1u, "server", 0u, changed,
               sizeof(changed), &required) == ASTRA_CONFIG_OK);
    assert(astra_config_document_get(changed, required - 1u, "server", 0u,
                                     value, sizeof(value), &length) ==
           ASTRA_CONFIG_OK);
    assert(strcmp(value, "third.example") == 0);
}

static void reads_typed_scalars(void)
{
    int64_t signed_value = 0;
    uint64_t unsigned_value = 0u;
    int enabled = 0;

    assert(astra_config_document_get_i64(
               document, sizeof(document) - 1u, "signed", 0u,
               &signed_value) == ASTRA_CONFIG_OK);
    assert(signed_value == INT64_MIN);
    assert(astra_config_document_get_u64(
               document, sizeof(document) - 1u, "unsigned", 0u,
               &unsigned_value) == ASTRA_CONFIG_OK);
    assert(unsigned_value == UINT64_MAX);
    assert(astra_config_document_get_bool(
               document, sizeof(document) - 1u, "enabled", 0u,
               &enabled) == ASTRA_CONFIG_OK);
    assert(enabled == 1);
}

static void reads_typed_lists(void)
{
    int64_t retry = 0;
    int feature = 0;
    uint32_t count = 0u;

    assert(astra_config_document_count(
               document, sizeof(document) - 1u, "retry", &count) ==
           ASTRA_CONFIG_OK);
    assert(count == 2u);
    assert(astra_config_document_get_i64(
               document, sizeof(document) - 1u, "retry", 1u, &retry) ==
           ASTRA_CONFIG_OK);
    assert(retry == 8);
    assert(astra_config_document_get_bool(
               document, sizeof(document) - 1u, "feature", 0u, &feature) ==
           ASTRA_CONFIG_OK);
    assert(feature == 1);
    assert(astra_config_document_get_bool(
               document, sizeof(document) - 1u, "feature", 1u, &feature) ==
           ASTRA_CONFIG_OK);
    assert(feature == 0);
}

static void sizes_are_queries_not_limits(void)
{
    char value[4];
    uint32_t required = 0u;

    assert(astra_config_document_get(document, sizeof(document) - 1u,
                                     "words", 0u, value, sizeof(value),
                                     &required) ==
           ASTRA_CONFIG_BUFFER_TOO_SMALL);
    assert(required == strlen("an unquoted value with spaces"));
    assert(astra_config_document_replace(
               document, sizeof(document) - 1u, "new-key", 0u, "value",
               NULL, 0u, &required) == ASTRA_CONFIG_BUFFER_TOO_SMALL);
    assert(required > sizeof(document));
}

static void scopes_owners_without_leaking_them_to_programs(void)
{
    char root[96];

    assert(astra_config_capability_root(
               "config", ASTRA_CONFIG_OWNER_SERVICE, "ntpd", root,
               sizeof(root)) == ASTRA_CONFIG_OK);
    assert(strcmp(root, "config/services/ntpd") == 0);
    assert(astra_config_capability_root(
               "config", ASTRA_CONFIG_OWNER_APPLICATION, "Terminal", root,
               sizeof(root)) == ASTRA_CONFIG_OK);
    assert(strcmp(root, "config/applications/Terminal") == 0);
    assert(astra_config_capability_root(
               "config", ASTRA_CONFIG_OWNER_COMMAND, "../ping", root,
               sizeof(root)) == ASTRA_CONFIG_INVALID);
    assert(astra_config_capability_root(
               "config", ASTRA_CONFIG_OWNER_SYSTEM, "network", root, 8u) ==
           ASTRA_CONFIG_BUFFER_TOO_SMALL);
    assert(astra_config_scope_root(
               "config", ASTRA_CONFIG_OWNER_COMMAND, root, sizeof(root)) ==
           ASTRA_CONFIG_OK);
    assert(strcmp(root, "config/commands") == 0);
    assert(astra_config_owner_root("config/commands", "ping", root,
                                   sizeof(root)) == ASTRA_CONFIG_OK);
    assert(strcmp(root, "config/commands/ping") == 0);
}

int main(void)
{
    reads_forgiving_syntax();
    reports_damage_without_guessing();
    edits_preserve_the_rest();
    reads_typed_scalars();
    reads_typed_lists();
    sizes_are_queries_not_limits();
    scopes_owners_without_leaking_them_to_programs();
    return 0;
}
