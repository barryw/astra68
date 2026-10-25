#ifndef ASTRA_FONT_WORKER_H
#define ASTRA_FONT_WORKER_H

/* Private HPS font-worker wire format. All integers are big-endian. */
#define ASTRA_FONT_WORKER_REQUEST_MAGIC "FNTC"
#define ASTRA_FONT_WORKER_REPLY_MAGIC "FNTR"
#define ASTRA_FONT_WORKER_VERSION 2u
#define ASTRA_FONT_WORKER_REQUEST_SIZE 28u
#define ASTRA_FONT_WORKER_REPLY_SIZE 20u
#define ASTRA_FONT_WORKER_SOURCE_MAX (32u * 1024u * 1024u)
#define ASTRA_FONT_WORKER_OUTPUT_MAX (128u * 1024u * 1024u)
#define ASTRA_FONT_WORKER_TEXT_MAX 255u
#define ASTRA_FONT_WORKER_FACES_MAX 64u

/* Request: magic, version, face index, source bytes, name bytes, license bytes,
 * source encoding.
 * Reply: magic, version, conversion status, collection faces, AFNT bytes.
 * Name, license, then source follow request. AFNT follows successful reply.
 * A worker handles one request, then exits. No filesystem path is transferred. */

#endif
