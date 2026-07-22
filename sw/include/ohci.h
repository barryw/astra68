// OHCI 1.0 USB host interface for Astra 68.
// Registers are CPU-native 32-bit MMIO values. DMA structures are standard
// OHCI little-endian memory objects and must live in coherent/uncached memory.
#ifndef ASTRA_OHCI_H
#define ASTRA_OHCI_H

#include <stddef.h>
#include <stdint.h>

#define OHCI_BASE 0xFFF40000u
#define OHCI_DMA_POOL_BASE 0x03F00000u
#define OHCI_DMA_POOL_SIZE 0x00100000u

typedef volatile struct {
    uint32_t REVISION;             // 0x000
    uint32_t CONTROL;              // 0x004
    uint32_t COMMAND_STATUS;       // 0x008
    uint32_t INTERRUPT_STATUS;     // 0x00C
    uint32_t INTERRUPT_ENABLE;     // 0x010
    uint32_t INTERRUPT_DISABLE;    // 0x014
    uint32_t HCCA;                 // 0x018
    uint32_t PERIOD_CURRENT_ED;    // 0x01C
    uint32_t CONTROL_HEAD_ED;      // 0x020
    uint32_t CONTROL_CURRENT_ED;   // 0x024
    uint32_t BULK_HEAD_ED;         // 0x028
    uint32_t BULK_CURRENT_ED;      // 0x02C
    uint32_t DONE_HEAD;            // 0x030
    uint32_t FRAME_INTERVAL;       // 0x034
    uint32_t FRAME_REMAINING;      // 0x038
    uint32_t FRAME_NUMBER;         // 0x03C
    uint32_t PERIODIC_START;       // 0x040
    uint32_t LS_THRESHOLD;         // 0x044
    uint32_t RH_DESCRIPTOR_A;      // 0x048
    uint32_t RH_DESCRIPTOR_B;      // 0x04C
    uint32_t RH_STATUS;            // 0x050
    uint32_t RH_PORT_STATUS[1];    // 0x054
    uint32_t _reserved[(0xF00 - 0x058) / 4];

    // Astra integration diagnostics; outside the OHCI 1.0 register set.
    uint32_t ASTRA_ID;             // 0xF00 "AUSB"
    uint32_t ASTRA_VERSION;        // 0xF04
    uint32_t ASTRA_STATUS;         // 0xF08; write bit 0 to clear DMA fault
    uint32_t ASTRA_FAULT_ADDRESS;  // 0xF0C
    uint32_t ASTRA_DMA_POOL_BASE;  // 0xF10
    uint32_t ASTRA_DMA_POOL_SIZE;  // 0xF14
} OhciRegs;

#define OHCI ((OhciRegs *)OHCI_BASE)

#define OHCI_REVISION_1_0 0x10u
#define OHCI_ASTRA_ID_MAGIC 0x41555342u // "AUSB"
#define OHCI_ASTRA_VERSION_1_0 0x00010000u
#define OHCI_ASTRA_DMA_FAULT (1u << 0)
#define OHCI_ASTRA_IRQ       (1u << 1)

#define OHCI_CONTROL_PLE (1u << 2)
#define OHCI_CONTROL_IE  (1u << 3)
#define OHCI_CONTROL_CLE (1u << 4)
#define OHCI_CONTROL_BLE (1u << 5)
#define OHCI_CONTROL_HCFS_SHIFT 6
#define OHCI_CONTROL_HCFS_MASK (3u << OHCI_CONTROL_HCFS_SHIFT)
#define OHCI_CONTROL_HCFS_RESET       (0u << OHCI_CONTROL_HCFS_SHIFT)
#define OHCI_CONTROL_HCFS_RESUME      (1u << OHCI_CONTROL_HCFS_SHIFT)
#define OHCI_CONTROL_HCFS_OPERATIONAL (2u << OHCI_CONTROL_HCFS_SHIFT)
#define OHCI_CONTROL_HCFS_SUSPEND     (3u << OHCI_CONTROL_HCFS_SHIFT)

#define OHCI_COMMAND_HCR (1u << 0)
#define OHCI_COMMAND_CLF (1u << 1)
#define OHCI_COMMAND_BLF (1u << 2)
#define OHCI_COMMAND_OCR (1u << 3)

#define OHCI_INT_SO  (1u << 0)
#define OHCI_INT_WDH (1u << 1)
#define OHCI_INT_SF  (1u << 2)
#define OHCI_INT_RD  (1u << 3)
#define OHCI_INT_UE  (1u << 4)
#define OHCI_INT_FNO (1u << 5)
#define OHCI_INT_RHSC (1u << 6)
#define OHCI_INT_OC  (1u << 30)
#define OHCI_INT_MIE (1u << 31)

typedef struct {
    uint32_t interrupt_table[32];
    uint16_t frame_number;
    uint16_t pad1;
    uint32_t done_head;
    uint8_t reserved[120];
} OhciHcca;

typedef struct {
    uint32_t control;
    uint32_t tail_td;
    uint32_t head_td;
    uint32_t next_ed;
} OhciEndpointDescriptor;

typedef struct {
    uint32_t control;
    uint32_t current_buffer;
    uint32_t next_td;
    uint32_t buffer_end;
} OhciTransferDescriptor;

static inline uint16_t ohci_cpu_to_le16(uint16_t value)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return __builtin_bswap16(value);
#else
    return value;
#endif
}

static inline uint32_t ohci_cpu_to_le32(uint32_t value)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return __builtin_bswap32(value);
#else
    return value;
#endif
}

#define ohci_le16_to_cpu(value) ohci_cpu_to_le16(value)
#define ohci_le32_to_cpu(value) ohci_cpu_to_le32(value)

_Static_assert(offsetof(OhciRegs, RH_PORT_STATUS) == 0x054u,
               "OHCI port-status ABI offset");
_Static_assert(offsetof(OhciRegs, ASTRA_ID) == 0xF00u,
               "Astra OHCI extension ABI offset");
_Static_assert(sizeof(OhciHcca) == 256u, "OHCI HCCA size");
_Static_assert(sizeof(OhciEndpointDescriptor) == 16u,
               "OHCI endpoint descriptor size");
_Static_assert(sizeof(OhciTransferDescriptor) == 16u,
               "OHCI transfer descriptor size");

#endif // ASTRA_OHCI_H
