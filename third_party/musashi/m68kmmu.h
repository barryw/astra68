/*
 * Musashi adapter for Astra68's independent MC68030 PMMU model.
 *
 * This file intentionally implements only the four instructions present on
 * the MC68030: PMOVE, PFLUSH, PLOAD, and PTEST.  MC68851-only command words
 * take the F-line unimplemented exception in supervisor mode, as real 68030
 * hardware does.
 */

#ifndef ASTRA_MUSASHI_M68KMMU_H
#define ASTRA_MUSASHI_M68KMMU_H

static bool astra_pmmu_bus_read32(void *context, uint32_t address,
                                 uint32_t *value)
{
    (void)context;
    return CALLBACK_PMMU_READ32(ADDRESS_68K(address), value) != 0;
}

static bool astra_pmmu_bus_write32(void *context, uint32_t address,
                                  uint32_t value)
{
    (void)context;
    return CALLBACK_PMMU_WRITE32(ADDRESS_68K(address), value) != 0;
}

static const pmmu030_bus astra_pmmu_bus = {
    NULL,
    astra_pmmu_bus_read32,
    astra_pmmu_bus_write32,
    NULL,
    NULL
};

/* A real MC68030 uses format A only when the faulted write is the final
 * pending bus cycle.  Start with the complete MOVE-to-memory family, whose
 * destination write is unambiguously last; unclassified and multi-cycle
 * stores conservatively use restartable format B. */
static bool astra_pmmu_lastwrite_eligible(uint opcode)
{
    uint operation = opcode >> 12;
    uint destination_mode;
    uint destination_register;

    if (operation < 1u || operation > 3u)
        return false;
    destination_mode = (opcode >> 6) & 7u;
    destination_register = (opcode >> 9) & 7u;
    if (destination_mode < 2u)
        return false;
    if (destination_mode == 7u && destination_register >= 2u)
        return false;
    return true;
}

static inline bool astra_pmmu_fast_translate(uint32_t logical_address,
                                             uint8_t function_code,
                                             pmmu030_access access,
                                             uint32_t *physical_address)
{
    return pmmu030_fast_translate_inline(&PMMU_STATE, logical_address,
                                         function_code, access,
                                         physical_address);
}

uint m68ki_pmmu_translate_addr(uint address, uint function_code,
                              pmmu030_access access, uint transfer_size,
                              uint data_output, uint instruction,
                              uint fault_address, uint cycle_index)
{
    uint32_t physical_address;
    pmmu030_result result;

    if (astra_pmmu_fast_translate(address, (uint8_t)function_code, access,
                                  &physical_address))
        return physical_address;
    result = pmmu030_translate(&PMMU_STATE, &astra_pmmu_bus, address,
                               (uint8_t)function_code, access);

    PMMU_ENABLED = (PMMU_STATE.tc & PMMU030_TC_ENABLE) != 0u;
    if (result.fault != PMMU030_FAULT_NONE) {
#ifdef ASTRA_PMMU_TRACE
        fprintf(stderr, "PMMU fault=%u la=%08x fc=%u access=%u size=%u "
                "pc=%08x tc=%08x\n", (unsigned)result.fault, address,
                function_code & 7u, (unsigned)access, transfer_size,
                REG_PC, PMMU_STATE.tc);
#endif
        uint ssw = 0u;
        uint size_code;

        switch (transfer_size) {
        case 1u:
            size_code = 1u;
            data_output &= 0xffu;
            break;
        case 2u:
            size_code = 2u;
            data_output &= 0xffffu;
            break;
        case 3u:
            size_code = 3u;
            data_output &= 0x00ffffffu;
            break;
        default:
            size_code = 0u;
            break;
        }

        CPU_BUS_FAULT_DEFERRED = 0u;
        CPU_BUS_FAULT_STATE1 = 0u;
        if (instruction) {
            CPU_BUS_FAULT_FORMAT = M68K030_BUS_FRAME_B;
            ssw = M68K030_SSW_FB | M68K030_SSW_RB;
        } else {
            bool lastwrite = access == PMMU030_ACCESS_WRITE &&
                             CPU_RUN_MODE != RUN_MODE_BERR_AERR_RESET_WSF &&
                             astra_pmmu_lastwrite_eligible(REG_IR);

            CPU_BUS_FAULT_FORMAT = lastwrite ? M68K030_BUS_FRAME_A :
                                               M68K030_BUS_FRAME_B;
            ssw = M68K030_SSW_DF | (size_code << 4) |
                  (function_code & M68K030_SSW_FC_MASK);
            if (access == PMMU030_ACCESS_READ)
                ssw |= M68K030_SSW_RW;
            if (access == PMMU030_ACCESS_RMW)
                ssw |= M68K030_SSW_RM;
            if (lastwrite) {
                CPU_BUS_FAULT_STATE1 = 0x0100u;
                CPU_BUS_FAULT_DEFERRED = 1u;
            }
        }

        CPU_BUS_FAULT_VALID = 1u;
        CPU_BUS_FAULT_SSW = ssw;
        CPU_BUS_FAULT_ADDRESS = fault_address;
        CPU_BUS_FAULT_DOB = data_output;
        CPU_BUS_FAULT_DIB = 0u;
        CPU_BUS_FAULT_STAGE_B = instruction ? address : REG_PC;
        CPU_BUS_FAULT_CYCLE = cycle_index;
        CPU_BUS_FAULT_PC = CPU_BUS_FAULT_FORMAT == M68K030_BUS_FRAME_A ?
                           REG_PC : REG_PPC;

        m68ki_aerr_address = fault_address;
        m68ki_aerr_write_mode = access == PMMU030_ACCESS_READ ?
                                MODE_READ : MODE_WRITE;
        m68ki_aerr_fc = function_code & 7u;
        if (CPU_BUS_FAULT_DEFERRED)
            return address;
        m68ki_exception_bus_error();
        return address;
    }
    return result.physical_address;
}

static bool astra_pmmu_decode_fc(uint extension, uint *function_code)
{
    uint field = extension & 31u;

    if ((field & 0x18u) == 0x10u) {
        *function_code = field & 7u;
        return true;
    }
    if ((field & 0x18u) == 0x08u) {
        *function_code = REG_D[field & 7u] & 7u;
        return true;
    }
    if (field == 0u) {
        *function_code = REG_SFC & 7u;
        return true;
    }
    if (field == 1u) {
        *function_code = REG_DFC & 7u;
        return true;
    }
    return false;
}

/* Decode the control-alterable modes accepted by the MC68030 PMMU. */
static bool astra_pmmu_decode_ea(uint *effective_address)
{
    uint mode = (REG_IR >> 3) & 7u;
    uint reg = REG_IR & 7u;

    switch (mode) {
    case 2u:                         /* (An) */
        *effective_address = REG_A[reg];
        return true;
    case 5u:                         /* (d16,An) */
        *effective_address = REG_A[reg] + MAKE_INT_16(m68ki_read_imm_16());
        return true;
    case 6u:                         /* indexed, including full extension */
        *effective_address = m68ki_get_ea_ix(REG_A[reg]);
        return true;
    case 7u:
        if (reg == 0u) {             /* (xxx).W */
            *effective_address = (uint32)MAKE_INT_16(m68ki_read_imm_16());
            return true;
        }
        if (reg == 1u) {             /* (xxx).L */
            *effective_address = m68ki_read_imm_32();
            return true;
        }
        return false;
    default:
        return false;
    }
}

static void astra_pmmu_unimplemented(void)
{
    m68ki_exception_1111();
}

static void astra_pmmu_pmove_tt(uint extension)
{
    uint preg = (extension >> 10) & 7u;
    bool from_mmu = (extension & 0x0200u) != 0u;
    bool flush_disabled = (extension & 0x0100u) != 0u;
    uint ea;

    if ((extension & 0x00ffu) != 0u || (preg != 2u && preg != 3u) ||
        !astra_pmmu_decode_ea(&ea)) {
        astra_pmmu_unimplemented();
        return;
    }
    if (from_mmu) {
        m68ki_write_32(ea, preg == 2u ? PMMU_STATE.tt0 : PMMU_STATE.tt1);
    } else if (preg == 2u) {
        pmmu030_write_tt0(&PMMU_STATE, m68ki_read_32(ea), flush_disabled);
    } else {
        pmmu030_write_tt1(&PMMU_STATE, m68ki_read_32(ea), flush_disabled);
    }
}

static void astra_pmmu_pmove_root_or_tc(uint extension)
{
    uint preg = (extension >> 10) & 7u;
    bool from_mmu = (extension & 0x0200u) != 0u;
    bool flush_disabled = (extension & 0x0100u) != 0u;
    uint ea;
    bool valid = true;

    if ((extension & 0x00ffu) != 0u ||
        (preg != 0u && preg != 2u && preg != 3u) ||
        !astra_pmmu_decode_ea(&ea)) {
        astra_pmmu_unimplemented();
        return;
    }

    if (from_mmu) {
        if (preg == 0u) {
            m68ki_write_32(ea, PMMU_STATE.tc);
        } else {
            const uint32_t *root = preg == 2u ? PMMU_STATE.srp : PMMU_STATE.crp;
            m68ki_write_32(ea, root[0]);
            m68ki_write_32(ea + 4u, root[1]);
        }
        return;
    }

    if (preg == 0u) {
        valid = pmmu030_write_tc(&PMMU_STATE, m68ki_read_32(ea),
                                 flush_disabled);
        PMMU_ENABLED = (PMMU_STATE.tc & PMMU030_TC_ENABLE) != 0u;
    } else {
        uint32_t high = m68ki_read_32(ea);
        uint32_t low = m68ki_read_32(ea + 4u);
        valid = preg == 2u ?
                pmmu030_write_srp(&PMMU_STATE, high, low, flush_disabled) :
                pmmu030_write_crp(&PMMU_STATE, high, low, flush_disabled);
    }
    if (!valid) {
        m68ki_exception_mmu_configuration();
    }
}

static void astra_pmmu_pmove_mmusr(uint extension)
{
    bool from_mmu = (extension & 0x0200u) != 0u;
    uint ea;

    if ((extension & 0x1dffu) != 0u || !astra_pmmu_decode_ea(&ea)) {
        astra_pmmu_unimplemented();
        return;
    }
    if (from_mmu) {
        m68ki_write_16(ea, PMMU_STATE.mmusr);
    } else {
        pmmu030_write_mmusr(&PMMU_STATE, (uint16_t)m68ki_read_16(ea));
    }
}

static void astra_pmmu_pload(uint extension)
{
    uint function_code;
    uint ea;
    pmmu030_access access = (extension & 0x0200u) != 0u ?
                            PMMU030_ACCESS_READ : PMMU030_ACCESS_WRITE;

    if ((extension & 0x01e0u) != 0u ||
        !astra_pmmu_decode_fc(extension, &function_code) ||
        !astra_pmmu_decode_ea(&ea)) {
        astra_pmmu_unimplemented();
        return;
    }
    (void)pmmu030_pload(&PMMU_STATE, &astra_pmmu_bus, ea,
                        (uint8_t)function_code, access);
}

static void astra_pmmu_pflush(uint extension)
{
    uint mode = (extension >> 10) & 7u;
    uint mask = (extension >> 5) & 7u;
    uint function_code;
    uint ea;

    if ((extension & 0x0300u) != 0u) {
        astra_pmmu_unimplemented();
        return;
    }
    if (mode == 1u) {
        if ((REG_IR & 0x3fu) != 0u || mask != 0u ||
            (extension & 31u) != 0u) {
            astra_pmmu_unimplemented();
            return;
        }
        pmmu030_flush_all(&PMMU_STATE);
        return;
    }
    if ((mode != 4u && mode != 6u) ||
        !astra_pmmu_decode_fc(extension, &function_code)) {
        astra_pmmu_unimplemented();
        return;
    }
    if (mode == 4u) {
        if ((REG_IR & 0x3fu) != 0u) {
            astra_pmmu_unimplemented();
            return;
        }
        pmmu030_flush(&PMMU_STATE, (uint8_t)function_code, (uint8_t)mask,
                      NULL);
        return;
    }
    if (!astra_pmmu_decode_ea(&ea)) {
        astra_pmmu_unimplemented();
        return;
    }
    pmmu030_flush(&PMMU_STATE, (uint8_t)function_code, (uint8_t)mask, &ea);
}

static void astra_pmmu_ptest(uint extension)
{
    uint level = (extension >> 10) & 7u;
    uint function_code;
    bool return_descriptor = (extension & 0x0100u) != 0u;
    uint address_register = (extension >> 5) & 7u;
    pmmu030_access access = (extension & 0x0200u) != 0u ?
                            PMMU030_ACCESS_READ : PMMU030_ACCESS_WRITE;
    uint ea;
    pmmu030_result result;

    if ((!return_descriptor && address_register != 0u) ||
        (level == 0u && return_descriptor) ||
        !astra_pmmu_decode_fc(extension, &function_code) ||
        !astra_pmmu_decode_ea(&ea)) {
        astra_pmmu_unimplemented();
        return;
    }
    result = pmmu030_ptest(&PMMU_STATE, &astra_pmmu_bus, ea,
                           (uint8_t)function_code, access, level);
    if (return_descriptor) {
        REG_A[address_register] = result.last_descriptor_address;
    }
}

/* Historical Musashi hook name retained so generated opcode tables need not change. */
void m68881_mmu_ops(void)
{
    uint extension;
    uint command_class;

    if (!FLAG_S) {
        m68ki_exception_privilege_violation();
        return;
    }
    if (CPU_TYPE != CPU_TYPE_030) {
        astra_pmmu_unimplemented();
        return;
    }

    extension = m68ki_read_imm_16();
    command_class = extension >> 13;
    switch (command_class) {
    case 0u:
        astra_pmmu_pmove_tt(extension);
        return;
    case 1u:
        if ((extension & 0xfc00u) == 0x2000u) {
            astra_pmmu_pload(extension);
        } else {
            astra_pmmu_pflush(extension);
        }
        return;
    case 2u:
        astra_pmmu_pmove_root_or_tc(extension);
        return;
    case 3u:
        astra_pmmu_pmove_mmusr(extension);
        return;
    case 4u:
        astra_pmmu_ptest(extension);
        return;
    default:
        astra_pmmu_unimplemented();
        return;
    }
}

#endif
