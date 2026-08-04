/*
 * QEMU adapter for Astra68's independent MC68030 PMMU model.
 *
 * The architectural table walker and ATC live in pmmu030.c and are shared
 * with Musashi.  This file only connects that model to QEMU's physical bus,
 * softmmu TLB, and translated PMMU instructions.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/memory.h"

#if !defined(CONFIG_USER_ONLY)

#define M68030_SSW_FB       0x4000u
#define M68030_SSW_RB       0x1000u
#define M68030_SSW_DF       0x0100u
#define M68030_SSW_RM       0x0080u
#define M68030_SSW_RW       0x0040u
#define M68030_SSW_FC_MASK  0x0007u

static bool qemu_pmmu_read32(void *context, uint32_t address, uint32_t *value)
{
    CPUState *cs = context;
    MemTxResult result;

    *value = address_space_ldl_be(cs->as, address, MEMTXATTRS_UNSPECIFIED,
                                  &result);
    return result == MEMTX_OK;
}

static bool qemu_pmmu_write32(void *context, uint32_t address, uint32_t value)
{
    CPUState *cs = context;
    MemTxResult result;

    address_space_stl_be(cs->as, address, value, MEMTXATTRS_UNSPECIFIED,
                         &result);
    return result == MEMTX_OK;
}

static pmmu030_bus qemu_pmmu_bus(CPUM68KState *env)
{
    pmmu030_bus bus = {
        .context = env_cpu(env),
        .read32 = qemu_pmmu_read32,
        .write32 = qemu_pmmu_write32,
    };

    return bus;
}

static uint8_t qemu_pmmu_function_code(MMUAccessType access_type, int mmu_idx)
{
    uint8_t function_code = mmu_idx == MMU_USER_IDX ? 0u : 4u;

    function_code |= access_type == MMU_INST_FETCH ? 2u : 1u;
    return function_code;
}

static pmmu030_access qemu_pmmu_access(MMUAccessType access_type)
{
    return access_type == MMU_DATA_STORE ? PMMU030_ACCESS_WRITE :
                                           PMMU030_ACCESS_READ;
}

static uint16_t qemu_pmmu_fault_ssw(MMUAccessType access_type, int size,
                                    uint8_t function_code)
{
    uint16_t ssw;
    unsigned size_code;

    if (access_type == MMU_INST_FETCH) {
        return M68030_SSW_FB | M68030_SSW_RB;
    }
    switch (size) {
    case 1:
        size_code = 1u;
        break;
    case 2:
        size_code = 2u;
        break;
    default:
        size_code = 0u;
        break;
    }
    ssw = M68030_SSW_DF | (size_code << 4) |
          (function_code & M68030_SSW_FC_MASK);
    if (access_type != MMU_DATA_STORE) {
        ssw |= M68030_SSW_RW;
    }
    return ssw;
}

bool m68k_pmmu030_tlb_fill(CPUState *cs, vaddr address, int size,
                           MMUAccessType access_type, int mmu_idx,
                           bool probe, uintptr_t retaddr)
{
    CPUM68KState *env = cpu_env(cs);
    pmmu030_bus bus = qemu_pmmu_bus(env);
    pmmu030_result result;
    pmmu030_access access;
    target_ulong page_size;
    target_ulong page_mask;
    uint8_t function_code;
    int prot;

    if (!pmmu030_translation_active_inline(&env->pmmu030)) {
        tlb_set_page(cs, address & TARGET_PAGE_MASK,
                     address & TARGET_PAGE_MASK,
                     PAGE_READ | PAGE_WRITE | PAGE_EXEC,
                     mmu_idx, TARGET_PAGE_SIZE);
        return true;
    }

    function_code = qemu_pmmu_function_code(access_type, mmu_idx);
    access = qemu_pmmu_access(access_type);
    result = pmmu030_translate(&env->pmmu030, &bus, address,
                               function_code, access);
    if (likely(result.fault == PMMU030_FAULT_NONE)) {
        if (result.transparent ||
            (env->pmmu030.tc & PMMU030_TC_ENABLE) == 0u) {
            page_size = TARGET_PAGE_SIZE;
        } else {
            page_size = (target_ulong)1u <<
                        pmmu030_page_shift_inline(&env->pmmu030);
            if (page_size < TARGET_PAGE_SIZE) {
                page_size = TARGET_PAGE_SIZE;
            }
        }
        page_mask = ~(page_size - 1u);

        if (access_type == MMU_INST_FETCH) {
            prot = PAGE_EXEC;
        } else {
            prot = PAGE_READ;
            /* Force the first write through the PMMU to set M/U bits. */
            if (!result.write_protect &&
                (access_type == MMU_DATA_STORE || result.modified)) {
                prot |= PAGE_WRITE;
            }
        }
        tlb_set_page(cs, address & page_mask,
                     result.physical_address & page_mask,
                     prot, mmu_idx, page_size);
        return true;
    }

    if (probe) {
        return false;
    }

    env->mmu.ar = address;
    env->mmu.ssw = qemu_pmmu_fault_ssw(access_type, size, function_code);
    env->mmu.fault_030 = true;
    env->mmu.fault_030_fc = function_code;
    env->mmu.fault_030_size = size;
    env->mmu.fault_030_access = access_type;
    env->mmu.fault_030_kind = result.fault;
    cs->exception_index = EXCP_ACCESS;
    cpu_loop_exit_restore(cs, retaddr);
}

hwaddr m68k_pmmu030_get_phys_debug(CPUState *cs, vaddr address)
{
    CPUM68KState *env = cpu_env(cs);
    pmmu030_bus bus = qemu_pmmu_bus(env);
    pmmu030_result result;
    uint8_t function_code;

    if (!pmmu030_translation_active_inline(&env->pmmu030)) {
        return address;
    }
    function_code = (env->sr & SR_S ? 4u : 0u) | 1u;
    result = pmmu030_translate(&env->pmmu030, &bus, address,
                               function_code, PMMU030_ACCESS_READ);
    return result.fault == PMMU030_FAULT_NONE ? result.physical_address : -1;
}

G_NORETURN static void qemu_pmmu_raise(CPUM68KState *env, int exception,
                                       uintptr_t retaddr)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = exception;
    cpu_loop_exit_restore(cs, retaddr);
}

static bool qemu_pmmu_valid_ea(uint16_t opcode)
{
    unsigned mode = (opcode >> 3) & 7u;
    unsigned reg = opcode & 7u;

    return mode == 2u || mode == 5u || mode == 6u ||
           (mode == 7u && reg <= 1u);
}

static bool qemu_pmmu_decode_fc(CPUM68KState *env, uint16_t extension,
                                unsigned *function_code)
{
    unsigned field = extension & 31u;

    if ((field & 0x18u) == 0x10u) {
        *function_code = field & 7u;
        return true;
    }
    if ((field & 0x18u) == 0x08u) {
        *function_code = env->dregs[field & 7u] & 7u;
        return true;
    }
    if (field == 0u) {
        *function_code = env->sfc & 7u;
        return true;
    }
    if (field == 1u) {
        *function_code = env->dfc & 7u;
        return true;
    }
    return false;
}

static void qemu_pmmu_pmove_tt(CPUM68KState *env, uint32_t address,
                               uint16_t opcode, uint16_t extension,
                               uintptr_t retaddr)
{
    unsigned preg = (extension >> 10) & 7u;
    bool from_mmu = (extension & 0x0200u) != 0u;
    bool flush_disabled = (extension & 0x0100u) != 0u;

    if ((extension & 0x00ffu) != 0u || (preg != 2u && preg != 3u) ||
        !qemu_pmmu_valid_ea(opcode)) {
        qemu_pmmu_raise(env, EXCP_LINEF, retaddr);
    }
    if (from_mmu) {
        cpu_stl_data_ra(env, address,
                        preg == 2u ? env->pmmu030.tt0 : env->pmmu030.tt1,
                        retaddr);
        return;
    }
    if (preg == 2u) {
        pmmu030_write_tt0(&env->pmmu030,
                          cpu_ldl_data_ra(env, address, retaddr),
                          flush_disabled);
    } else {
        pmmu030_write_tt1(&env->pmmu030,
                          cpu_ldl_data_ra(env, address, retaddr),
                          flush_disabled);
    }
    tlb_flush(env_cpu(env));
}

static void qemu_pmmu_pmove_root_or_tc(CPUM68KState *env, uint32_t address,
                                       uint16_t opcode, uint16_t extension,
                                       uintptr_t retaddr)
{
    unsigned preg = (extension >> 10) & 7u;
    bool from_mmu = (extension & 0x0200u) != 0u;
    bool flush_disabled = (extension & 0x0100u) != 0u;
    bool valid;

    if ((extension & 0x00ffu) != 0u ||
        (preg != 0u && preg != 2u && preg != 3u) ||
        !qemu_pmmu_valid_ea(opcode)) {
        qemu_pmmu_raise(env, EXCP_LINEF, retaddr);
    }
    if (from_mmu) {
        if (preg == 0u) {
            cpu_stl_data_ra(env, address, env->pmmu030.tc, retaddr);
        } else {
            const uint32_t *root = preg == 2u ? env->pmmu030.srp :
                                                env->pmmu030.crp;
            cpu_stl_data_ra(env, address, root[0], retaddr);
            cpu_stl_data_ra(env, address + 4u, root[1], retaddr);
        }
        return;
    }

    if (preg == 0u) {
        valid = pmmu030_write_tc(&env->pmmu030,
                                 cpu_ldl_data_ra(env, address, retaddr),
                                 flush_disabled);
    } else {
        uint32_t high = cpu_ldl_data_ra(env, address, retaddr);
        uint32_t low = cpu_ldl_data_ra(env, address + 4u, retaddr);

        valid = preg == 2u ?
                pmmu030_write_srp(&env->pmmu030, high, low, flush_disabled) :
                pmmu030_write_crp(&env->pmmu030, high, low, flush_disabled);
    }
    tlb_flush(env_cpu(env));
    if (!valid) {
        qemu_pmmu_raise(env, EXCP_MMU_CONF, retaddr);
    }
}

static void qemu_pmmu_pmove_mmusr(CPUM68KState *env, uint32_t address,
                                  uint16_t opcode, uint16_t extension,
                                  uintptr_t retaddr)
{
    bool from_mmu = (extension & 0x0200u) != 0u;

    if ((extension & 0x1dffu) != 0u || !qemu_pmmu_valid_ea(opcode)) {
        qemu_pmmu_raise(env, EXCP_LINEF, retaddr);
    }
    if (from_mmu) {
        cpu_stw_data_ra(env, address, env->pmmu030.mmusr, retaddr);
    } else {
        pmmu030_write_mmusr(&env->pmmu030,
                            cpu_lduw_data_ra(env, address, retaddr));
    }
}

static void qemu_pmmu_pload(CPUM68KState *env, uint32_t address,
                            uint16_t opcode, uint16_t extension,
                            uintptr_t retaddr)
{
    pmmu030_bus bus = qemu_pmmu_bus(env);
    pmmu030_access access = (extension & 0x0200u) != 0u ?
                            PMMU030_ACCESS_READ : PMMU030_ACCESS_WRITE;
    unsigned function_code;

    if ((extension & 0x01e0u) != 0u || !qemu_pmmu_valid_ea(opcode) ||
        !qemu_pmmu_decode_fc(env, extension, &function_code)) {
        qemu_pmmu_raise(env, EXCP_LINEF, retaddr);
    }
    (void)pmmu030_pload(&env->pmmu030, &bus, address,
                        function_code, access);
    tlb_flush(env_cpu(env));
}

static void qemu_pmmu_pflush(CPUM68KState *env, uint32_t address,
                             uint16_t opcode, uint16_t extension,
                             uintptr_t retaddr)
{
    unsigned mode = (extension >> 10) & 7u;
    unsigned mask = (extension >> 5) & 7u;
    unsigned function_code;

    if ((extension & 0x0300u) != 0u) {
        qemu_pmmu_raise(env, EXCP_LINEF, retaddr);
    }
    if (mode == 1u) {
        if ((opcode & 0x3fu) != 0u || mask != 0u ||
            (extension & 31u) != 0u) {
            qemu_pmmu_raise(env, EXCP_LINEF, retaddr);
        }
        pmmu030_flush_all(&env->pmmu030);
        tlb_flush(env_cpu(env));
        return;
    }
    if ((mode != 4u && mode != 6u) ||
        !qemu_pmmu_decode_fc(env, extension, &function_code)) {
        qemu_pmmu_raise(env, EXCP_LINEF, retaddr);
    }
    if (mode == 4u) {
        if ((opcode & 0x3fu) != 0u) {
            qemu_pmmu_raise(env, EXCP_LINEF, retaddr);
        }
        pmmu030_flush(&env->pmmu030, function_code, mask, NULL);
    } else {
        if (!qemu_pmmu_valid_ea(opcode)) {
            qemu_pmmu_raise(env, EXCP_LINEF, retaddr);
        }
        pmmu030_flush(&env->pmmu030, function_code, mask, &address);
    }
    tlb_flush(env_cpu(env));
}

static void qemu_pmmu_ptest(CPUM68KState *env, uint32_t address,
                            uint16_t opcode, uint16_t extension,
                            uintptr_t retaddr)
{
    pmmu030_bus bus = qemu_pmmu_bus(env);
    unsigned level = (extension >> 10) & 7u;
    bool return_descriptor = (extension & 0x0100u) != 0u;
    unsigned address_register = (extension >> 5) & 7u;
    pmmu030_access access = (extension & 0x0200u) != 0u ?
                            PMMU030_ACCESS_READ : PMMU030_ACCESS_WRITE;
    unsigned function_code;
    pmmu030_result result;

    if ((!return_descriptor && address_register != 0u) ||
        (level == 0u && return_descriptor) || !qemu_pmmu_valid_ea(opcode) ||
        !qemu_pmmu_decode_fc(env, extension, &function_code)) {
        qemu_pmmu_raise(env, EXCP_LINEF, retaddr);
    }
    result = pmmu030_ptest(&env->pmmu030, &bus, address,
                           function_code, access, level);
    if (return_descriptor) {
        env->aregs[address_register] = result.last_descriptor_address;
    }
}

void HELPER(pmmu030_op)(CPUM68KState *env, uint32_t address,
                        uint32_t extension_value, uint32_t opcode_value)
{
    uintptr_t retaddr = GETPC();
    uint16_t extension = extension_value;
    uint16_t opcode = opcode_value;

    switch (extension >> 13) {
    case 0u:
        qemu_pmmu_pmove_tt(env, address, opcode, extension, retaddr);
        return;
    case 1u:
        if ((extension & 0xfc00u) == 0x2000u) {
            qemu_pmmu_pload(env, address, opcode, extension, retaddr);
        } else {
            qemu_pmmu_pflush(env, address, opcode, extension, retaddr);
        }
        return;
    case 2u:
        qemu_pmmu_pmove_root_or_tc(env, address, opcode, extension, retaddr);
        return;
    case 3u:
        qemu_pmmu_pmove_mmusr(env, address, opcode, extension, retaddr);
        return;
    case 4u:
        qemu_pmmu_ptest(env, address, opcode, extension, retaddr);
        return;
    default:
        qemu_pmmu_raise(env, EXCP_LINEF, retaddr);
    }
}

#endif
