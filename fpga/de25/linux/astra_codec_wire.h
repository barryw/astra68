#ifndef ASTRA_CODEC_WIRE_H
#define ASTRA_CODEC_WIRE_H

#include <astra/codec_worker.h>
#include <stddef.h>
#include <stdint.h>

int64_t astra_codec_wire_now_ms(void);
/* Negative errno. Deadline -1 waits until peer closes. */
int astra_codec_wire_transfer(int socket, void *buffer, size_t bytes,
                              int output, int64_t deadline_ms);
int astra_codec_wire_message(int socket, AstraCodecWorkerMessage *message,
                             int output, int64_t deadline_ms);

#endif
