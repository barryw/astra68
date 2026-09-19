#ifndef ASTRA_ENTROPY_SERVICE_H
#define ASTRA_ENTROPY_SERVICE_H

#include <stdint.h>

#include <astra/entropy.h>

int astra_entropy_request_valid(const AstraEntropyRequest *request,
                                uint32_t size, uint32_t handle_count);
void astra_entropy_reply_init(AstraEntropyReply *reply,
                              const AstraEntropyRequest *request,
                              uint32_t status, const void *bytes,
                              uint32_t length);

#endif
