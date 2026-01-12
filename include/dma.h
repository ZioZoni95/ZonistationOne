#ifndef DMA_H
#define DMA_H

#include <stdint.h>
#include <stdbool.h> // Include if using bool

// Enums from DuckStation
typedef enum {
    DMA_CHANNEL_MDECIN = 0,
    DMA_CHANNEL_MDECOUT = 1,
    DMA_CHANNEL_GPU = 2,
    DMA_CHANNEL_CDROM = 3,
    DMA_CHANNEL_SPU = 4,
    DMA_CHANNEL_PIO = 5,
    DMA_CHANNEL_OTC = 6,
    DMA_NUM_CHANNELS = 7
} DmaChannelIndex;

typedef enum {
    DMA_SYNC_MANUAL = 0,
    DMA_SYNC_REQUEST = 1,
    DMA_SYNC_LINKED_LIST = 2,
    DMA_SYNC_RESERVED = 3
} DmaSyncMode;

typedef enum {
    DMA_DIRECTION_TO_RAM = 0,
    DMA_DIRECTION_FROM_RAM = 1
} DmaDirection;

typedef enum {
    DMA_STEP_INCREMENT = 0,
    DMA_STEP_DECREMENT = 1
} DmaStep;

// Updated DmaChannel struct to match DuckStation's ChannelState
typedef struct {
    uint32_t base_addr;
    union {
        uint32_t bits;
        struct {
            uint32_t word_count : 16;
        } manual;
        struct {
            uint32_t block_size : 16;
            uint32_t block_count : 16;
        } request;
    } block_control;
    union {
        uint32_t bits;
        struct {
            uint32_t copy_to_device : 1;  // 0: to RAM, 1: from RAM
            uint32_t address_step_reverse : 1;
            uint32_t chopping_enable : 1;
            uint32_t sync_mode : 2;
            uint32_t chopping_dma_window_size : 3;
            uint32_t chopping_cpu_window_size : 3;
            uint32_t enable_busy : 1;
            uint32_t start_trigger : 1;
        };
    } channel_control;
    bool request;
} DmaChannel;

// DPCR register (DMA Priority Control)
typedef union {
    uint32_t bits;
    struct {
        uint8_t mdecin_priority : 3;
        uint8_t mdecin_master_enable : 1;
        uint8_t mdecout_priority : 3;
        uint8_t mdecout_master_enable : 1;
        uint8_t gpu_priority : 3;
        uint8_t gpu_master_enable : 1;
        uint8_t cdrom_priority : 3;
        uint8_t cdrom_master_enable : 1;
        uint8_t spu_priority : 3;
        uint8_t spu_master_enable : 1;
        uint8_t pio_priority : 3;
        uint8_t pio_master_enable : 1;
        uint8_t otc_priority : 3;
        uint8_t otc_master_enable : 1;
        uint8_t priority_offset : 3;
        uint8_t unused : 1;
    };
} DmaDPCR;

// DICR register (DMA Interrupt Control)
typedef union {
    uint32_t bits;
    struct {
        uint8_t dicr_unknown_rw : 6;
        uint8_t force_irq : 1;
        uint8_t channel_irq_enable : 7;
        uint8_t master_irq_enable : 1;
        uint8_t channel_irq_flags : 7;
        uint8_t master_irq_flag : 1;
    };
} DmaDICR;

// --- Main DMA State Structure ---
typedef struct {
    struct Interconnect* inter;
    DmaDPCR control;  // DPCR
    DmaDICR dicr;     // DICR
    DmaChannel channels[DMA_NUM_CHANNELS];
} Dma;

// --- Function Prototypes ---
void dma_init(Dma* dma, struct Interconnect* inter);
uint32_t dma_read(Dma* dma, uint32_t offset);
// Return bool to indicate if a channel became active
bool dma_write(Dma* dma, uint32_t offset, uint32_t value); // <-- Return type changed here
bool dma_channel_is_active(DmaChannel* ch);
void dma_channel_done(DmaChannel* ch);

// Helper to get channel control register value
uint32_t channel_get_control(DmaChannel* ch); // <-- Declaration added
// Helper to set channel control register value
void channel_set_control(DmaChannel* ch, uint32_t value); // <-- Declaration added

// New functions for DuckStation-style implementation
void dma_set_request(Dma* dma, DmaChannelIndex channel, bool request);
bool dma_can_transfer_channel(Dma* dma, DmaChannelIndex channel);
void dma_update_irq(Dma* dma);
bool dma_transfer_channel(Dma* dma, DmaChannelIndex channel);

#endif // DMA_H