#ifndef ASTRA_RUNTIME_LOG_TEST_HOOKS_H
#define ASTRA_RUNTIME_LOG_TEST_HOOKS_H

/*
 * Force-included (-include) into every source of the host runtime test, so
 * the -D overrides of ASTRA_LOG_STARTUP_ADDRESS and ASTRA_TIME_PAGE_ADDRESS
 * name declared functions. The fixed startup block and time page addresses
 * cannot be mapped by a host process.
 */

#include <stdint.h>

struct AstraStartupInfo;

const struct AstraStartupInfo *astra_test_startup(void);
uintptr_t astra_test_time_page_address(void);
/* log.c: forget the ring and the retry back-off (ASTRA_LOG_TEST builds). */
void astra_log_test_reset(void);

#endif
