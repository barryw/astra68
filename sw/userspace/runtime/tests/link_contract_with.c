/*
 * Half of the link contract: an image that declares its provenance links, and
 * what it declared is still there afterwards.
 *
 * The other half is link_contract_without.c, which must fail to link. Both are
 * linked by the runtime's `link-contract` target against the real
 * astra_user.ld, with --gc-sections on, because the collector is the reason the
 * section needs KEEP and a test that left it off would prove nothing.
 *
 * The version is 1.2.3 rather than 1.0.0 so that a reader printing the record
 * back cannot pass by printing zeroes.
 */

#include <astra/program.h>

ASTRA_PROGRAM("contract", 1, 2, 3, "Barry Walker",
              "Copyright 2026 Barry Walker");

void _start(void);

void
_start(void)
{
}
