/*
 * Astra68 MC68030 PMMU model
 *
 * This is an independent implementation from the programmer-visible
 * behaviour documented in Motorola's MC68030 User's Manual and M68000
 * Family Programmer's Reference Manual.  It contains no code from the PMMU
 * implementation that upstream Musashi distributes.
 */

#ifndef ASTRA_PMMU030_H
#define ASTRA_PMMU030_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PMMU030_ATC_ENTRIES 22u

#define PMMU030_TC_ENABLE UINT32_C(0x80000000)
#define PMMU030_TC_SRE    UINT32_C(0x02000000)
#define PMMU030_TC_FCL    UINT32_C(0x01000000)
#define PMMU030_TC_MASK   UINT32_C(0x83ffffff)

#define PMMU030_TT_ENABLE UINT32_C(0x00008000)
#define PMMU030_TT_MASK   UINT32_C(0xffff8777)

#define PMMU030_RP_MASK_HI UINT32_C(0xffff0003)
#define PMMU030_RP_MASK_LO UINT32_C(0xfffffff0)

#define PMMU030_MMUSR_B UINT16_C(0x8000)
#define PMMU030_MMUSR_L UINT16_C(0x4000)
#define PMMU030_MMUSR_S UINT16_C(0x2000)
#define PMMU030_MMUSR_W UINT16_C(0x0800)
#define PMMU030_MMUSR_I UINT16_C(0x0400)
#define PMMU030_MMUSR_M UINT16_C(0x0200)
#define PMMU030_MMUSR_T UINT16_C(0x0040)
#define PMMU030_MMUSR_N UINT16_C(0x0007)
#define PMMU030_MMUSR_MASK UINT16_C(0xee47)

typedef enum pmmu030_access {
    PMMU030_ACCESS_READ = 0,
    PMMU030_ACCESS_WRITE,
    PMMU030_ACCESS_RMW
} pmmu030_access;

typedef enum pmmu030_fault {
    PMMU030_FAULT_NONE = 0,
    PMMU030_FAULT_INVALID,
    PMMU030_FAULT_LIMIT,
    PMMU030_FAULT_SUPERVISOR,
    PMMU030_FAULT_WRITE_PROTECT,
    PMMU030_FAULT_TABLE_BUS,
    PMMU030_FAULT_ATC_BUS
} pmmu030_fault;

/*
 * Descriptor traffic is physical and must not be passed back through the
 * PMMU.  Returning false reports a physical bus error.  begin/end_walk are
 * optional hooks for a host that models the 68030's indivisible table search.
 */
typedef struct pmmu030_bus {
    void *context;
    bool (*read32)(void *context, uint32_t physical_address, uint32_t *value);
    bool (*write32)(void *context, uint32_t physical_address, uint32_t value);
    void (*begin_walk)(void *context);
    void (*end_walk)(void *context);
} pmmu030_bus;

typedef struct pmmu030_atc_entry {
    uint32_t logical_page;
    uint32_t physical_page;
    uint8_t function_code;
    bool valid;
    bool bus_error;
    bool cache_inhibit;
    bool write_protect;
    bool modified;
} pmmu030_atc_entry;

typedef struct pmmu030_state {
    uint32_t tc;
    uint32_t tt0;
    uint32_t tt1;
    uint32_t crp[2];
    uint32_t srp[2];
    uint16_t mmusr;
    uint8_t replacement_cursor;
    pmmu030_atc_entry atc[PMMU030_ATC_ENTRIES];
} pmmu030_state;

typedef struct pmmu030_result {
    uint32_t logical_address;
    uint32_t physical_address;
    uint32_t last_descriptor_address;
    uint16_t mmusr;
    uint8_t function_code;
    uint8_t levels;
    pmmu030_fault fault;
    bool translated;
    bool transparent;
    bool atc_hit;
    bool cache_inhibit;
    bool write_protect;
    bool modified;
} pmmu030_result;

void pmmu030_init(pmmu030_state *state);

/* RESET clears TC.E and both TT enables, but deliberately preserves the ATC. */
void pmmu030_reset(pmmu030_state *state);

bool pmmu030_translation_active(const pmmu030_state *state);
unsigned pmmu030_page_shift(const pmmu030_state *state);
uint32_t pmmu030_page_mask(const pmmu030_state *state);

/* false means the value was loaded but caused an MMU configuration error. */
bool pmmu030_write_tc(pmmu030_state *state, uint32_t value, bool flush_disabled);
bool pmmu030_write_crp(pmmu030_state *state, uint32_t high, uint32_t low,
                       bool flush_disabled);
bool pmmu030_write_srp(pmmu030_state *state, uint32_t high, uint32_t low,
                       bool flush_disabled);
void pmmu030_write_tt0(pmmu030_state *state, uint32_t value,
                       bool flush_disabled);
void pmmu030_write_tt1(pmmu030_state *state, uint32_t value,
                       bool flush_disabled);
void pmmu030_write_mmusr(pmmu030_state *state, uint16_t value);

void pmmu030_flush_all(pmmu030_state *state);
void pmmu030_flush(pmmu030_state *state, uint8_t function_code,
                   uint8_t function_code_mask, const uint32_t *logical_address);

pmmu030_result pmmu030_translate(pmmu030_state *state, const pmmu030_bus *bus,
                                 uint32_t logical_address, uint8_t function_code,
                                 pmmu030_access access);

/* PLOAD always performs a table search, even when TC.E is clear. */
pmmu030_result pmmu030_pload(pmmu030_state *state, const pmmu030_bus *bus,
                             uint32_t logical_address, uint8_t function_code,
                             pmmu030_access access);

/* level 0 tests transparent translation and the ATC; levels 1-7 walk tables. */
pmmu030_result pmmu030_ptest(pmmu030_state *state, const pmmu030_bus *bus,
                             uint32_t logical_address, uint8_t function_code,
                             pmmu030_access access, unsigned level);

#ifdef __cplusplus
}
#endif

#endif
