#ifndef DMA_H
#define DMA_H

#include <stdint.h>
#include <stdbool.h> // Include if using bool

// --- Enums for Channel Control (CHCR - Section 3.3) ---
typedef enum {
    TO_RAM = 0,    // Peripheral to RAM
    FROM_RAM = 1   // RAM to Peripheral
} DmaDirection;

typedef enum {
    INCREMENT = 0,
    DECREMENT = 1
} DmaStep;

typedef enum {
    MANUAL = 0,      // Start transfer via CHCR Trigger bit
    REQUEST = 1,     // Sync blocks to DRQ signals
    LINKED_LIST = 2  // Used for GPU command lists
} DmaSync;

struct Interconnect; // Forward declaration

// --- Structure for a single DMA Channel ---
typedef struct {
    // CHCR - Channel Control Register (Offset 0xX8)
    bool enable;                 // Bit 24: Channel Enable
    DmaDirection direction;      // Bit 0: Transfer Direction
    DmaStep step;                // Bit 1: Address Step (Inc/Dec)
    bool chopping;               // Bit 8: Chopping Enable
    DmaSync sync;                // Bits 9-10: Sync Mode (Manual, Request, Linked List)
    bool trigger;                // Bit 28: Manual Trigger (for Manual Sync)
    uint8_t chopping_dma_sz;    // Bits 16-18: DMA window size exponent (1 << n words)
    uint8_t chopping_cpu_sz;    // Bits 20-22: CPU window size exponent (1 << n cycles)

    // MADR - Base Address Register (Offset 0xX0)
    uint32_t base_addr;          // Effective address (lower 24 bits)

    // BCR - Block Control Register (Offset 0xX4)
    uint16_t block_size;         // BC/BA field (Word count or block size)
    uint16_t block_count;        // BS field (Block count for Request sync)

} DmaChannel;


// --- Main DMA State Structure ---
typedef struct {
    // DPCR - DMA Control Register (Offset 0x70)
    uint32_t control;

    // DICR - DMA Interrupt Register (Offset 0x74)
    bool force_irq;
    uint8_t channel_irq_enable;
    bool master_irq_enable;
    uint8_t channel_irq_flags;
    bool master_irq_flag;
    uint8_t dicr_unknown_rw;

    // Array of 7 DMA Channels
    DmaChannel channels[7];

    struct Interconnect* inter; // Pointer to Interconnect for event scheduling

    /* GPU DMA slice state (Phase 3 threading) */
    uint32_t gpu_ll_addr;       /* current LL node address between slices */
    bool     gpu_ll_active;     /* true: GPU linked-list transfer in progress */
    uint32_t gpu_req_addr;      /* current word address for REQUEST/MANUAL GPU DMA */
    uint32_t gpu_req_remaining; /* words remaining in REQUEST/MANUAL GPU transfer */
    int32_t  gpu_req_step;      /* address step: +4 or -4 */
    bool     gpu_req_active;    /* true: GPU REQUEST/MANUAL transfer in progress */

    /* MDEC DMA slice state (ch0 = RAM->MDEC input, ch1 = MDEC->RAM output).
     * Sliced the same way as GPU above: real hardware runs both channels as
     * background transfers that interleave with each other and with the CPU,
     * so MDEC's own decode-one-macroblock-then-wait-for-drain pacing (see
     * mdec.c) can proceed instead of the whole ch0 burst overflowing MDEC's
     * input FIFO before ch1 ever gets a chance to drain any output. */
    uint32_t mdec_in_addr;      uint32_t mdec_in_remaining;
    int32_t  mdec_in_step;      bool     mdec_in_active;
    uint32_t mdec_out_addr;     uint32_t mdec_out_remaining;
    int32_t  mdec_out_step;     bool     mdec_out_active;

} Dma;

// --- Function Prototypes ---
void dma_init(Dma* dma, struct Interconnect* inter);
uint32_t dma_read(Dma* dma, uint32_t offset);
bool dma_write(Dma* dma, uint32_t offset, uint32_t value);
bool dma_channel_is_active(DmaChannel* ch);
void dma_channel_done(DmaChannel* ch);

uint32_t channel_get_control(DmaChannel* ch);
void channel_set_control(DmaChannel* ch, uint32_t value);

/* GPU DMA slice resume — called by evq_handle_dma_gpu each slice */
void dma_gpu_resume(struct Interconnect* inter);

/* MDEC DMA slice resume — called by evq_handle_mdec each slice */
void dma_mdec_resume(struct Interconnect* inter);


#endif // DMA_H