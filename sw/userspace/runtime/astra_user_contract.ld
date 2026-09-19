/* Shared Astra executable identity and virtual-memory contract. */
OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)

ASTRA_PAGE_SIZE = 0x1000;
/* Mirrored from astra/address_space.h because GNU ld scripts cannot include C
 * headers. tests/test_boundaries.py makes this duplication executable. */
ASTRA_TEXT_BASE = 0x00100000;
ASTRA_EXECUTABLE_ADDRESS_END = 0x20000000;
/* astra/event_descriptor.h's ASTRA_EVENT_CATALOG_BASE, which a linker script
 * cannot include. The runtime test checks that the two agree. */
ASTRA_EVENT_CATALOG_BASE = DEFINED(ASTRA_EVENT_IMAGE_CATALOG_BASE) ?
    ASTRA_EVENT_IMAGE_CATALOG_BASE : 0xE0000000;
/* astra/program.h's ASTRA_PROGRAM_SIZE, for the same reason. */
ASTRA_PROGRAM_SIZE = 120;
