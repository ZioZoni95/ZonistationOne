#include "dma.h"
#include <stdio.h> // For fprintf, stderr
#include "log.h"
// DuckStation-style: No event scheduler (removed)
#include "interconnect.h"   // For Interconnect struct
#include "irq/irq_core.h"   // For IRQ module functions
#include "gpu/gpu_core.h"   // For GPU DMA functions
#include "spu.h"            // For SPU (placeholders)
#include "cdrom/cdrom_core.h" // For CDROM (placeholders)
#include <string.h>         // For memset
// #include "mdec.h"           // For MDEC (placeholders)

// Logging: Only use LOG_DMA_ERROR for DMA hardware faults. No per-transfer logs.

// Example: Replace LOG_DMA_INFO or LOG_DMA_DEBUG for frequent register accesses with LOG_DMA_TRACE or wrap in a higher debug level check.
#ifdef LOG_DMA_TRACE
#define LOG_DMA_TRACE_ENABLED 1
#else
#define LOG_DMA_TRACE_ENABLED 0
#endif

// Constants
#define DMA_BASE_ADDRESS_MASK 0x00FFFFFF
#define DMA_TRANSFER_ADDRESS_MASK 0x00FFFFFC
#define DMA_LINKED_LIST_TERMINATOR 0x00FFFFFF

// Helper function to get channel control register value
// REMOVED 'static'
uint32_t channel_get_control(DmaChannel* ch) {
    return ch->channel_control.bits;
}

// Helper function to set channel control register value
// REMOVED 'static'
void channel_set_control(DmaChannel* ch, uint32_t value) {
    ch->channel_control.bits = value;
}

// Checks if a channel should start transferring based on its state.
bool dma_channel_is_active(DmaChannel* ch) {
    if (!ch->channel_control.enable_busy) {
        return false;
    }
    if (ch->channel_control.sync_mode == DMA_SYNC_MANUAL) {
        return ch->channel_control.start_trigger;
    }
    return true;
}

// Marks a channel as finished after a transfer.
void dma_channel_done(DmaChannel* ch) {
    ch->channel_control.enable_busy = false;
    ch->channel_control.start_trigger = false;
}

// Helper: Estimate cycles for a DMA transfer (very rough, tune as needed)
static uint32_t estimate_dma_cycles(DmaChannel* ch) {
    // PS1 DMA is fast, but not instant. Use 2 cycles per word as a starting point.
    uint32_t words;
    if (ch->channel_control.sync_mode == DMA_SYNC_MANUAL) {
        words = ch->block_control.manual.word_count;
        if (words == 0) words = 0x10000;
    } else if (ch->channel_control.sync_mode == DMA_SYNC_REQUEST) {
        uint32_t bc = ch->block_control.request.block_count;
        uint32_t bs = ch->block_control.request.block_size;
        words = (bc == 0 ? 1 : bc) * (bs == 0 ? 1 : bs);
    } else {
        words = 1; // Linked list, estimate 1 for now
    }
    if (words == 0) words = 1;
    return words * 2; // 2 cycles per word (tune as needed)
}

// Initializes the DMA state to reset values.
void dma_init(Dma* dma, struct Interconnect* inter) {
    LOG_DMA_DEBUG("DMA initialized");
    dma->inter = inter; // Store pointer to Interconnect
    // DPCR reset value
    dma->control.bits = 0x07654321;

    // Initialize DICR fields
    dma->dicr.bits = 0;

    // Initialize all channels to default values
    for (int i = 0; i < DMA_NUM_CHANNELS; ++i) {
        dma->channels[i].base_addr = 0;
        dma->channels[i].block_control.bits = 0;
        dma->channels[i].channel_control.bits = 0;
        dma->channels[i].request = false;
    }

    printf("DMA Initialized. DPCR=0x%08x, Channels initialized.\n", dma->control.bits);
}

// Reads a 32-bit value from a DMA register address (relative offset).
uint32_t dma_read(Dma* dma, uint32_t offset) {
    uint32_t channel_index = (offset >> 4) & 0x7;
    uint32_t register_offset = offset & 0xF;

    if (channel_index < DMA_NUM_CHANNELS) { // Channel Register Access
        DmaChannel* ch = &dma->channels[channel_index];
        switch (register_offset) {
            case 0x0: // MADR
                return ch->base_addr;
            case 0x4: // BCR
                return ch->block_control.bits;
            case 0x8: // CHCR
                return channel_get_control(ch);
            default:
                LOG_DMA_WARN("Warning: Unhandled DMA Channel read at offset 0x%x (Channel %d, Reg %x)\n", offset, channel_index, register_offset);
                return 0;
        }
    } else { // Main DMA Register Access
        switch (offset) {
            case 0x70: // DPCR
                return dma->control.bits;
            case 0x74: // DICR
                return dma->dicr.bits;
            default:
                LOG_DMA_ERROR("Error: Unhandled DMA Main register read at offset 0x%x\n", offset);
                return 0;
        }
    }
}

// Writes a 32-bit value to a DMA register address (relative offset).
// Returns true if the write made a channel active, false otherwise.
bool dma_write(Dma* dma, uint32_t offset, uint32_t value) {
    uint32_t channel_index = (offset >> 4) & 0x7;
    uint32_t register_offset = offset & 0xF;
    bool channel_became_active = false;

    if (channel_index < DMA_NUM_CHANNELS) { // Channel Register Access
        DmaChannel* ch = &dma->channels[channel_index];
        switch (register_offset) {
            case 0x0: // MADR
                ch->base_addr = value & DMA_BASE_ADDRESS_MASK;
                break;
            case 0x4: // BCR
                ch->block_control.bits = value;
                break;
            case 0x8: // CHCR
                channel_set_control(ch, value);
                channel_became_active = dma_channel_is_active(ch);
                if (channel_became_active) {
                    // Rate-limit DMA activation logs
                    static uint32_t dma_activate_count = 0;
                    dma_activate_count++;
                    if (dma_activate_count <= 10 || dma_activate_count % 100 == 0) {
                        LOG_DMA_DEBUG("[DMA] Channel %d activated #%u", channel_index, dma_activate_count);
                    }
                    // Start transfer
                    dma_transfer_channel(dma, (DmaChannelIndex)channel_index);
                }
                break;
            default:
                if (log_get_level() >= LOG_LEVEL_WARN) {
                    LOG_DMA_WARN("Warning: Unhandled DMA Channel write at offset 0x%x = 0x%08x (Channel %d, Reg %x)", offset, value, channel_index, register_offset);
                }
                break;
        }
    } else { // Main DMA Register Access
         switch (offset) {
            case 0x70: // DPCR
                dma->control.bits = value;
                // Check if any channels can now transfer
                for (int i = 0; i < DMA_NUM_CHANNELS; i++) {
                    if (dma_can_transfer_channel(dma, (DmaChannelIndex)i)) {
                        dma_transfer_channel(dma, (DmaChannelIndex)i);
                    }
                }
                break;
            case 0x74: // DICR
                dma->dicr.bits = (dma->dicr.bits & ~0x00FFFFFF) | (value & 0x00FFFFFF);
                dma->dicr.bits = dma->dicr.bits & ~(value & 0x7F000000);
                dma_update_irq(dma);
                // Acknowledge logic
                uint8_t ack_flags = (uint8_t)((value >> 24) & 0x7F);
                if (ack_flags) {
                    dma->dicr.channel_irq_flags &= ~ack_flags;
                    dma_update_irq(dma);
                }
                break;
            default:
                LOG_DMA_ERROR("Error: Unhandled DMA Main register write at offset 0x%x = 0x%08x", offset, value);
                break;
        }
    }

    // For frequent DMA region accesses, only log at TRACE level:
    #if LOG_DMA_TRACE_ENABLED
        LOG_DMA_TRACE("Write32 to DMA region: Addr=0x%08x Offset=0x%02x = 0x%08x", offset, register_offset, value);
    #endif

    return channel_became_active;
}

// At the end of interconnect_perform_dma (or equivalent), after marking the channel as done:
// if (dma->channel_irq_enable & (1 << channel_index)) {
//     interconnect_request_irq(dma->inter, IRQ_DMA, "DMA");
// }

// New functions for DuckStation-style implementation
void dma_set_request(Dma* dma, DmaChannelIndex channel, bool request) {
    DmaChannel* cs = &dma->channels[channel];
    if (cs->request == request) return;
    cs->request = request;
    if (dma_can_transfer_channel(dma, channel)) {
        dma_transfer_channel(dma, channel);
    }
}

bool dma_can_transfer_channel(Dma* dma, DmaChannelIndex channel) {
    DmaChannel* cs = &dma->channels[channel];
    if (!dma->control.bits & (1 << (channel * 4 + 3))) return false; // Master enable
    if (!cs->channel_control.enable_busy) return false;
    if (cs->channel_control.sync_mode != DMA_SYNC_MANUAL) return false; // Simplified, no halt
    return cs->request;
}

void dma_update_irq(Dma* dma) {
    dma->dicr.master_irq_flag = (dma->dicr.force_irq ||
                                 (dma->dicr.master_irq_enable && (dma->dicr.channel_irq_flags & dma->dicr.channel_irq_enable)));
    if (dma->dicr.master_irq_flag) {
        interconnect_request_irq(dma->inter, IRQ_DMA, "DMA");
    }
}

static uint32_t dma_get_word_count_manual(DmaChannel* cs) {
    uint32_t wc = cs->block_control.manual.word_count;
    return (wc == 0) ? 0x10000 : wc;
}

static uint32_t dma_get_block_size(DmaChannel* cs) {
    uint32_t bs = cs->block_control.request.block_size;
    return (bs == 0) ? 0x10000 : bs;
}

static uint32_t dma_get_block_count(DmaChannel* cs) {
    uint32_t bc = cs->block_control.request.block_count;
    return (bc == 0) ? 0x10000 : bc;
}

static bool dma_is_linked_list_terminator(uint32_t address) {
    return (address & DMA_LINKED_LIST_TERMINATOR) == DMA_LINKED_LIST_TERMINATOR;
}

static void dma_complete_transfer(Dma* dma, DmaChannelIndex channel, DmaChannel* cs) {
    cs->channel_control.enable_busy = false;
    if ((dma->dicr.channel_irq_enable & (1 << (uint32_t)channel)) &&
        dma->dicr.master_irq_enable) {
        dma->dicr.channel_irq_flags |= (1 << (uint32_t)channel);
        dma_update_irq(dma);
    }
}

// Placeholder DMA functions for devices not implemented
static void dma_spu_write(const uint32_t* words, uint32_t word_count) {
    // Placeholder
}

static void dma_spu_read(uint32_t* words, uint32_t word_count) {
    // Placeholder
    memset(words, 0, word_count * sizeof(uint32_t));
}

static void dma_cdrom_write(const uint32_t* words, uint32_t word_count) {
    // Placeholder
}

static void dma_cdrom_read(uint32_t* words, uint32_t word_count) {
    // Placeholder
    memset(words, 0, word_count * sizeof(uint32_t));
}

static void dma_mdecin_write(const uint32_t* words, uint32_t word_count) {
    // Placeholder
}

static void dma_mdecout_read(uint32_t* words, uint32_t word_count) {
    // Placeholder
    memset(words, 0, word_count * sizeof(uint32_t));
}

bool dma_transfer_channel(Dma* dma, DmaChannelIndex channel) {
    DmaChannel* cs = &dma->channels[channel];
    bool copy_to_device = cs->channel_control.copy_to_device;
    cs->channel_control.start_trigger = false;

    uint32_t current_address = cs->base_addr;
    uint32_t increment = cs->channel_control.address_step_reverse ? (uint32_t)-4 : 4;

    switch (cs->channel_control.sync_mode) {
        case DMA_SYNC_MANUAL: {
            uint32_t word_count = dma_get_word_count_manual(cs);
            uint32_t transfer_addr = current_address & DMA_TRANSFER_ADDRESS_MASK;

            if (copy_to_device) {
                // Memory to device
                switch (channel) {
                    case DMA_CHANNEL_GPU:
                        // Assume RAM is accessible via interconnect
                        for (uint32_t i = 0; i < word_count; i++) {
                            uint32_t value = interconnect_load32(dma->inter, transfer_addr);
                            gpu_dma_write(&dma->inter->gpu, value);
                            transfer_addr += increment;
                        }
                        gpu_end_dma_write(&dma->inter->gpu);
                        break;
                    case DMA_CHANNEL_SPU:
                        // Simplified
                        for (uint32_t i = 0; i < word_count; i++) {
                            uint32_t value = interconnect_load32(dma->inter, transfer_addr);
                            dma_spu_write(&value, 1);
                            transfer_addr += increment;
                        }
                        break;
                    case DMA_CHANNEL_CDROM:
                        for (uint32_t i = 0; i < word_count; i++) {
                            uint32_t value = interconnect_load32(dma->inter, transfer_addr);
                            dma_cdrom_write(&value, 1);
                            transfer_addr += increment;
                        }
                        break;
                    case DMA_CHANNEL_MDECIN:
                        for (uint32_t i = 0; i < word_count; i++) {
                            uint32_t value = interconnect_load32(dma->inter, transfer_addr);
                            dma_mdecin_write(&value, 1);
                            transfer_addr += increment;
                        }
                        break;
                    default:
                        break;
                }
            } else {
                // Device to memory
                switch (channel) {
                    case DMA_CHANNEL_GPU:
                        gpu_dma_read(&dma->inter->gpu, NULL, word_count); // Simplified
                        break;
                    case DMA_CHANNEL_CDROM:
                        {
                            uint32_t words[1];
                            dma_cdrom_read(words, 1);
                            interconnect_store32(dma->inter, transfer_addr, words[0]);
                        }
                        break;
                    case DMA_CHANNEL_SPU:
                        {
                            uint32_t words[1];
                            dma_spu_read(words, 1);
                            interconnect_store32(dma->inter, transfer_addr, words[0]);
                        }
                        break;
                    case DMA_CHANNEL_MDECOUT:
                        {
                            uint32_t words[1];
                            dma_mdecout_read(words, 1);
                            interconnect_store32(dma->inter, transfer_addr, words[0]);
                        }
                        break;
                    case DMA_CHANNEL_OTC:
                        // Clear ordering table
                        for (uint32_t i = 0; i < word_count - 1; i++) {
                            uint32_t next = (transfer_addr - 4) & 0x1FFFFC;
                            interconnect_store32(dma->inter, transfer_addr, next);
                            transfer_addr = next;
                        }
                        interconnect_store32(dma->inter, transfer_addr, 0xFFFFFF);
                        break;
                    default:
                        break;
                }
            }
            dma_complete_transfer(dma, channel, cs);
            return true;
        }
        case DMA_SYNC_REQUEST: {
            // Simplified, no slicing
            uint32_t block_size = dma_get_block_size(cs);
            uint32_t blocks_remaining = dma_get_block_count(cs);
            for (uint32_t b = 0; b < blocks_remaining; b++) {
                uint32_t transfer_addr = current_address & DMA_TRANSFER_ADDRESS_MASK;
                if (copy_to_device) {
                    switch (channel) {
                        case DMA_CHANNEL_GPU:
                            for (uint32_t i = 0; i < block_size; i++) {
                                uint32_t value = interconnect_load32(dma->inter, transfer_addr);
                                gpu_dma_write(&dma->inter->gpu, value);
                                transfer_addr += increment;
                            }
                            break;
                        default:
                            break;
                    }
                } else {
                    switch (channel) {
                        case DMA_CHANNEL_GPU:
                            gpu_dma_read(&dma->inter->gpu, NULL, block_size);
                            break;
                        default:
                            break;
                    }
                }
                current_address = transfer_addr;
            }
            cs->base_addr = current_address;
            cs->block_control.request.block_count = 0;
            dma_complete_transfer(dma, channel, cs);
            return true;
        }
        case DMA_SYNC_LINKED_LIST: {
            if (!copy_to_device) return true; // Not implemented
            while (cs->request) {
                uint32_t header = interconnect_load32(dma->inter, current_address & DMA_TRANSFER_ADDRESS_MASK);
                uint32_t word_count = header >> 24;
                uint32_t next_address = header & 0x00FFFFFF;
                uint32_t transfer_addr = (current_address & DMA_TRANSFER_ADDRESS_MASK) + 4;
                for (uint32_t i = 0; i < word_count; i++) {
                    uint32_t value = interconnect_load32(dma->inter, transfer_addr);
                    gpu_dma_write(&dma->inter->gpu, value);
                    transfer_addr += 4;
                }
                current_address = next_address;
                if (dma_is_linked_list_terminator(current_address)) {
                    cs->base_addr = DMA_LINKED_LIST_TERMINATOR;
                    dma_complete_transfer(dma, channel, cs);
                    return true;
                }
            }
            cs->base_addr = current_address;
            return true;
        }
        default:
            return true;
    }
}