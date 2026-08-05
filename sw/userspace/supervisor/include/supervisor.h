#ifndef ASTRA_SUPERVISOR_SERVICE_H
#define ASTRA_SUPERVISOR_SERVICE_H

#include <stdint.h>

#include <astra/process.h>
#include <astra/supervisor.h>

/*
 * What the supervisor observed about itself through the kernel ABI. Gathering
 * is separated from judging so the judgement runs on the host under the
 * sanitizers and the analyzer, where a trap instruction cannot.
 */
typedef struct SupervisorProbe {
    uint32_t query_status;
    uint32_t abi_version;
    uint32_t process_handle;
    uint32_t thread_handle;
    uint32_t info_status;
    AstraProcessInfo info;
} SupervisorProbe;

/*
 * Returns ASTRA_SUPERVISOR_STATUS_OK, or the tag with one bit set per failed
 * check. The capability table is passed separately because the startup block
 * carries its address as an integer.
 */
uint32_t supervisor_validate(const AstraStartupInfo *startup,
                             const AstraStartupCapability *capabilities,
                             const SupervisorProbe *probe);

#endif
