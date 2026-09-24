#ifndef ASTRA_MEDIA_PCM_PROTOCOL_H
#define ASTRA_MEDIA_PCM_PROTOCOL_H

#include <astra/pcm_service.h>
#include <stdint.h>

int astra_pcm_request_valid(const AstraPcmRequest *request, uint32_t size,
                            int factory);

#endif
