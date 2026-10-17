#include "astra_font_convert.h"
#include "astra_font_worker.h"

#include <astra/font_worker.h>
#include <astra/font_encoding.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint32_t decode32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
           (uint32_t)bytes[2] << 8 | bytes[3];
}

static void encode32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static int transfer_exact(int fd, void *buffer, size_t size, int output)
{
    uint8_t *bytes = buffer;
    while (size != 0u) {
        ssize_t moved = output ? write(fd, bytes, size) : read(fd, bytes, size);
        if (moved < 0 && errno == EINTR) continue;
        if (moved <= 0) return 0;
        bytes += moved;
        size -= (size_t)moved;
    }
    return 1;
}

int astra_font_worker_run(int input, int output)
{
    uint8_t request[ASTRA_FONT_WORKER_REQUEST_SIZE];
    uint8_t reply[ASTRA_FONT_WORKER_REPLY_SIZE] = {0};
    uint8_t *source = NULL, *afnt = NULL;
    uint32_t source_size, name_size, license_size, face_index, encoding;
    uint32_t afnt_size = 0u, faces = 0u;
    char name[ASTRA_FONT_WORKER_TEXT_MAX + 1u];
    char license[ASTRA_FONT_WORKER_TEXT_MAX + 1u];
    int result = ASTRA_FONT_CONVERT_INVALID;
    int delivered;

    if (!transfer_exact(input, request, sizeof(request), 0)) return 1;
    source_size = decode32(request + 12u);
    name_size = decode32(request + 16u);
    license_size = decode32(request + 20u);
    face_index = decode32(request + 8u);
    encoding = decode32(request + 24u);
    if (memcmp(request, ASTRA_FONT_WORKER_REQUEST_MAGIC, 4u) != 0 ||
        decode32(request + 4u) != ASTRA_FONT_WORKER_VERSION ||
        source_size == 0u || source_size > ASTRA_FONT_WORKER_SOURCE_MAX ||
        name_size == 0u || name_size > ASTRA_FONT_WORKER_TEXT_MAX ||
        license_size > ASTRA_FONT_WORKER_TEXT_MAX ||
        face_index >= ASTRA_FONT_WORKER_FACES_MAX ||
        encoding > ASTRA_FONT_ENCODING_ISO_8859_2)
        goto done;
    if (!transfer_exact(input, name, name_size, 0) ||
        !transfer_exact(input, license, license_size, 0) ||
        memchr(name, '\0', name_size) != NULL ||
        memchr(license, '\0', license_size) != NULL)
        goto done;
    name[name_size] = '\0';
    license[license_size] = '\0';
    source = malloc(source_size);
    if (source == NULL) {
        result = ASTRA_FONT_CONVERT_NOMEM;
        goto done;
    }
    if (!transfer_exact(input, source, source_size, 0)) goto done;
    if (source_size >= 4u &&
        memcmp(source, "\0\0\3\363", 4u) == 0) {
        const char *codepage = encoding == ASTRA_FONT_ENCODING_ISO_8859_1 ?
            "ISO-8859-1" : encoding == ASTRA_FONT_ENCODING_ISO_8859_2 ?
            "ISO-8859-2" : NULL;
        result = codepage == NULL || face_index != 0u ?
            ASTRA_FONT_CONVERT_INVALID :
            astra_font_convert_amiga(source, source_size, codepage,
                name, license, &afnt, &afnt_size, &faces);
    } else if (encoding == ASTRA_FONT_ENCODING_AUTO) {
        result = astra_font_convert_outline(source, source_size, face_index,
                                            name, license, &afnt, &afnt_size,
                                            &faces);
    }
    if (result == ASTRA_FONT_CONVERT_OK &&
        (afnt == NULL || afnt_size == 0u ||
         afnt_size > ASTRA_FONT_WORKER_OUTPUT_MAX)) {
        result = ASTRA_FONT_CONVERT_LIMIT;
        afnt_size = 0u;
    }
done:
    memcpy(reply, ASTRA_FONT_WORKER_REPLY_MAGIC, 4u);
    encode32(reply + 4u, ASTRA_FONT_WORKER_VERSION);
    encode32(reply + 8u, (uint32_t)result);
    encode32(reply + 12u, faces);
    encode32(reply + 16u, result == ASTRA_FONT_CONVERT_OK ? afnt_size : 0u);
    delivered = transfer_exact(output, reply, sizeof(reply), 1);
    if (delivered && result == ASTRA_FONT_CONVERT_OK)
        delivered = transfer_exact(output, afnt, afnt_size, 1);
    free(afnt);
    free(source);
    return delivered ? 0 : 1;
}
