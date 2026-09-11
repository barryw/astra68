#include <astra/clipboard.h>
#include <astra/syscall.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

uint32_t astra_ndk_test_syscall(uint32_t number, uintptr_t d1, uintptr_t d2,
                                uintptr_t d3, uintptr_t d4, uintptr_t d5,
                                uint32_t *out_d1, uint32_t *out_d2)
{
    (void)number;
    (void)d1;
    (void)d2;
    (void)d3;
    (void)d4;
    (void)d5;
    (void)out_d1;
    (void)out_d2;
    assert(!"unexpected clipboard syscall");
    return ASTRA_SYSCALL_INVALID_ARGUMENT;
}

int main(void)
{
    static const char utf8[] = "Astra \xe2\x98\x85";
    static const char rich_type[] = "application/x-astra-text-runs";
    static const uint8_t rich[] = { 1u, 2u, 3u, 4u };
    AstraClipboardRepresentation representations[2] = {
        ASTRA_CLIPBOARD_REPRESENTATION_INIT,
        ASTRA_CLIPBOARD_REPRESENTATION_INIT,
    };
    uint8_t document[256];
    uint8_t unchanged[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };
    uint32_t bytes = 0u;

    representations[0].type = ASTRA_CLIPBOARD_TYPE_UTF8;
    representations[0].type_length = sizeof(ASTRA_CLIPBOARD_TYPE_UTF8) - 1u;
    representations[0].data = utf8;
    representations[0].data_length = sizeof(utf8) - 1u;
    representations[1].type = rich_type;
    representations[1].type_length = sizeof(rich_type) - 1u;
    representations[1].data = rich;
    representations[1].data_length = sizeof(rich);

    assert(astra_clipboard_document_write(
               NULL, 0u, representations, 2u, &bytes) ==
           ASTRA_ERROR_BUFFER_TOO_SMALL);
    assert(bytes > sizeof(AstraClipboardDocumentHeader));
    assert(astra_clipboard_document_write(
               unchanged, sizeof(unchanged), representations, 2u, &bytes) ==
           ASTRA_ERROR_BUFFER_TOO_SMALL);
    assert(memcmp(unchanged, "\1\2\3\4\5\6\7\10", 8u) == 0);
    assert(astra_clipboard_document_write(
               document, sizeof(document), representations, 2u, &bytes) ==
           ASTRA_OK);
    assert(astra_clipboard_document_validate(document, bytes) == ASTRA_OK);
    {
        AstraClipboardItem item = ASTRA_CLIPBOARD_ITEM_INIT;
        const void *found = NULL;
        uint32_t found_length = 0u;

        item._private_area.address = document;
        item._private_area.size = sizeof(document);
        item._private_area.map_flags = ASTRA_AREA_MAP_READ;
        item.generation = 1u;
        item._private_document_size = bytes;
        assert(astra_clipboard_item_find(
                   &item, ASTRA_CLIPBOARD_TYPE_UTF8,
                   sizeof(ASTRA_CLIPBOARD_TYPE_UTF8) - 1u,
                   &found, &found_length) == ASTRA_OK);
        assert(found_length == sizeof(utf8) - 1u &&
               memcmp(found, utf8, found_length) == 0);
        assert(astra_clipboard_item_find(
                   &item, "missing/type", 12u, &found, &found_length) ==
               ASTRA_ERROR_NOT_PRESENT);
    }

    document[0] ^= 1u;
    assert(astra_clipboard_document_validate(document, bytes) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    document[0] ^= 1u;
    {
        AstraClipboardDocumentRecord *records =
            (AstraClipboardDocumentRecord *)(void *)(
                document + sizeof(AstraClipboardDocumentHeader));
        uint32_t saved = records[0].data_offset;

        records[0].data_offset = bytes;
        assert(astra_clipboard_document_validate(document, bytes) ==
               ASTRA_ERROR_INVALID_ARGUMENT);
        records[0].data_offset = saved;
        document[saved] = (uint8_t)0xffu;
        assert(astra_clipboard_document_validate(document, bytes) ==
               ASTRA_ERROR_INVALID_ARGUMENT);
    }
    return 0;
}
