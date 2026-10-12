#ifndef SUPERVISOR_BOOTSTRAP_HOST_H
#define SUPERVISOR_BOOTSTRAP_HOST_H

#include <stdint.h>
#include <astra/process.h>

/* Logs the pre-service provider comparison. Absence/mismatch is nonfatal;
 * cleanup failures are returned so the loader cannot strand the claim. */
uint32_t supervisor_bootstrap_probe(const AstraStartupInfo *startup,
                                    const void *expected, uint32_t length);

#endif
