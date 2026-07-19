#include "interconnect.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "log.h"
#include "dma.h"
#include "gpu.h"
#include "ram.h"
#include "cpu.h"
#include "mdec.h"
#include "debugger.h"
#include "lua_debug.h"

// --- Memory Region Masking ---
const uint32_t REGION_MASK[8] = {
    0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, // KUSEG (no mask)
    0x7fffffff,                                      // KSEG0 (strip bit 31)
    0x1fffffff,                                      // KSEG1 (strip bits 31-29)
    0xffffffff, 0xffffffff                           // KSEG2 (no mask)
};

uint32_t mask_region(uint32_t addr) {
    return addr & REGION_MASK[(addr >> 29) & 7];
}

// --- Forward declarations ---
static void interconnect_perform_dma(Interconnect* inter, uint32_t channel_index);
static uint32_t dma_get_transfer_size_words(DmaChannel* ch);

// =============================================================================
// HW DISPATCH TABLE  (DuckStation-style, ref: bus.cpp:2046-2118)
//
// Hardware registers occupy 0x1F801000-0x1F801FFF (4 KB).
// We index by (phys >> 4) & 0xFF — one entry per 16-byte block (256 entries).
// Each region gets its own read/write handler that handles all access sizes.
// =============================================================================

typedef enum { BUS_BYTE = 1, BUS_HWORD = 2, BUS_WORD = 4 } BusSize;
typedef uint32_t (*HWReadFn) (Interconnect*, uint32_t addr, BusSize);
typedef void     (*HWWriteFn)(Interconnect*, uint32_t addr, uint32_t val, BusSize);

static HWReadFn  g_hw_read[256];
static HWWriteFn g_hw_write[256];

// ---------------------------------------------------------------------------
// Per-region handler prototypes
// ---------------------------------------------------------------------------
static uint32_t hw_memctrl_read  (Interconnect*, uint32_t, BusSize);
static void     hw_memctrl_write (Interconnect*, uint32_t, uint32_t, BusSize);
static uint32_t hw_sio_read      (Interconnect*, uint32_t, BusSize);
static void     hw_sio_write     (Interconnect*, uint32_t, uint32_t, BusSize);
static uint32_t hw_memctrl2_read (Interconnect*, uint32_t, BusSize);
static void     hw_memctrl2_write(Interconnect*, uint32_t, uint32_t, BusSize);
static uint32_t hw_irq_read      (Interconnect*, uint32_t, BusSize);
static void     hw_irq_write     (Interconnect*, uint32_t, uint32_t, BusSize);
static uint32_t hw_dma_read      (Interconnect*, uint32_t, BusSize);
static void     hw_dma_write     (Interconnect*, uint32_t, uint32_t, BusSize);
static uint32_t hw_timers_read   (Interconnect*, uint32_t, BusSize);
static void     hw_timers_write  (Interconnect*, uint32_t, uint32_t, BusSize);
static uint32_t hw_cdrom_read    (Interconnect*, uint32_t, BusSize);
static void     hw_cdrom_write   (Interconnect*, uint32_t, uint32_t, BusSize);
static uint32_t hw_gpu_read      (Interconnect*, uint32_t, BusSize);
static void     hw_gpu_write     (Interconnect*, uint32_t, uint32_t, BusSize);
static uint32_t hw_mdec_read     (Interconnect*, uint32_t, BusSize);
static void     hw_mdec_write    (Interconnect*, uint32_t, uint32_t, BusSize);
static uint32_t hw_spu_read      (Interconnect*, uint32_t, BusSize);
static void     hw_spu_write     (Interconnect*, uint32_t, uint32_t, BusSize);
static uint32_t hw_unmapped_read (Interconnect*, uint32_t, BusSize);
static void     hw_unmapped_write(Interconnect*, uint32_t, uint32_t, BusSize);

// ---------------------------------------------------------------------------
// Table initialisation
// ---------------------------------------------------------------------------
#define HW_SET(start, end, rfn, wfn) do { \
    for (uint32_t _a = (start); _a < (end); _a += 16) { \
        g_hw_read[(_a >> 4) & 0xFF]  = (rfn); \
        g_hw_write[(_a >> 4) & 0xFF] = (wfn); \
    } \
} while (0)

void bus_hw_tables_init(void) {
    for (int i = 0; i < 256; i++) {
        g_hw_read[i]  = hw_unmapped_read;
        g_hw_write[i] = hw_unmapped_write;
    }
    // MemCtrl  0x1F801000-0x1F80103F  (indices 0x00-0x03)
    HW_SET(0x1F801000, 0x1F801040, hw_memctrl_read,   hw_memctrl_write);
    // PAD/SIO  0x1F801040-0x1F80105F  (indices 0x04-0x05)
    HW_SET(0x1F801040, 0x1F801060, hw_sio_read,       hw_sio_write);
    // RAM_SIZE 0x1F801060-0x1F80106F  (index  0x06)
    HW_SET(0x1F801060, 0x1F801070, hw_memctrl2_read,  hw_memctrl2_write);
    // IRQ      0x1F801070-0x1F80107F  (index  0x07)
    HW_SET(0x1F801070, 0x1F801080, hw_irq_read,       hw_irq_write);
    // DMA      0x1F801080-0x1F8010FF  (indices 0x08-0x0F)
    HW_SET(0x1F801080, 0x1F801100, hw_dma_read,       hw_dma_write);
    // Timers   0x1F801100-0x1F80112F  (indices 0x10-0x12)
    HW_SET(0x1F801100, 0x1F801130, hw_timers_read,    hw_timers_write);
    // CDROM    0x1F801800-0x1F80180F  (index  0x80)
    HW_SET(0x1F801800, 0x1F801810, hw_cdrom_read,     hw_cdrom_write);
    // GPU      0x1F801810-0x1F80181F  (index  0x81)
    HW_SET(0x1F801810, 0x1F801820, hw_gpu_read,       hw_gpu_write);
    // MDEC     0x1F801820-0x1F80182F  (index  0x82)
    HW_SET(0x1F801820, 0x1F801830, hw_mdec_read,      hw_mdec_write);
    // SPU      0x1F801C00-0x1F801E7F  (indices 0xC0-0xE7)
    HW_SET(0x1F801C00, 0x1F801E80, hw_spu_read,       hw_spu_write);
}

// =============================================================================
// PER-REGION HANDLERS
// =============================================================================

// --- MemCtrl (0x1F801000-0x1F80103F) ---
// Real per-region access-delay registers (nocash spec), ported from DuckStation's
// Bus::CalculateMemoryTiming. Real BIOS/EXP1/CDROM/SPU ROM/bus accesses cost several
// cycles each — not the flat 1 cycle/instruction this project used to assume everywhere.
static uint32_t calc_memory_timing_word_cycles(uint32_t mem_delay, uint32_t common_delay) {
    int32_t access_time    = (int32_t)((mem_delay >> 4) & 0xF);
    bool use_com0          = (mem_delay >> 8)  & 1;
    bool use_com2          = (mem_delay >> 10) & 1;
    bool use_com3          = (mem_delay >> 11) & 1;
    bool data_bus_16bit    = (mem_delay >> 12) & 1;

    int32_t com0 = (int32_t)(common_delay & 0xF);
    int32_t com2 = (int32_t)((common_delay >> 8)  & 0xF);
    int32_t com3 = (int32_t)((common_delay >> 12) & 0xF);

    int32_t first = 0, seq = 0, min_cycles = 0;
    if (use_com0) { first += com0 - 1; seq += com0 - 1; }
    if (use_com2) { first += com2; seq += com2; }
    if (use_com3) { min_cycles = com3; }
    if (first < 6) first++;
    first = first + access_time + 2;
    seq   = seq   + access_time + 2;
    if (first < min_cycles + 6) first = min_cycles + 6;
    if (seq   < min_cycles + 2) seq   = min_cycles + 2;

    int32_t word_time = data_bus_16bit ? (first + seq) : (first + seq + seq + seq);
    return (uint32_t)(word_time - 1 > 0 ? word_time - 1 : 0);
}

void bus_memctrl_recalculate(Interconnect* inter) {
    inter->bios_access_cycles = calc_memory_timing_word_cycles(inter->memctrl_regs[4], inter->memctrl_regs[8]);
    LOG_INTERCONNECT_DEBUG("[BUS] Memory timing recalculated: BIOS word access = %u extra cycles",
                            inter->bios_access_cycles);
}

void bus_memctrl_init(Interconnect* inter) {
    static const uint32_t defaults[9] = {
        0x1F000000, 0x1F802000, 0x0013243F, 0x00003022,
        0x0013243F, 0x200931E1, 0x00020843, 0x00070777, 0x00031125,
    };
    memcpy(inter->memctrl_regs, defaults, sizeof(defaults));
    bus_memctrl_recalculate(inter);
}

static uint32_t hw_memctrl_read(Interconnect* inter, uint32_t addr, BusSize sz) {
    uint32_t off = addr - 0x1F801000;
    uint32_t idx = off >> 2;
    uint32_t v32 = (idx < 9) ? inter->memctrl_regs[idx] : 0;
    if (sz == BUS_WORD)  return v32;
    if (sz == BUS_HWORD) return (uint16_t)(v32 >> ((off & 2) << 3));
    return (uint8_t)(v32 >> ((off & 3) << 3));
}

static void hw_memctrl_write(Interconnect* inter, uint32_t addr, uint32_t val, BusSize sz) {
    (void)sz;
    if      (addr == 0x1F801000 && val != 0x1F000000)
        LOG_INTERCONNECT_WARN("[BUS] Bad EXP1 base write: 0x%08x", val);
    else if (addr == 0x1F801004 && val != 0x1F802000)
        LOG_INTERCONNECT_WARN("[BUS] Bad EXP2 base write: 0x%08x", val);

    uint32_t idx = (addr - 0x1F801000) >> 2;
    if (idx < 9) {
        inter->memctrl_regs[idx] = val;
        if (idx == 4 || idx == 8) // bios_delay or common_delay
            bus_memctrl_recalculate(inter);
    }
}

// --- PAD/SIO (0x1F801040-0x1F80105F) ---
static uint32_t hw_sio_read(Interconnect* inter, uint32_t addr, BusSize sz) {
    uint32_t off = addr - 0x1F801040;
    if (sz == BUS_WORD)  return sio_read32(&inter->sio, off);
    if (sz == BUS_HWORD) return sio_read16(&inter->sio, off);
    return sio_read8(&inter->sio, off);
}

static void hw_sio_write(Interconnect* inter, uint32_t addr, uint32_t val, BusSize sz) {
    uint32_t off = addr - 0x1F801040;
    if      (sz == BUS_WORD)  sio_write32(&inter->sio, off, val);
    else if (sz == BUS_HWORD) sio_write16(&inter->sio, off, (uint16_t)val);
    else                      sio_write8 (&inter->sio, off, (uint8_t)val);
    if (inter->sio.pending_irq) {
        inter->sio.pending_irq = false;
        interconnect_set_irq_line(inter, IRQ_CTRLMEMCARD, true);
        interconnect_set_irq_line(inter, IRQ_CTRLMEMCARD, false);
    }
}

// --- MemCtrl2 / RAM_SIZE (0x1F801060-0x1F80106F) ---
static uint32_t hw_memctrl2_read(Interconnect* inter, uint32_t addr, BusSize sz) {
    (void)inter; (void)addr; (void)sz;
    return 0x00000B88;
}
static void hw_memctrl2_write(Interconnect* inter, uint32_t addr, uint32_t val, BusSize sz) {
    (void)inter; (void)addr; (void)val; (void)sz;
}

// --- IRQ (0x1F801070-0x1F80107F) ---
static uint32_t hw_irq_read(Interconnect* inter, uint32_t addr, BusSize sz) {
    uint32_t v32 = (addr == IRQ_STATUS_ADDR) ? inter->irq_status : inter->irq_mask;
    if (sz == BUS_WORD)  return v32;
    if (sz == BUS_HWORD) return (uint16_t)v32;
    return (uint8_t)v32;
}

static void hw_irq_write(Interconnect* inter, uint32_t addr, uint32_t val, BusSize sz) {
    (void)sz;
    if (addr == IRQ_STATUS_ADDR) {
        static const char* const names[] = {
            "VBLANK","GPU","CDROM","DMA","TMR0","TMR1","TMR2","PAD","SIO","SPU","IRQ10"
        };
        // PSX I_STAT: write 0 to clear, write 1 to keep.
        // wv = bits to KEEP; bits ~wv are being cleared.
        const uint16_t wv      = (uint16_t)(val & 0x7FF);
        const uint16_t cleared = (uint16_t)(inter->irq_status & ~wv);
        for (uint32_t i = 0; i < 11; i++)
            if (cleared & (1u << i)) LOG_IRQ_DEBUG("%s IRQ cleared", names[i]);
        inter->irq_status    &= wv;
        inter->irq_line_state &= wv;  // allow re-fire: next set_irq_line(true) is fresh edge
        if ((cleared & (1u << IRQ_SPU)) && inter->spu.irq9_flag) {
            inter->spu.irq9_flag  = false;
            inter->spu.status    &= ~SPU_STATUS_IRQ9_FLAG;
            interconnect_set_irq_line(inter, IRQ_SPU, false);
            LOG_IRQ_DEBUG("SPU IRQ9 edge-trigger reset");
        }
        if (inter->cpu) inter->cpu->downcount = 0;
    } else if (addr == IRQ_MASK_ADDR) {
        inter->irq_mask = (uint16_t)(val & 0x7FF);
        LOG_IRQ_DEBUG("I_MASK <- 0x%03x", val & 0x7FF);
        if (inter->cpu) inter->cpu->downcount = 0;
    }
}

// --- DMA (0x1F801080-0x1F8010FF) ---
static uint32_t hw_dma_read(Interconnect* inter, uint32_t addr, BusSize sz) {
    uint32_t off = addr - DMA_START;
    uint32_t v32 = dma_read(&inter->dma, off);
    if (sz == BUS_WORD)  return v32;
    if (sz == BUS_HWORD) return (uint16_t)(v32 >> ((addr & 2) << 3));
    return (uint8_t)(v32 >> ((addr & 3) << 3));
}

static void hw_dma_write(Interconnect* inter, uint32_t addr, uint32_t val, BusSize sz) {
    uint32_t off = addr - DMA_START;
    if (sz != BUS_WORD) {
        LOG_DMA_WARN("[DMA] Non-word write at 0x%08x sz=%d", addr, sz);
        return;
    }
    bool channel_became_active = dma_write(&inter->dma, off, val);
    if (channel_became_active) {
        uint32_t ch = (off >> 4) & 0x7;
        if ((inter->dma.control >> (ch * 4 + 3)) & 1u) {
            LOG_DMA_DEBUG("[DMA] ch%u activated (DPCR enabled)", ch);
            interconnect_perform_dma(inter, ch);
        } else {
            LOG_DMA_DEBUG("[DMA] ch%u blocked by DPCR", ch);
        }
    } else if (off == 0x70) {
        // DPCR write: unblock any already-active channels
        for (uint32_t i = 0; i < 7; i++) {
            if (((inter->dma.control >> (i * 4 + 3)) & 1u) &&
                dma_channel_is_active(&inter->dma.channels[i])) {
                LOG_DMA_DEBUG("[DMA] DPCR write: ch%u unblocked", i);
                interconnect_perform_dma(inter, i);
            }
        }
    }
}

// --- Timers (0x1F801100-0x1F80112F) ---
static uint32_t hw_timers_read(Interconnect* inter, uint32_t addr, BusSize sz) {
    int      idx = (int)((addr - 0x1F801100) / 0x10);
    uint32_t off = addr & 0xF;
    if (sz == BUS_WORD)  return timer_read32(&inter->timers_state, idx, off);
    if (sz == BUS_HWORD) return timer_read16(&inter->timers_state, idx, off);
    LOG_INTERCONNECT_WARN("[BUS] Timer byte read 0x%08x", addr);
    return 0xFF;
}
static void hw_timers_write(Interconnect* inter, uint32_t addr, uint32_t val, BusSize sz) {
    int      idx = (int)((addr - 0x1F801100) / 0x10);
    uint32_t off = addr & 0xF;
    if      (sz == BUS_WORD)  { timer_write32(&inter->timers_state, idx, off, val); return; }
    if      (sz == BUS_HWORD) { timer_write16(&inter->timers_state, idx, off, (uint16_t)val); return; }
    LOG_INTERCONNECT_WARN("[BUS] Timer byte write 0x%08x = 0x%02x", addr, val);
}

// --- CDROM (0x1F801800-0x1F80180F) ---
static uint32_t hw_cdrom_read(Interconnect* inter, uint32_t addr, BusSize sz) {
    if (sz == BUS_BYTE)  return cdrom_read8(&inter->cdrom, addr);
    if (sz == BUS_HWORD) {
        LOG_CDROM_WARN("[CDROM] Read16 at 0x%08x (UNEXPECTED SIZE)", addr);
        return (uint32_t)cdrom_read8(&inter->cdrom, addr) |
               ((uint32_t)cdrom_read8(&inter->cdrom, addr + 1) << 8);
    }
    LOG_CDROM_WARN("[CDROM] Read32 at 0x%08x (UNEXPECTED SIZE)", addr);
    return (uint32_t)cdrom_read8(&inter->cdrom, addr)     |
           ((uint32_t)cdrom_read8(&inter->cdrom, addr+1) << 8)  |
           ((uint32_t)cdrom_read8(&inter->cdrom, addr+2) << 16) |
           ((uint32_t)cdrom_read8(&inter->cdrom, addr+3) << 24);
}
static void hw_cdrom_write(Interconnect* inter, uint32_t addr, uint32_t val, BusSize sz) {
    if (sz == BUS_BYTE) { cdrom_write8(&inter->cdrom, addr, (uint8_t)val); return; }
    LOG_CDROM_WARN("[CDROM] Write%s at 0x%08x (UNEXPECTED SIZE)",
                   sz == BUS_HWORD ? "16" : "32", addr);
    cdrom_write8(&inter->cdrom, addr, (uint8_t)val);
}

// --- GPU (0x1F801810-0x1F80181F) ---
static uint32_t hw_gpu_read(Interconnect* inter, uint32_t addr, BusSize sz) {
    uint32_t v32 = (addr == GPU_GPUREAD_ADDR) ? gpu_read_data(&inter->gpu)
                                               : gpu_read_status(&inter->gpu);
    if (sz == BUS_WORD)  return v32;
    if (sz == BUS_HWORD) return (uint16_t)(v32 >> ((addr & 2) << 3));
    return (uint8_t)(v32 >> ((addr & 3) << 3));
}
static void hw_gpu_write(Interconnect* inter, uint32_t addr, uint32_t val, BusSize sz) {
    if (sz != BUS_WORD) {
        LOG_GPU_WARN("[GPU] Write%s at 0x%08x (UNEXPECTED SIZE)",
                     sz == BUS_HWORD ? "16" : "8", addr);
        return;
    }
    if (addr == GPU_GP0_ADDR) gpu_gp0(&inter->gpu, val);
    else                      gpu_gp1(&inter->gpu, val);
}

// --- MDEC (0x1F801820-0x1F80182F) ---
static uint32_t hw_mdec_read(Interconnect* inter, uint32_t addr, BusSize sz) {
    uint32_t v32 = mdec_read(&inter->mdec, addr);
    if (sz == BUS_WORD)  return v32;
    if (sz == BUS_HWORD) return (uint16_t)(v32 >> ((addr & 2) << 3));
    return (uint8_t)(v32 >> ((addr & 3) << 3));
}
static void hw_mdec_write(Interconnect* inter, uint32_t addr, uint32_t val, BusSize sz) {
    if (sz == BUS_WORD) { mdec_write(&inter->mdec, addr, val); return; }
    LOG_INTERCONNECT_WARN("[BUS] MDEC write%s at 0x%08x",
                          sz == BUS_HWORD ? "16" : "8", addr);
}

// --- SPU (0x1F801C00-0x1F801E7F) ---
static uint32_t hw_spu_read(Interconnect* inter, uint32_t addr, BusSize sz) {
    if (sz == BUS_WORD)  return spu_read32(inter, addr);
    if (sz == BUS_HWORD) return spu_read16(inter, addr);
    return (uint8_t)(spu_read16(inter, addr & ~1u) >> ((addr & 1u) << 3));
}
static void hw_spu_write(Interconnect* inter, uint32_t addr, uint32_t val, BusSize sz) {
    if      (sz == BUS_WORD)  { spu_write32(inter, addr, val); return; }
    if      (sz == BUS_HWORD) { spu_write16(inter, addr, (uint16_t)val); return; }
    spu_write8(inter, addr, (uint8_t)val);
}

// --- Unmapped HW region ---
static uint32_t hw_unmapped_read(Interconnect* inter, uint32_t addr, BusSize sz) {
    (void)inter;
    LOG_INTERCONNECT_WARN("[BUS] Unmapped HW read%d 0x%08x", sz * 8, addr);
    return 0xFFFFFFFF;
}
static void hw_unmapped_write(Interconnect* inter, uint32_t addr, uint32_t val, BusSize sz) {
    (void)inter;
    LOG_INTERCONNECT_WARN("[BUS] Unmapped HW write%d 0x%08x = 0x%x", sz * 8, addr, val);
}

// =============================================================================
// SCRATCHPAD HELPERS  (1 KB fast RAM, KUSEG/KSEG0 only, not KSEG1)
// =============================================================================
static inline uint32_t sp_load32(Interconnect* i, uint32_t off) {
    return (uint32_t)i->scratchpad[off]     | ((uint32_t)i->scratchpad[off+1] << 8)
         | ((uint32_t)i->scratchpad[off+2] << 16) | ((uint32_t)i->scratchpad[off+3] << 24);
}
static inline uint16_t sp_load16(Interconnect* i, uint32_t off) {
    return (uint16_t)i->scratchpad[off] | ((uint16_t)i->scratchpad[off+1] << 8);
}
static inline void sp_store32(Interconnect* i, uint32_t off, uint32_t v) {
    i->scratchpad[off]   = (uint8_t)(v);
    i->scratchpad[off+1] = (uint8_t)(v >> 8);
    i->scratchpad[off+2] = (uint8_t)(v >> 16);
    i->scratchpad[off+3] = (uint8_t)(v >> 24);
}
static inline void sp_store16(Interconnect* i, uint32_t off, uint16_t v) {
    i->scratchpad[off]   = (uint8_t)(v);
    i->scratchpad[off+1] = (uint8_t)(v >> 8);
}

// =============================================================================
// LOAD OPERATIONS
// =============================================================================

uint32_t interconnect_load32(Interconnect* inter, uint32_t address) {
    if (address & 3) {
        LOG_INTERCONNECT_ERROR("[BUS] Unaligned load32 0x%08x", address);
        if (inter->cpu) { inter->cpu->badvaddr = address; cpu_exception(inter->cpu, EXCEPTION_LOAD_ADDRESS_ERROR); }
        return 0;
    }
    debugger_check_read_watchpoint(&inter->debugger, inter->cpu, address, 4);
    const uint32_t phys = mask_region(address);

    // RAM (2 MB, mirrored in first 8 MB)
    if (phys < 0x00800000) {
        uint32_t ram_off = phys & (RAM_SIZE - 1);
        uint32_t val = ram_load32(inter->ram, ram_off);
        return val;
    }
    // Scratchpad (KUSEG/KSEG0 only — excluded in KSEG1)
    if (phys >= 0x1F800000 && phys < 0x1F800400 && address < 0xA0000000)
        return sp_load32(inter, phys - 0x1F800000);
    // Expansion 1 (unpopulated)
    if (phys >= 0x1F000000 && phys < 0x1F800000) return 0x00000000;
    // Hardware registers 0x1F801000-0x1F801FFF
    if (phys >= 0x1F801000 && phys < 0x1F802000)
        return g_hw_read[(phys >> 4) & 0xFF](inter, phys, BUS_WORD);
    // Expansion 2 (reads: open bus)
    if (phys >= 0x1F802000 && phys < 0x1F804000) return 0xFFFFFFFF;
    // Expansion 3
    if (phys >= 0x1FA00000 && phys < 0x1FC00000) return 0x00000000;
    // BIOS ROM (512 KB, mirrored up to 4 MB above start)
    if (phys >= 0x1FC00000 && phys < 0x20000000)
        return bios_load32(inter->bios, (phys - 0x1FC00000) & (BIOS_SIZE - 1));
    // Cache Control (KSEG2)
    if (phys == CACHE_CONTROL_ADDR) return 0;
    // Large unmapped windows — return 0 silently
    if (phys >= 0x20000000) return 0;

    LOG_INTERCONNECT_WARN("[BUS] Unmapped load32 0x%08x (phys 0x%08x)", address, phys);
    return 0xFFFFFFFF;
}

uint16_t interconnect_load16(Interconnect* inter, uint32_t address) {
    if (address & 1) {
        LOG_INTERCONNECT_ERROR("[BUS] Unaligned load16 0x%08x", address);
        if (inter->cpu) { inter->cpu->badvaddr = address; cpu_exception(inter->cpu, EXCEPTION_LOAD_ADDRESS_ERROR); }
        return 0;
    }
    debugger_check_read_watchpoint(&inter->debugger, inter->cpu, address, 2);
    const uint32_t phys = mask_region(address);

    if (phys < 0x00800000)
        return ram_load16(inter->ram, phys & (RAM_SIZE - 1));
    if (phys >= 0x1F800000 && phys < 0x1F800400 && address < 0xA0000000)
        return sp_load16(inter, phys - 0x1F800000);
    if (phys >= 0x1F000000 && phys < 0x1F800000) return 0x0000;
    if (phys >= 0x1F801000 && phys < 0x1F802000)
        return (uint16_t)g_hw_read[(phys >> 4) & 0xFF](inter, phys, BUS_HWORD);
    if (phys >= 0x1F802000 && phys < 0x1F804000) return 0xFFFF;
    if (phys >= 0x1FA00000 && phys < 0x1FC00000) return 0x0000;
    if (phys >= 0x1FC00000 && phys < 0x20000000)
        return bios_load16(inter->bios, (phys - 0x1FC00000) & (BIOS_SIZE - 1));
    if (phys >= 0x20000000) return 0;

    LOG_INTERCONNECT_WARN("[BUS] Unmapped load16 0x%08x (phys 0x%08x)", address, phys);
    return 0xFFFF;
}

uint8_t interconnect_load8(Interconnect* inter, uint32_t address) {
    debugger_check_read_watchpoint(&inter->debugger, inter->cpu, address, 1);
    const uint32_t phys = mask_region(address);

    if (phys < 0x00800000)
        return ram_load8(inter->ram, phys & (RAM_SIZE - 1));
    if (phys >= 0x1F800000 && phys < 0x1F800400 && address < 0xA0000000)
        return inter->scratchpad[phys - 0x1F800000];
    if (phys >= 0x1F000000 && phys < 0x1F800000) return 0x00;
    if (phys >= 0x1F801000 && phys < 0x1F802000)
        return (uint8_t)g_hw_read[(phys >> 4) & 0xFF](inter, phys, BUS_BYTE);
    if (phys >= 0x1F802000 && phys < 0x1F804000) return 0xFF;
    if (phys >= 0x1FA00000 && phys < 0x1FC00000) return 0x00;
    if (phys >= 0x1FC00000 && phys < 0x20000000)
        return inter->bios->data[(phys - 0x1FC00000) & (BIOS_SIZE - 1)];
    if (phys >= 0x20000000) return 0;

    LOG_INTERCONNECT_WARN("[BUS] Unmapped load8 0x%08x (phys 0x%08x)", address, phys);
    return 0xFF;
}

// =============================================================================
// STORE OPERATIONS
// =============================================================================

void interconnect_store32(Interconnect* inter, uint32_t address, uint32_t value) {
    if (address & 3) {
        LOG_INTERCONNECT_ERROR("[BUS] Unaligned store32 0x%08x = 0x%08x", address, value);
        if (inter->cpu) { inter->cpu->badvaddr = address; cpu_exception(inter->cpu, EXCEPTION_STORE_ADDRESS_ERROR); }
        return;
    }
    debugger_check_write_watchpoint(&inter->debugger, inter->cpu, address, 4);
    const uint32_t phys = mask_region(address);

    if (phys < 0x00800000) {
        uint32_t ram_off = phys & (RAM_SIZE - 1);
        ram_store32(inter->ram, ram_off, value);
        return;
    }
    if (phys >= 0x1F800000 && phys < 0x1F800400) { sp_store32(inter, phys - 0x1F800000, value); return; }
    if (phys >= 0x1F000000 && phys < 0x1F800000) return; // EXP1
    if (phys >= 0x1F801000 && phys < 0x1F802000) { g_hw_write[(phys>>4)&0xFF](inter, phys, value, BUS_WORD); return; }
    if (phys >= 0x1F802000 && phys < 0x1F804000) return; // EXP2
    if (phys >= 0x1FA00000 && phys < 0x1FC00000) return; // EXP3
    if (phys >= 0x1FC00000 && phys < 0x20000000) { LOG_INTERCONNECT_ERROR("[BUS] Write to BIOS ROM 0x%08x", address); return; }
    if (phys == CACHE_CONTROL_ADDR) return;
    if (phys >= 0x20000000) return; // Large unmapped windows

    LOG_INTERCONNECT_WARN("[BUS] Unhandled store32 0x%08x = 0x%08x", address, value);
}

void interconnect_store16(Interconnect* inter, uint32_t address, uint16_t value) {
    if (address & 1) {
        LOG_INTERCONNECT_ERROR("[BUS] Unaligned store16 0x%08x = 0x%04x", address, value);
        if (inter->cpu) { inter->cpu->badvaddr = address; cpu_exception(inter->cpu, EXCEPTION_STORE_ADDRESS_ERROR); }
        return;
    }
    debugger_check_write_watchpoint(&inter->debugger, inter->cpu, address, 2);
    const uint32_t phys = mask_region(address);

    if (phys < 0x00800000) {
        uint32_t ram_off = phys & (RAM_SIZE - 1);
        ram_store16(inter->ram, ram_off, value);
        return;
    }
    if (phys >= 0x1F800000 && phys < 0x1F800400) { sp_store16(inter, phys - 0x1F800000, value); return; }
    if (phys >= 0x1F000000 && phys < 0x1F800000) return;
    if (phys >= 0x1F801000 && phys < 0x1F802000) { g_hw_write[(phys>>4)&0xFF](inter, phys, value, BUS_HWORD); return; }
    if (phys >= 0x1F802000 && phys < 0x1F804000) return; // EXP2 16-bit: ignored
    if (phys >= 0x1FA00000 && phys < 0x1FC00000) return;
    if (phys >= 0x1FC00000 && phys < 0x20000000) { LOG_INTERCONNECT_ERROR("[BUS] Write16 to BIOS ROM 0x%08x", address); return; }
    if (phys >= 0x20000000) return;

    LOG_INTERCONNECT_WARN("[BUS] Unhandled store16 0x%08x = 0x%04x", address, value);
}

void interconnect_store8(Interconnect* inter, uint32_t address, uint8_t value) {
    debugger_check_write_watchpoint(&inter->debugger, inter->cpu, address, 1);
    const uint32_t phys = mask_region(address);

    if (phys < 0x00800000) {
        uint32_t ram_off = phys & (RAM_SIZE - 1);
        ram_store8(inter->ram, ram_off, value);
        return;
    }
    if (phys >= 0x1F800000 && phys < 0x1F800400) { inter->scratchpad[phys - 0x1F800000] = value; return; }
    if (phys >= 0x1F000000 && phys < 0x1F800000) return; // EXP1
    if (phys >= 0x1F801000 && phys < 0x1F802000) { g_hw_write[(phys>>4)&0xFF](inter, phys, value, BUS_BYTE); return; }
    // EXP2 (0x1F802000-0x1F803FFF) — only offset 0x23/0x80 is the TTY char
    // port (matches DuckStation's EXP2WriteHandler). The rest of the range is
    // POST status codes and other diagnostic registers — capturing writes to
    // those as if they were TTY text corrupts the log with garbage characters.
    if (phys >= 0x1F802000 && phys < 0x1F804000) {
        uint32_t offset = phys - 0x1F802000;
        if (offset == 0x23 || offset == 0x80) {
            char ch = (char)(value & 0xFF);
            if ((uint8_t)ch >= 0x20 && (uint8_t)ch < 0x7F) {
                if (inter->tty_line_len < (int)(sizeof(inter->tty_line_buf) - 1))
                    inter->tty_line_buf[inter->tty_line_len++] = ch;
            } else if (ch == '\n' || ch == '\r') {
                if (inter->tty_line_len > 0) {
                    inter->tty_line_buf[inter->tty_line_len] = '\0';
                    log_print_tty(inter->tty_line_buf);
                    inter->tty_line_len = 0;
                }
            }
        } else if (offset == 0x41 || offset == 0x42) {
            LOG_BIOS_DEBUG("[BUS] BIOS POST status: 0x%02x", (unsigned)(value & 0x0F));
        } else if (offset == 0x70) {
            LOG_BIOS_DEBUG("[BUS] BIOS POST2 status: 0x%02x", (unsigned)(value & 0x0F));
        }
        return;
    }
    if (phys >= 0x1FA00000 && phys < 0x1FC00000) return;
    if (phys >= 0x1FC00000 && phys < 0x20000000) { LOG_INTERCONNECT_ERROR("[BUS] Write8 to BIOS ROM 0x%08x", address); return; }
    if (phys >= 0x20000000) return;

    LOG_INTERCONNECT_WARN("[BUS] Unhandled store8 0x%08x = 0x%02x", address, value);
}

// =============================================================================
// DMA TRANSFER LOGIC
// =============================================================================

#define DMA_SLICE_WORDS  64    /* GPU commands per slice before yielding */
#define DMA_SLICE_CYCLES 1000  /* EVQ cycles between slices (~30µs) */

/* Signal DMA ch2 completion IRQ */
static void dma_ch2_signal_done(Interconnect* inter) {
    DmaChannel* ch = &inter->dma.channels[2];
    dma_channel_done(ch);
    /* Fixed stall per slice (exact accounting happens across EVQ_DMA_GPU reschedules) */
    if (inter->cpu) inter->cpu->downcount -= (int32_t)DMA_SLICE_CYCLES;

    if (inter->dma.channel_irq_enable & (1u << 2)) {
        inter->dma.channel_irq_flags |= (1u << 2);
        inter->dma.master_irq_flag = inter->dma.force_irq ||
            (inter->dma.master_irq_enable &&
             (inter->dma.channel_irq_flags & inter->dma.channel_irq_enable) != 0);
        if (inter->dma.master_irq_flag && !(inter->irq_status & (1u << IRQ_DMA)))
            interconnect_request_irq(inter, IRQ_DMA, "DMA ch2 done");
    }
    lua_debug_notify("dma_ch2_done");
}

/* Run one slice of the GPU DMA transfer. Returns true when fully done. */
static bool dma_gpu_run_slice(Interconnect* inter) {
    Dma* dma = &inter->dma;
    uint32_t words_done = 0;

    if (dma->gpu_ll_active) {
        uint32_t addr = dma->gpu_ll_addr;
        while (words_done < DMA_SLICE_WORDS) {
            if (addr >= RAM_SIZE) {
                LOG_DMA_ERROR("[DMA] GPU LL: addr 0x%08x out of bounds", addr);
                dma->gpu_ll_active = false;
                return true;
            }
            uint32_t header    = interconnect_load32(inter, addr);
            uint32_t num_words = header >> 24;
            uint32_t raw_next  = header & 0x00FFFFFF;
            uint32_t next_addr = raw_next & 0x00FFFFFC;

            for (uint32_t i = 0; i < num_words; i++) {
                addr = (addr + 4) & 0x00FFFFFC;
                if (addr >= RAM_SIZE) {
                    dma->gpu_ll_active = false;
                    return true;
                }
                gpu_gp0(&inter->gpu, interconnect_load32(inter, addr));
                words_done++;
            }

            if (raw_next & 0x800000u) {
                LOG_DMA_TRACE("[DMA] GPU LL done after %u words (sliced)", words_done);
                dma->gpu_ll_active = false;
                return true;
            }
            if (next_addr >= RAM_SIZE) {
                LOG_DMA_ERROR("[DMA] GPU LL: next 0x%08x out of bounds", next_addr);
                dma->gpu_ll_active = false;
                return true;
            }
            addr = next_addr;
            words_done++;  /* count header traversal */
        }
        dma->gpu_ll_addr = addr;
        return false;
    }

    if (dma->gpu_req_active) {
        uint32_t addr      = dma->gpu_req_addr;
        uint32_t remaining = dma->gpu_req_remaining;
        while (words_done < DMA_SLICE_WORDS && remaining > 0) {
            uint32_t cur = addr & 0x00FFFFFC;
            if (cur >= RAM_SIZE) {
                LOG_DMA_ERROR("[DMA] GPU req: addr 0x%08x out of bounds", cur);
                dma->gpu_req_active = false;
                return true;
            }
            gpu_gp0(&inter->gpu, interconnect_load32(inter, cur));
            addr = (uint32_t)((int32_t)addr + dma->gpu_req_step);
            remaining--;
            words_done++;
        }
        dma->gpu_req_addr      = addr;
        dma->gpu_req_remaining = remaining;
        if (remaining == 0) {
            dma->gpu_req_active = false;
            return true;
        }
        return false;
    }

    return true;  /* nothing active */
}

/* Public: called by evq_handle_dma_gpu each slice tick */
void dma_gpu_resume(struct Interconnect* inter) {
    bool done = dma_gpu_run_slice(inter);
    if (done) {
        dma_ch2_signal_done(inter);
    } else {
        eventq_schedule(inter, EVQ_DMA_GPU, DMA_SLICE_CYCLES);
        LOG_DMA_TRACE("[DMA] GPU DMA slice: %u words left, rescheduled",
                      inter->dma.gpu_req_active ? inter->dma.gpu_req_remaining : 0u);
    }
}

/* Signal MDEC ch0 (input) / ch1 (output) completion IRQ. */
static void dma_mdec_signal_done(Interconnect* inter, uint32_t channel) {
    DmaChannel* ch = &inter->dma.channels[channel];
    dma_channel_done(ch);
    if (inter->dma.channel_irq_enable & (1u << channel)) {
        inter->dma.channel_irq_flags |= (1u << channel);
        inter->dma.master_irq_flag = inter->dma.force_irq ||
            (inter->dma.master_irq_enable &&
             (inter->dma.channel_irq_flags & inter->dma.channel_irq_enable) != 0);
        if (inter->dma.master_irq_flag && !(inter->irq_status & (1u << IRQ_DMA)))
            interconnect_request_irq(inter, IRQ_DMA, channel == 0 ? "DMA ch0 done" : "DMA ch1 done");
    }
    if (channel == 1) lua_debug_notify("mdec_ch1_done");
}

/* Run one slice of MDEC input (ch0) / output (ch1) DMA, each gated on MDEC's
 * own FIFO readiness rather than blasting the whole burst through unconditionally
 * — see the comment on Dma.mdec_in_active in dma.h for why this matters. */
static bool dma_mdec_run_slice(Interconnect* inter) {
    Dma* dma = &inter->dma;

    if (dma->mdec_in_active) {
        uint32_t addr = dma->mdec_in_addr;
        uint32_t remaining = dma->mdec_in_remaining;
        uint32_t words_done = 0;
        while (words_done < DMA_SLICE_WORDS && remaining > 0 && mdec_input_has_space(&inter->mdec)) {
            uint32_t cur = addr & 0x00FFFFFC;
            if (cur >= RAM_SIZE) {
                LOG_DMA_ERROR("[DMA] MDEC in: addr 0x%08x out of bounds", cur);
                remaining = 0;
                break;
            }
            mdec_dma_in(&inter->mdec, interconnect_load32(inter, cur));
            addr = (uint32_t)((int32_t)addr + dma->mdec_in_step);
            remaining--;
            words_done++;
        }
        dma->mdec_in_addr = addr;
        dma->mdec_in_remaining = remaining;
        if (remaining == 0) dma->mdec_in_active = false;
    }

    if (dma->mdec_out_active) {
        uint32_t addr = dma->mdec_out_addr;
        uint32_t remaining = dma->mdec_out_remaining;
        uint32_t words_done = 0;
        while (words_done < DMA_SLICE_WORDS && remaining > 0 && mdec_output_has_data(&inter->mdec)) {
            uint32_t cur = addr & 0x00FFFFFC;
            if (cur >= RAM_SIZE) {
                LOG_DMA_ERROR("[DMA] MDEC out: addr 0x%08x out of bounds", cur);
                remaining = 0;
                break;
            }
            interconnect_store32(inter, cur, mdec_dma_out(&inter->mdec));
            addr = (uint32_t)((int32_t)addr + dma->mdec_out_step);
            remaining--;
            words_done++;
        }
        dma->mdec_out_addr = addr;
        dma->mdec_out_remaining = remaining;
        if (remaining == 0) dma->mdec_out_active = false;

        /* If ch1 is still expecting output but none is ready, decode may be
         * stuck mid-command with buffered-but-undecoded input sitting in the
         * input FIFO (blocked on out_empty, which just became true above but
         * nothing re-entered the state machine to notice). mdec_dma_in/out
         * are the only other callers of mdec_execute(), and if ch0 already
         * finished its own word count this tick, nothing else will ever
         * nudge decode forward again — permanently stalling it. Give it one
         * more try here; safely a no-op if there isn't enough input yet. */
        if (dma->mdec_out_active && !mdec_output_has_data(&inter->mdec)) {
            mdec_execute(&inter->mdec);
        }
    }

    return !dma->mdec_in_active && !dma->mdec_out_active;
}

/* Public: called by evq_handle_mdec each slice tick */
void dma_mdec_resume(struct Interconnect* inter) {
    bool was_in  = inter->dma.mdec_in_active;
    bool was_out = inter->dma.mdec_out_active;
    bool done = dma_mdec_run_slice(inter);
    if (was_in  && !inter->dma.mdec_in_active)  dma_mdec_signal_done(inter, 0);
    if (was_out && !inter->dma.mdec_out_active) dma_mdec_signal_done(inter, 1);
    if (!done) {
        if (inter->cpu) inter->cpu->downcount -= (int32_t)DMA_SLICE_CYCLES;
        eventq_schedule(inter, EVQ_MDEC, DMA_SLICE_CYCLES);
    }
}

static uint32_t dma_get_transfer_size_words(DmaChannel* ch) {
    if (ch->sync == LINKED_LIST) return 0;

    uint32_t bs = (uint32_t)ch->block_size;
    if (ch->sync == MANUAL) {
        return (bs == 0) ? 0x10000 : bs;
    }
    uint32_t bc = (ch->block_count == 0) ? 0x10000u : (uint32_t)ch->block_count;
    bs = (bs == 0) ? 0x10000u : bs;
    return bs * bc;
}

static void interconnect_perform_dma(Interconnect* inter, uint32_t channel_index) {
    if (channel_index >= 7) {
        LOG_DMA_ERROR("[DMA] Invalid channel index %u", channel_index);
        return;
    }

    LOG_DMA_DEBUG("[DMA] ch%d start", channel_index);
    DmaChannel* ch = &inter->dma.channels[channel_index];
    DmaSync sync_mode = ch->sync;

    LOG_DMA_DEBUG("[DMA] ch%d sync=%d dir=%d base=0x%08x",
                  channel_index, sync_mode, ch->direction, ch->base_addr);

    switch (sync_mode) {
        case LINKED_LIST:
            if (channel_index == 2 && ch->direction == FROM_RAM) {
                LOG_DMA_DEBUG("[DMA] GPU LL start at 0x%08x (sliced)", ch->base_addr & 0x00FFFFFC);
                inter->dma.gpu_ll_addr   = ch->base_addr & 0x00FFFFFC;
                inter->dma.gpu_ll_active = true;
                inter->dma.gpu_req_active = false;
                dma_gpu_resume(inter);
                return;  /* done/IRQ handled by dma_gpu_resume */
            } else {
                LOG_DMA_ERROR("[DMA] Linked list on unsupported ch=%d dir=%d", channel_index, ch->direction);
            }
            break;

        case MANUAL:
        case REQUEST: {
            uint32_t words_to_transfer = dma_get_transfer_size_words(ch);
            if (words_to_transfer == 0) { LOG_DMA_WARN("[DMA] ch%d zero-size", channel_index); break; }

            uint32_t addr = ch->base_addr & 0x00FFFFFC;
            int32_t  step = (ch->step == INCREMENT) ? 4 : -4;

            /* GPU FROM_RAM: slice via EVQ to avoid stalling CPU */
            if (channel_index == 2 && ch->direction == FROM_RAM) {
                LOG_DMA_DEBUG("[DMA] GPU REQUEST/MANUAL FROM_RAM: %u words (sliced)", words_to_transfer);
                inter->dma.gpu_req_addr      = addr;
                inter->dma.gpu_req_remaining = words_to_transfer;
                inter->dma.gpu_req_step      = step;
                inter->dma.gpu_req_active    = true;
                inter->dma.gpu_ll_active     = false;
                dma_gpu_resume(inter);
                return;  /* done/IRQ handled by dma_gpu_resume */
            }

            /* MDEC input (ch0) / output (ch1): slice via EVQ, gated on MDEC's own
             * FIFO readiness — see dma_mdec_run_slice's comment for why blasting
             * the whole burst through synchronously (like the generic loop below)
             * overflows MDEC's input FIFO before ch1 ever gets a chance to drain
             * decoded output. */
            if (channel_index == 0 && ch->direction == FROM_RAM) {
                LOG_DMA_DEBUG("[DMA] MDEC ch0 FROM_RAM: %u words (sliced)", words_to_transfer);
                inter->dma.mdec_in_addr      = addr;
                inter->dma.mdec_in_remaining = words_to_transfer;
                inter->dma.mdec_in_step      = step;
                inter->dma.mdec_in_active    = true;
                dma_mdec_resume(inter);
                return;  /* done/IRQ handled by dma_mdec_resume */
            }
            if (channel_index == 1 && ch->direction == TO_RAM) {
                LOG_DMA_DEBUG("[DMA] MDEC ch1 TO_RAM: %u words (sliced)", words_to_transfer);
                inter->dma.mdec_out_addr      = addr;
                inter->dma.mdec_out_remaining = words_to_transfer;
                inter->dma.mdec_out_step      = step;
                inter->dma.mdec_out_active    = true;
                dma_mdec_resume(inter);
                return;  /* done/IRQ handled by dma_mdec_resume */
            }

            LOG_DMA_DEBUG("[DMA] ch%d %s %s step=%d addr=0x%08x words=%u",
                          channel_index,
                          ch->direction == FROM_RAM ? "FROM_RAM" : "TO_RAM",
                          sync_mode == MANUAL ? "MANUAL" : "REQUEST",
                          step, addr, words_to_transfer);

            for (uint32_t i = 0; i < words_to_transfer; ++i) {
                uint32_t cur = addr & 0x00FFFFFC;
                if (cur >= RAM_SIZE) {
                    LOG_DMA_ERROR("[DMA] ch%d addr 0x%08x out of RAM", channel_index, cur);
                    break;
                }
                if (ch->direction == FROM_RAM) {
                    uint32_t data = interconnect_load32(inter, cur);
                    switch (channel_index) {
                        case 4: {
                            uint16_t hw[2] = { (uint16_t)(data & 0xFFFF), (uint16_t)(data >> 16) };
                            spu_dma_write_halfwords(&inter->spu, inter, hw, 2);
                            break;
                        }
                        default:
                            LOG_DMA_DEBUG("[DMA] FROM_RAM ch%d unhandled addr=0x%08x", channel_index, cur);
                            break;
                    }
                } else {
                    uint32_t data = 0;
                    switch (channel_index) {
                        case 6: data = (i == words_to_transfer - 1) ? 0x00FFFFFF : ((addr - 4) & 0x00FFFFFC); break;
                        case 2: data = gpu_read_data(&inter->gpu); break;
                        case 3: data = cdrom_dma_read_word(&inter->cdrom); break;
                        case 4: {
                            uint16_t hw[2];
                            spu_dma_read_halfwords(&inter->spu, inter, hw, 2);
                            data = (uint32_t)hw[0] | ((uint32_t)hw[1] << 16);
                            break;
                        }
                        default:
                            LOG_DMA_DEBUG("[DMA] TO_RAM ch%d unhandled addr=0x%08x", channel_index, cur);
                            break;
                    }
                    interconnect_store32(inter, cur, data);
                }
                addr = (uint32_t)((int32_t)addr + step);
            }
            LOG_DMA_DEBUG("[DMA] ch%d transfer complete: %u words", channel_index, words_to_transfer);
            break;
        }

        default:
            LOG_DMA_ERROR("[DMA] Unknown sync mode %d on ch%d", sync_mode, channel_index);
            break;
    }

    dma_channel_done(ch);
    LOG_DMA_DEBUG("[DMA] ch%d done", channel_index);

    // CPU stall: account for DMA bus contention
    if (inter->cpu) {
        static const uint32_t dev_clks[7] = {1, 1, 1, 40, 4, 20, 1};
        uint32_t rate = dev_clks[channel_index < 7 ? channel_index : 0];
        uint32_t words = (sync_mode == LINKED_LIST)
                       ? ((ch->block_size > 0) ? ch->block_size : 64)
                       : dma_get_transfer_size_words(ch);
        if (words == 0) words = 1;
        uint32_t stall = words * rate + ((words + 15u) / 16u) * 17u;
        inter->cpu->downcount -= (int32_t)stall;
    }

    // DMA completion IRQ (IRQ3)
    if (inter->dma.channel_irq_enable & (1u << channel_index)) {
        inter->dma.channel_irq_flags |= (1u << channel_index);
        inter->dma.master_irq_flag = inter->dma.force_irq ||
            (inter->dma.master_irq_enable &&
             (inter->dma.channel_irq_flags & inter->dma.channel_irq_enable) != 0);
        if (inter->dma.master_irq_flag && !(inter->irq_status & (1u << IRQ_DMA)))
            interconnect_request_irq(inter, IRQ_DMA, "DMA channel done");
    }
}

