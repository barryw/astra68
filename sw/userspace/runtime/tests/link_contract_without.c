/*
 * The other half of the link contract: an image that declares no provenance
 * must not link. astra_user.ld's ASSERT is what refuses it.
 *
 * This file compiles cleanly on purpose. The rule is not "a program must
 * include a header" -- a rule a compiler enforces is one somebody works around
 * by not including the header. It is "an image must carry the record", checked
 * where images are made.
 */

void _start(void);

void
_start(void)
{
}
