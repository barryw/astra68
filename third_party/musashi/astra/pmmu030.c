/*
 * Astra68 MC68030 PMMU model.
 *
 * Primary specifications:
 *   Motorola MC68030 User's Manual, section 9
 *   Motorola M68000 Family Programmer's Reference Manual, PMMU instructions
 *
 * The implementation is intentionally independent of upstream Musashi's PMMU
 * source.  See ../ASTRA_VENDOR.md for provenance and review boundaries.
 */

#include "pmmu030.h"

#include <string.h>

enum walk_kind {
    WALK_TRANSLATE,
    WALK_LOAD,
    WALK_TEST
};

enum walk_end {
    WALK_END_PAGE,
    WALK_END_LEVEL,
    WALK_END_INVALID,
    WALK_END_LIMIT,
    WALK_END_SUPERVISOR,
    WALK_END_BUS
};

typedef struct walk_index {
    uint32_t value;
    uint32_t logical_mask;
    unsigned width;
    bool function_code;
} walk_index;

typedef struct descriptor {
    uint32_t word0;
    uint32_t word1;
    uint32_t address;
    unsigned size;
    unsigned type;
    bool is_long;
} descriptor;

typedef struct walk_result {
    enum walk_end end;
    uint32_t physical_address;
    uint32_t last_descriptor_address;
    uint32_t used_logical_mask;
    uint8_t levels;
    bool bus_error;
    bool limit;
    bool supervisor;
    bool write_protect;
    bool cache_inhibit;
    bool modified;
} walk_result;

static uint32_t low_mask(unsigned bits)
{
    if (bits == 0u) {
        return UINT32_C(0);
    }
    if (bits >= 32u) {
        return UINT32_MAX;
    }
    return (UINT32_C(1) << bits) - UINT32_C(1);
}

static bool is_write(pmmu030_access access)
{
    return access != PMMU030_ACCESS_READ;
}

static void result_init(pmmu030_result *result, uint32_t logical_address,
                        uint8_t function_code)
{
    memset(result, 0, sizeof(*result));
    result->logical_address = logical_address;
    result->physical_address = logical_address;
    result->function_code = function_code & UINT8_C(7);
    result->fault = PMMU030_FAULT_NONE;
}

static bool limit_violation(uint32_t descriptor_word, uint32_t index)
{
    uint32_t limit = (descriptor_word >> 16) & UINT32_C(0x7fff);
    bool lower = (descriptor_word & UINT32_C(0x80000000)) != 0u;

    return lower ? index < limit : index > limit;
}

static unsigned make_indexes(const pmmu030_state *state, uint32_t logical,
                             uint8_t function_code, walk_index indexes[5])
{
    unsigned count = 0u;
    unsigned consumed = (state->tc >> 16) & 15u;
    unsigned table;

    if ((state->tc & PMMU030_TC_FCL) != 0u) {
        indexes[count].value = function_code & UINT8_C(7);
        indexes[count].logical_mask = UINT32_C(0);
        indexes[count].width = 3u;
        indexes[count].function_code = true;
        ++count;
    }

    for (table = 0u; table < 4u; ++table) {
        unsigned shift = 12u - table * 4u;
        unsigned width = (state->tc >> shift) & 15u;
        unsigned right;

        if (width == 0u) {
            break;
        }
        if (consumed + width > 32u) {
            break;
        }
        right = 32u - consumed - width;
        indexes[count].value = (logical >> right) & low_mask(width);
        indexes[count].logical_mask = low_mask(width) << right;
        indexes[count].width = width;
        indexes[count].function_code = false;
        ++count;
        consumed += width;
    }
    return count;
}

static bool read_descriptor(const pmmu030_bus *bus, uint32_t address,
                            unsigned size, descriptor *out)
{
    uint32_t word0;
    uint32_t word1 = UINT32_C(0);

    if (bus == NULL || bus->read32 == NULL ||
        !bus->read32(bus->context, address, &word0)) {
        return false;
    }
    if (size == 8u && !bus->read32(bus->context, address + 4u, &word1)) {
        return false;
    }
    out->word0 = word0;
    out->word1 = word1;
    out->address = address;
    out->size = size;
    out->type = word0 & 3u;
    out->is_long = size == 8u;
    return true;
}

static bool update_descriptor(const pmmu030_bus *bus, const descriptor *desc,
                              uint32_t set_bits)
{
    if ((desc->word0 & set_bits) == set_bits) {
        return true;
    }
    return bus != NULL && bus->write32 != NULL &&
           bus->write32(bus->context, desc->address, desc->word0 | set_bits);
}

static pmmu030_fault walk_fault(const walk_result *walk)
{
    switch (walk->end) {
    case WALK_END_INVALID:
        return PMMU030_FAULT_INVALID;
    case WALK_END_LIMIT:
        return PMMU030_FAULT_LIMIT;
    case WALK_END_SUPERVISOR:
        return PMMU030_FAULT_SUPERVISOR;
    case WALK_END_BUS:
        return PMMU030_FAULT_TABLE_BUS;
    default:
        return PMMU030_FAULT_NONE;
    }
}

static void walk_mark_bus(walk_result *walk)
{
    walk->end = WALK_END_BUS;
    walk->bus_error = true;
}

static void walk_descriptor_status(const descriptor *desc, walk_result *walk)
{
    walk->write_protect = walk->write_protect ||
                          (desc->word0 & UINT32_C(0x00000004)) != 0u;
}

static bool descriptor_supervisor_violation(const descriptor *desc,
                                            uint8_t function_code)
{
    return desc->is_long && (desc->word0 & UINT32_C(0x00000100)) != 0u &&
           (function_code & UINT8_C(4)) == 0u;
}

static bool finish_page(const descriptor *desc, uint32_t logical_address,
                        uint32_t used_logical_mask, bool early,
                        const walk_index *next_index, uint8_t function_code,
                        pmmu030_access access, enum walk_kind kind,
                        const pmmu030_bus *bus, walk_result *walk)
{
    uint32_t page_base;
    uint32_t history = UINT32_C(0);

    if (descriptor_supervisor_violation(desc, function_code)) {
        walk->supervisor = true;
        walk->end = WALK_END_SUPERVISOR;
        return false;
    }

    walk_descriptor_status(desc, walk);
    if (desc->is_long && early && next_index != NULL &&
        limit_violation(desc->word0, next_index->value)) {
        if (kind != WALK_TEST && !update_descriptor(bus, desc, UINT32_C(8))) {
            walk_mark_bus(walk);
            return false;
        }
        walk->limit = true;
        walk->end = WALK_END_LIMIT;
        return false;
    }

    if (kind != WALK_TEST) {
        history |= UINT32_C(8);
        if (is_write(access) && !walk->write_protect) {
            history |= UINT32_C(0x10);
        }
        if (!update_descriptor(bus, desc, history)) {
            walk_mark_bus(walk);
            return false;
        }
    }

    walk->cache_inhibit = (desc->word0 & UINT32_C(0x40)) != 0u;
    walk->modified = (desc->word0 & UINT32_C(0x10)) != 0u ||
                     (history & UINT32_C(0x10)) != 0u;
    page_base = desc->is_long ? desc->word1 : desc->word0;
    page_base &= UINT32_C(0xffffff00);
    walk->physical_address = page_base +
                             (logical_address & ~used_logical_mask);
    walk->end = WALK_END_PAGE;
    return true;
}

static walk_result table_walk(pmmu030_state *state, const pmmu030_bus *bus,
                              uint32_t logical_address, uint8_t function_code,
                              pmmu030_access access, enum walk_kind kind,
                              unsigned maximum_level)
{
    walk_result walk;
    walk_index indexes[5];
    const uint32_t *root;
    uint32_t root_word;
    uint32_t table_base;
    uint32_t used_mask = UINT32_C(0);
    unsigned index_count;
    unsigned index_pos = 0u;
    unsigned descriptor_size;
    bool parent_has_limit = true;

    memset(&walk, 0, sizeof(walk));
    walk.end = WALK_END_INVALID;
    function_code &= UINT8_C(7);
    root = ((state->tc & PMMU030_TC_SRE) != 0u &&
            (function_code & UINT8_C(4)) != 0u) ? state->srp : state->crp;
    root_word = root[0];
    table_base = root[1] & UINT32_C(0xfffffff0);
    index_count = make_indexes(state, logical_address, function_code, indexes);

    if (bus != NULL && bus->begin_walk != NULL) {
        bus->begin_walk(bus->context);
    }

    if ((root_word & 3u) == 0u) {
        walk.end = WALK_END_INVALID;
        goto done;
    }

    if ((root_word & 3u) == 1u) {
        if ((state->tc & PMMU030_TC_FCL) == 0u && index_count != 0u &&
            limit_violation(root_word, indexes[0].value)) {
            walk.limit = true;
            walk.end = WALK_END_LIMIT;
            goto done;
        }
        walk.physical_address = table_base + logical_address;
        walk.end = WALK_END_PAGE;
        goto done;
    }

    descriptor_size = (root_word & 3u) == 2u ? 4u : 8u;
    if (index_count == 0u) {
        walk.end = WALK_END_INVALID;
        goto done;
    }

    while (index_pos < index_count) {
        descriptor desc;
        uint32_t descriptor_address;
        bool more_indexes;

        if (parent_has_limit &&
            !(indexes[index_pos].function_code && index_pos == 0u) &&
            limit_violation(root_word, indexes[index_pos].value)) {
            walk.limit = true;
            walk.end = WALK_END_LIMIT;
            goto done;
        }

        descriptor_address = table_base +
                             indexes[index_pos].value * descriptor_size;
        if (!read_descriptor(bus, descriptor_address, descriptor_size, &desc)) {
            walk_mark_bus(&walk);
            goto done;
        }
        walk.last_descriptor_address = descriptor_address;
        ++walk.levels;
        used_mask |= indexes[index_pos].logical_mask;
        ++index_pos;
        more_indexes = index_pos < index_count;

        if (desc.type == 0u) {
            walk.end = WALK_END_INVALID;
            goto done;
        }
        if (desc.type == 1u) {
            (void)finish_page(&desc, logical_address, used_mask, more_indexes,
                              more_indexes ? &indexes[index_pos] : NULL,
                              function_code, access, kind, bus, &walk);
            goto done;
        }

        if (maximum_level != 0u && walk.levels >= maximum_level) {
            if (descriptor_supervisor_violation(&desc, function_code)) {
                walk.supervisor = true;
                walk.end = WALK_END_SUPERVISOR;
                goto done;
            }
            walk_descriptor_status(&desc, &walk);
            walk.end = WALK_END_LEVEL;
            goto done;
        }

        if (more_indexes) {
            if (descriptor_supervisor_violation(&desc, function_code)) {
                walk.supervisor = true;
                walk.end = WALK_END_SUPERVISOR;
                goto done;
            }
            walk_descriptor_status(&desc, &walk);
            if (kind != WALK_TEST &&
                !update_descriptor(bus, &desc, UINT32_C(8))) {
                walk_mark_bus(&walk);
                goto done;
            }
            root_word = desc.word0;
            table_base = (desc.is_long ? desc.word1 : desc.word0) &
                         UINT32_C(0xfffffff0);
            descriptor_size = desc.type == 2u ? 4u : 8u;
            parent_has_limit = desc.is_long;
            continue;
        }

        /* At the bottom level, types 2 and 3 are indirect descriptors. */
        {
            descriptor target;
            uint32_t target_address = (desc.is_long ? desc.word1 : desc.word0) &
                                      UINT32_C(0xfffffffc);
            unsigned target_size = desc.type == 2u ? 4u : 8u;

            if (!read_descriptor(bus, target_address, target_size, &target)) {
                walk_mark_bus(&walk);
                goto done;
            }
            walk.last_descriptor_address = target_address;
            ++walk.levels;
            if (target.type == 0u) {
                walk.end = WALK_END_INVALID;
                goto done;
            }
            if (target.type != 1u) {
                walk.end = WALK_END_INVALID;
                goto done;
            }
            (void)finish_page(&target, logical_address, used_mask, false, NULL,
                              function_code, access, kind, bus, &walk);
            goto done;
        }
    }

done:
    walk.used_logical_mask = used_mask;
    if (bus != NULL && bus->end_walk != NULL) {
        bus->end_walk(bus->context);
    }
    return walk;
}

static bool transparent_match(uint32_t tt, uint32_t logical_address,
                              uint8_t function_code, pmmu030_access access)
{
    uint32_t address_base;
    uint32_t address_mask;
    uint32_t fc_base;
    uint32_t fc_mask;
    bool read_cycle = access == PMMU030_ACCESS_READ;

    if ((tt & PMMU030_TT_ENABLE) == 0u) {
        return false;
    }
    address_base = tt >> 24;
    address_mask = (tt >> 16) & UINT32_C(0xff);
    if ((((logical_address >> 24) ^ address_base) & ~address_mask &
         UINT32_C(0xff)) != 0u) {
        return false;
    }
    fc_base = (tt >> 4) & 7u;
    fc_mask = tt & 7u;
    if ((((function_code & 7u) ^ fc_base) & ~fc_mask & 7u) != 0u) {
        return false;
    }
    if (access == PMMU030_ACCESS_RMW) {
        return (tt & UINT32_C(0x100)) != 0u;
    }
    if ((tt & UINT32_C(0x100)) != 0u) {
        return true;
    }
    return read_cycle == ((tt & UINT32_C(0x200)) != 0u);
}

static bool transparent_lookup(const pmmu030_state *state,
                               uint32_t logical_address, uint8_t function_code,
                               pmmu030_access access, bool *cache_inhibit)
{
    bool match0 = transparent_match(state->tt0, logical_address,
                                    function_code, access);
    bool match1 = transparent_match(state->tt1, logical_address,
                                    function_code, access);

    *cache_inhibit = (match0 && (state->tt0 & UINT32_C(0x400)) != 0u) ||
                     (match1 && (state->tt1 & UINT32_C(0x400)) != 0u);
    return match0 || match1;
}

static pmmu030_atc_entry *atc_lookup(pmmu030_state *state,
                                     uint32_t logical_address,
                                     uint8_t function_code)
{
    uint32_t page_mask = pmmu030_page_mask(state);
    uint32_t logical_page = logical_address & ~page_mask;
    unsigned i;

    for (i = 0u; i < PMMU030_ATC_ENTRIES; ++i) {
        pmmu030_atc_entry *entry = &state->atc[i];
        if (entry->valid && entry->function_code == (function_code & 7u) &&
            entry->logical_page == logical_page) {
            return entry;
        }
    }
    return NULL;
}

static pmmu030_atc_entry *atc_allocate(pmmu030_state *state,
                                       uint32_t logical_address,
                                       uint8_t function_code)
{
    pmmu030_atc_entry *entry = atc_lookup(state, logical_address, function_code);
    unsigned i;

    if (entry != NULL) {
        memset(entry, 0, sizeof(*entry));
    } else {
        for (i = 0u; i < PMMU030_ATC_ENTRIES; ++i) {
            if (!state->atc[i].valid) {
                entry = &state->atc[i];
                break;
            }
        }
    }
    if (entry == NULL) {
        entry = &state->atc[state->replacement_cursor];
        state->replacement_cursor =
            (uint8_t)((state->replacement_cursor + 1u) % PMMU030_ATC_ENTRIES);
    }
    memset(entry, 0, sizeof(*entry));
    entry->valid = true;
    entry->function_code = function_code & UINT8_C(7);
    entry->logical_page = logical_address & ~pmmu030_page_mask(state);
    return entry;
}

static void atc_fill(pmmu030_state *state, uint32_t logical_address,
                     uint8_t function_code, const walk_result *walk)
{
    pmmu030_atc_entry *entry = atc_allocate(state, logical_address,
                                            function_code);
    uint32_t page_mask = pmmu030_page_mask(state);

    entry->bus_error = walk->end == WALK_END_INVALID ||
                       walk->end == WALK_END_LIMIT ||
                       walk->end == WALK_END_SUPERVISOR ||
                       walk->end == WALK_END_BUS;
    entry->physical_page = walk->physical_address & ~page_mask;
    entry->cache_inhibit = walk->cache_inhibit;
    entry->write_protect = walk->write_protect;
    entry->modified = walk->modified;
}

static void result_from_entry(pmmu030_result *result,
                              const pmmu030_state *state,
                              const pmmu030_atc_entry *entry)
{
    uint32_t page_mask = pmmu030_page_mask(state);

    result->atc_hit = true;
    result->translated = true;
    result->physical_address = entry->physical_page |
                               (result->logical_address & page_mask);
    result->cache_inhibit = entry->cache_inhibit;
    result->write_protect = entry->write_protect;
    result->modified = entry->modified;
}

static uint16_t mmusr_from_walk(const walk_result *walk)
{
    uint16_t status = walk->levels & PMMU030_MMUSR_N;

    if (walk->bus_error) {
        status |= PMMU030_MMUSR_B;
    }
    if (walk->limit) {
        status |= PMMU030_MMUSR_L;
    }
    if (walk->supervisor) {
        status |= PMMU030_MMUSR_S;
    }
    if (walk->write_protect) {
        status |= PMMU030_MMUSR_W;
    }
    if (walk->end == WALK_END_INVALID || walk->end == WALK_END_LIMIT ||
        walk->end == WALK_END_SUPERVISOR || walk->end == WALK_END_BUS) {
        status |= PMMU030_MMUSR_I;
    }
    if (walk->modified) {
        status |= PMMU030_MMUSR_M;
    }
    return status & PMMU030_MMUSR_MASK;
}

void pmmu030_init(pmmu030_state *state)
{
    memset(state, 0, sizeof(*state));
}

void pmmu030_reset(pmmu030_state *state)
{
    state->tc &= ~PMMU030_TC_ENABLE;
    state->tt0 &= ~PMMU030_TT_ENABLE;
    state->tt1 &= ~PMMU030_TT_ENABLE;
}

bool pmmu030_translation_active(const pmmu030_state *state)
{
    return (state->tc & PMMU030_TC_ENABLE) != 0u ||
           (state->tt0 & PMMU030_TT_ENABLE) != 0u ||
           (state->tt1 & PMMU030_TT_ENABLE) != 0u;
}

unsigned pmmu030_page_shift(const pmmu030_state *state)
{
    unsigned shift = (state->tc >> 20) & 15u;
    return shift >= 8u ? shift : 8u;
}

uint32_t pmmu030_page_mask(const pmmu030_state *state)
{
    return low_mask(pmmu030_page_shift(state));
}

void pmmu030_flush_all(pmmu030_state *state)
{
    unsigned i;
    for (i = 0u; i < PMMU030_ATC_ENTRIES; ++i) {
        state->atc[i].valid = false;
    }
    state->replacement_cursor = 0u;
}

bool pmmu030_write_tc(pmmu030_state *state, uint32_t value,
                      bool flush_disabled)
{
    unsigned ps;
    unsigned sum;
    unsigned table;
    bool valid = true;

    state->tc = value & PMMU030_TC_MASK;
    if (!flush_disabled) {
        pmmu030_flush_all(state);
    }
    if ((state->tc & PMMU030_TC_ENABLE) == 0u) {
        return true;
    }

    ps = (state->tc >> 20) & 15u;
    sum = ps + ((state->tc >> 16) & 15u);
    if (ps < 8u) {
        valid = false;
    }
    for (table = 0u; table < 4u; ++table) {
        unsigned width = (state->tc >> (12u - table * 4u)) & 15u;
        if (width == 0u) {
            break;
        }
        sum += width;
    }
    if (sum != 32u) {
        valid = false;
    }
    if (!valid) {
        state->tc &= ~PMMU030_TC_ENABLE;
    }
    return valid;
}

static bool write_root(pmmu030_state *state, uint32_t root[2], uint32_t high,
                       uint32_t low, bool flush_disabled)
{
    root[0] = high & PMMU030_RP_MASK_HI;
    root[1] = low & PMMU030_RP_MASK_LO;
    if (!flush_disabled) {
        pmmu030_flush_all(state);
    }
    return (root[0] & 3u) != 0u;
}

bool pmmu030_write_crp(pmmu030_state *state, uint32_t high, uint32_t low,
                       bool flush_disabled)
{
    return write_root(state, state->crp, high, low, flush_disabled);
}

bool pmmu030_write_srp(pmmu030_state *state, uint32_t high, uint32_t low,
                       bool flush_disabled)
{
    return write_root(state, state->srp, high, low, flush_disabled);
}

void pmmu030_write_tt0(pmmu030_state *state, uint32_t value,
                       bool flush_disabled)
{
    state->tt0 = value & PMMU030_TT_MASK;
    if (!flush_disabled) {
        pmmu030_flush_all(state);
    }
}

void pmmu030_write_tt1(pmmu030_state *state, uint32_t value,
                       bool flush_disabled)
{
    state->tt1 = value & PMMU030_TT_MASK;
    if (!flush_disabled) {
        pmmu030_flush_all(state);
    }
}

void pmmu030_write_mmusr(pmmu030_state *state, uint16_t value)
{
    state->mmusr = value & PMMU030_MMUSR_MASK;
}

void pmmu030_flush(pmmu030_state *state, uint8_t function_code,
                   uint8_t function_code_mask, const uint32_t *logical_address)
{
    uint32_t page_mask = pmmu030_page_mask(state);
    unsigned i;

    function_code &= UINT8_C(7);
    function_code_mask &= UINT8_C(7);
    for (i = 0u; i < PMMU030_ATC_ENTRIES; ++i) {
        pmmu030_atc_entry *entry = &state->atc[i];
        if (!entry->valid) {
            continue;
        }
        if ((entry->function_code & function_code_mask) !=
            (function_code & function_code_mask)) {
            continue;
        }
        if (logical_address != NULL && entry->logical_page !=
            (*logical_address & ~page_mask)) {
            continue;
        }
        entry->valid = false;
    }
}

pmmu030_result pmmu030_translate(pmmu030_state *state, const pmmu030_bus *bus,
                                 uint32_t logical_address, uint8_t function_code,
                                 pmmu030_access access)
{
    pmmu030_result result;
    pmmu030_atc_entry *entry;
    walk_result walk;
    bool cache_inhibit = false;

    function_code &= UINT8_C(7);
    result_init(&result, logical_address, function_code);
    if (function_code == UINT8_C(7)) {
        return result;
    }
    if (transparent_lookup(state, logical_address, function_code, access,
                           &cache_inhibit)) {
        result.transparent = true;
        result.cache_inhibit = cache_inhibit;
        return result;
    }
    if ((state->tc & PMMU030_TC_ENABLE) == 0u) {
        return result;
    }

    entry = atc_lookup(state, logical_address, function_code);
    if (entry != NULL && !(is_write(access) && !entry->modified &&
                           !entry->write_protect && !entry->bus_error)) {
        result_from_entry(&result, state, entry);
        if (entry->bus_error) {
            result.fault = PMMU030_FAULT_ATC_BUS;
        } else if (is_write(access) && entry->write_protect) {
            result.fault = PMMU030_FAULT_WRITE_PROTECT;
        }
        return result;
    }

    walk = table_walk(state, bus, logical_address, function_code, access,
                      WALK_TRANSLATE, 0u);
    atc_fill(state, logical_address, function_code, &walk);
    entry = atc_lookup(state, logical_address, function_code);
    result.levels = walk.levels;
    result.last_descriptor_address = walk.last_descriptor_address;
    result.fault = walk_fault(&walk);
    if (entry != NULL) {
        result_from_entry(&result, state, entry);
    }
    if (result.fault == PMMU030_FAULT_NONE && is_write(access) &&
        result.write_protect) {
        result.fault = PMMU030_FAULT_WRITE_PROTECT;
    }
    return result;
}

pmmu030_result pmmu030_pload(pmmu030_state *state, const pmmu030_bus *bus,
                             uint32_t logical_address, uint8_t function_code,
                             pmmu030_access access)
{
    pmmu030_result result;
    pmmu030_atc_entry *entry;
    walk_result walk;

    function_code &= UINT8_C(7);
    result_init(&result, logical_address, function_code);
    pmmu030_flush(state, function_code, UINT8_C(7), &logical_address);
    walk = table_walk(state, bus, logical_address, function_code, access,
                      WALK_LOAD, 0u);
    atc_fill(state, logical_address, function_code, &walk);
    entry = atc_lookup(state, logical_address, function_code);
    result.levels = walk.levels;
    result.last_descriptor_address = walk.last_descriptor_address;
    result.fault = walk_fault(&walk);
    if (entry != NULL) {
        result_from_entry(&result, state, entry);
    }
    return result;
}

pmmu030_result pmmu030_ptest(pmmu030_state *state, const pmmu030_bus *bus,
                             uint32_t logical_address, uint8_t function_code,
                             pmmu030_access access, unsigned level)
{
    pmmu030_result result;
    pmmu030_atc_entry *entry;
    walk_result walk;
    bool cache_inhibit = false;

    function_code &= UINT8_C(7);
    result_init(&result, logical_address, function_code);
    if (level == 0u) {
        if (transparent_lookup(state, logical_address, function_code, access,
                               &cache_inhibit)) {
            result.transparent = true;
            result.cache_inhibit = cache_inhibit;
            result.mmusr = PMMU030_MMUSR_T;
        } else {
            entry = atc_lookup(state, logical_address, function_code);
            if (entry == NULL) {
                result.mmusr = PMMU030_MMUSR_I;
            } else {
                result_from_entry(&result, state, entry);
                if (entry->bus_error) {
                    result.mmusr = PMMU030_MMUSR_B | PMMU030_MMUSR_I;
                } else {
                    result.mmusr = (entry->write_protect ? PMMU030_MMUSR_W : 0u) |
                                   (entry->modified ? PMMU030_MMUSR_M : 0u);
                }
            }
        }
        state->mmusr = result.mmusr & PMMU030_MMUSR_MASK;
        result.mmusr = state->mmusr;
        return result;
    }

    if (level > 7u) {
        level = 7u;
    }
    walk = table_walk(state, bus, logical_address, function_code, access,
                      WALK_TEST, level);
    result.levels = walk.levels;
    result.last_descriptor_address = walk.last_descriptor_address;
    result.physical_address = walk.physical_address;
    result.translated = walk.end == WALK_END_PAGE;
    result.cache_inhibit = walk.cache_inhibit;
    result.write_protect = walk.write_protect;
    result.modified = walk.modified;
    result.fault = walk_fault(&walk);
    state->mmusr = mmusr_from_walk(&walk);
    result.mmusr = state->mmusr;
    return result;
}
