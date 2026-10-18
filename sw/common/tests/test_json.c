#include <astra/json.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static AstraJsonValue json(const char *text)
{
    return (AstraJsonValue){(const uint8_t *)text, (uint32_t)strlen(text)};
}

int main(void)
{
    static const char *valid[] = {
        "null", "true", "false", "0", "-0", "123", "-12.3e+4", "1E-2",
        "[]", "{}", " [ 1, true, null, {\"x\": []} ] ",
        "\"\\\"\\\\\\/\\b\\f\\n\\r\\t\\u0000\\ud83d\\ude00\"",
        "{\"license\":\"First\\nSecond \xce\xa9\"}"
    };
    static const char *invalid[] = {
        "", " ", "Null", "truefalse", "+1", ".1", "1.", "01", "-01", "-",
        "1e", "1e+", "NaN", "[1,]", "[,]", "[1 2]", "{,}", "{\"x\"}",
        "{\"x\":}", "{\"x\":false,}", "{\"x\":1 \"y\":2}", "{}[]",
        "\"raw\nnewline\"", "\"\\x20\"", "\"\\ud800\"", "\"\\udc00\"",
        "\"\\ud800\\u0041\"", "\"\xc0\xaf\"", "\"\xff\"", "\"unterminated"
    };
    for (uint32_t at = 0u; at < sizeof(valid) / sizeof(valid[0]); ++at)
        assert(astra_json_validate(json(valid[at])));
    for (uint32_t at = 0u; at < sizeof(invalid) / sizeof(invalid[0]); ++at)
        assert(!astra_json_validate(json(invalid[at])));

    AstraJsonValue root = json("{\"fa\\u006dily\":\"A\\u03a9\",\"notices\":["
        "{\"name_id\":13,\"text\":\"First\\r\\nSecond\\t\\ud83d\\ude00\"}]}");
    AstraJsonValue value, array, item;
    char output[128]; uint32_t count, bytes, number;
    assert(astra_json_member(root, "family", &value) == ASTRA_JSON_FOUND);
    assert(astra_json_string(value, output, sizeof(output), &bytes) &&
           bytes == 3u && memcmp(output, "A\xce\xa9", 4u) == 0);
    assert(astra_json_member(root, "missing", &value) == ASTRA_JSON_MISSING &&
           value.bytes == NULL && value.length == 0u);
    assert(astra_json_member(json("{\"x\":1,\"\\u0078\":2}"), "x", &value) == ASTRA_JSON_INVALID);
    assert(astra_json_member(root, "notices", &array) == ASTRA_JSON_FOUND);
    assert(astra_json_array_count(array, &count) && count == 1u);
    assert(astra_json_element(array, 0u, &item) == ASTRA_JSON_FOUND);
    assert(astra_json_member(item, "name_id", &value) == ASTRA_JSON_FOUND &&
           astra_json_u32(value, &number) && number == 13u);
    assert(astra_json_member(item, "text", &value) == ASTRA_JSON_FOUND);
    assert(astra_json_string(value, NULL, 0u, &bytes) && bytes == 18u);
    memset(output, 'Z', sizeof(output));
    assert(!astra_json_string(value, output, bytes, &count) && count == bytes);
    for (uint32_t at = 0u; at < sizeof(output); ++at) assert(output[at] == 'Z');
    assert(astra_json_string(value, output, sizeof(output), &bytes) &&
           memcmp(output, "First\r\nSecond\t\xf0\x9f\x98\x80", bytes + 1u) == 0);
    assert(astra_json_element(array, 1u, &item) == ASTRA_JSON_MISSING && item.bytes == NULL);
    assert(!astra_json_array_count(root, &count));
    assert(astra_json_member(array, "x", &item) == ASTRA_JSON_INVALID);
    assert(astra_json_u32(json("4294967295"), &number) && number == UINT32_MAX);
    assert(!astra_json_u32(json("4294967296"), &number));
    assert(!astra_json_u32(json("1e2"), &number));
    assert(!astra_json_u32(json("-1"), &number));
    assert(astra_json_string(json("\"A\\u0000B\""), output, sizeof(output), &bytes) &&
           bytes == 3u && memcmp(output, "A\0B\0", 4u) == 0);

    char nested[2u * (ASTRA_JSON_DEPTH_MAX + 1u) + 2u];
    for (uint32_t depth = ASTRA_JSON_DEPTH_MAX; depth <= ASTRA_JSON_DEPTH_MAX + 1u; ++depth) {
        memset(nested, '[', depth); nested[depth] = '0';
        memset(nested + depth + 1u, ']', depth); nested[2u * depth + 1u] = '\0';
        assert(astra_json_validate(json(nested)) == (depth == ASTRA_JSON_DEPTH_MAX));
    }
    const char *truncated = "{\"license\":\"\\ud83d\\ude00\",\"sources\":[1,2,3]}";
    for (uint32_t length = 0u; length < strlen(truncated); ++length)
        assert(!astra_json_validate((AstraJsonValue){(const uint8_t *)truncated, length}));
    puts("counted JSON validation and extraction passed");
    return 0;
}
