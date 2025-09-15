#include "mdec.h"
#include "log.h"
#include <string.h>

/**
 * @brief Initialize the MDEC (Motion Decoder)
 * Based on PSX-SPX specifications: https://psx-spx.consoledev.net/mdecdecompressionunit/
 */
void mdec_init(Mdec* mdec) {
    memset(mdec, 0, sizeof(Mdec));
    
    // Initialize to power-on state
    mdec->enabled = false;
    mdec->dma_in_enabled = false;
    mdec->dma_out_enabled = false;
    mdec->busy = false;
    
    // Default output format (based on PSX-SPX)
    mdec->output_24bit = false;     // Default to 16-bit output
    mdec->output_signed = false;    // Default to unsigned
    mdec->output_bit15_set = false; // Default bit 15 handling
    
    // Initialize status register
    mdec->status = MDEC_STATUS_DATA_OUT_FIFO_EMPTY | MDEC_STATUS_DATA_IN_FIFO_FULL;
    mdec->control = 0;
    
    // Clear FIFOs
    mdec->data_in_count = 0;
    mdec->data_out_count = 0;
    mdec->data_in_pos = 0;
    mdec->data_out_pos = 0;
    
    // Initialize default quantization tables (standard JPEG tables)
    // Luminance quantization table
    static const uint8_t default_quant_y[MDEC_QUANT_TABLE_SIZE] = {
        16, 11, 10, 16,  24,  40,  51,  61,
        12, 12, 14, 19,  26,  58,  60,  55,
        14, 13, 16, 24,  40,  57,  69,  56,
        14, 17, 22, 29,  51,  87,  80,  62,
        18, 22, 37, 56,  68, 109, 103,  77,
        24, 35, 55, 64,  81, 104, 113,  92,
        49, 64, 78, 87, 103, 121, 120, 101,
        72, 92, 95, 98, 112, 100, 103,  99
    };
    
    // Chrominance quantization table
    static const uint8_t default_quant_c[MDEC_QUANT_TABLE_SIZE] = {
        17, 18, 24, 47, 99, 99, 99, 99,
        18, 21, 26, 66, 99, 99, 99, 99,
        24, 26, 56, 99, 99, 99, 99, 99,
        47, 66, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99,
        99, 99, 99, 99, 99, 99, 99, 99
    };
    
    memcpy(mdec->quant_table_y, default_quant_y, MDEC_QUANT_TABLE_SIZE);
    memcpy(mdec->quant_table_c, default_quant_c, MDEC_QUANT_TABLE_SIZE);
    
    // Initialize default scale table (standard IDCT coefficients)
    for (int i = 0; i < MDEC_SCALE_TABLE_SIZE; i++) {
        mdec->scale_table[i] = 0x2000; // Default scale factor
    }
    
    // Clear statistics
    mdec->blocks_decoded = 0;
    mdec->commands_processed = 0;
    
    LOG_INFO("MDEC initialized (Motion Decoder with FIFOs, stubbed implementation)");
}

/**
 * @brief Reset MDEC to power-on state
 */
void mdec_reset(Mdec* mdec) {
    LOG_INFO("MDEC reset requested");
    mdec_init(mdec); // Reset is same as init for now
}

/**
 * @brief Read 32-bit value from MDEC register
 */
uint32_t mdec_load32(Mdec* mdec, uint32_t offset) {
    switch (offset) {
        case 0x0: // MDEC_DATA_OUT - Read output FIFO
            if (mdec_data_out_fifo_empty(mdec)) {
                LOG_WARN("MDEC: Attempt to read from empty output FIFO");
                return 0x00000000;
            }
            return mdec_pop_data_out(mdec);
            
        case 0x4: // MDEC_STATUS - Read status register
            // Update status flags based on current state
            mdec->status = 0;
            if (mdec_data_out_fifo_empty(mdec)) mdec->status |= MDEC_STATUS_DATA_OUT_FIFO_EMPTY;
            if (mdec_data_in_fifo_full(mdec)) mdec->status |= MDEC_STATUS_DATA_IN_FIFO_FULL;
            if (mdec->busy) mdec->status |= MDEC_STATUS_CMD_BUSY;
            if (!mdec_data_in_fifo_full(mdec)) mdec->status |= MDEC_STATUS_DATA_IN_REQUEST;
            if (!mdec_data_out_fifo_empty(mdec)) mdec->status |= MDEC_STATUS_DATA_OUT_REQUEST;
            
            LOG_TRACE("MDEC: Status read = 0x%08X", mdec->status);
            return mdec->status;
            
        default:
            LOG_WARN("MDEC: Unhandled read32 from offset 0x%X", offset);
            return 0;
    }
}

/**
 * @brief Write 32-bit value to MDEC register
 */
void mdec_store32(Mdec* mdec, uint32_t offset, uint32_t value) {
    switch (offset) {
        case 0x0: // MDEC_CMD - Command/Data input
            if ((value >> 29) != 0) {
                // This is a command (top 3 bits indicate command)
                mdec_execute_command(mdec, value);
            } else {
                // This is data for the current command
                if (mdec_data_in_fifo_full(mdec)) {
                    LOG_WARN("MDEC: Attempt to write to full input FIFO");
                    return;
                }
                mdec_push_data_in(mdec, value);
            }
            break;
            
        case 0x4: // MDEC_CTRL - Control register
            LOG_TRACE("MDEC: Control write = 0x%08X", value);
            
            if (value & MDEC_CTRL_RESET) {
                mdec_reset(mdec);
                return;
            }
            
            mdec->control = value;
            mdec->dma_in_enabled = (value & MDEC_CTRL_ENABLE_DMA_IN) != 0;
            mdec->dma_out_enabled = (value & MDEC_CTRL_ENABLE_DMA_OUT) != 0;
            mdec->output_24bit = (value & MDEC_CTRL_OUTPUT_DEPTH_24BIT) != 0;
            mdec->output_signed = (value & MDEC_CTRL_OUTPUT_SIGNED) != 0;
            mdec->output_bit15_set = (value & MDEC_CTRL_OUTPUT_BIT15_SET) != 0;
            
            LOG_TRACE("MDEC: DMA_IN=%d, DMA_OUT=%d, 24bit=%d, signed=%d", 
                      mdec->dma_in_enabled, mdec->dma_out_enabled, 
                      mdec->output_24bit, mdec->output_signed);
            break;
            
        default:
            LOG_WARN("MDEC: Unhandled write32 to offset 0x%X = 0x%08X", offset, value);
            break;
    }
}

/**
 * @brief Execute MDEC command
 */
void mdec_execute_command(Mdec* mdec, uint32_t cmd) {
    uint8_t command = (cmd >> 29) & 0x7;
    uint32_t param = cmd & 0x1FFFFFFF;
    
    LOG_TRACE("MDEC: Command 0x%X with parameter 0x%08X", command, param);
    
    mdec->current_cmd = command;
    mdec->cmd_param = param;
    mdec->busy = true;
    mdec->commands_processed++;
    
    switch (command) {
        case MDEC_CMD_DECODE_MACROBLOCK:
            LOG_TRACE("MDEC: Decode macroblock command (stub)");
            mdec->cmd_remaining = param & 0xFFFF; // Number of blocks
            break;
            
        case MDEC_CMD_SET_QUANT_TABLE:
            LOG_TRACE("MDEC: Set quantization table command (stub)");
            mdec->cmd_remaining = 64; // 64 words for quant table
            break;
            
        case MDEC_CMD_SET_SCALE_TABLE:
            LOG_TRACE("MDEC: Set scale table command (stub)");
            mdec->cmd_remaining = 64; // 64 words for scale table
            break;
            
        case MDEC_CMD_NOP:
            LOG_TRACE("MDEC: NOP command");
            mdec->busy = false; // NOP completes immediately
            break;
            
        default:
            LOG_WARN("MDEC: Unknown command 0x%X", command);
            mdec->busy = false;
            break;
    }
}

// FIFO Management Functions
bool mdec_data_in_fifo_full(Mdec* mdec) {
    return mdec->data_in_count >= MDEC_DATA_IN_FIFO_SIZE;
}

bool mdec_data_in_fifo_empty(Mdec* mdec) {
    return mdec->data_in_count == 0;
}

bool mdec_data_out_fifo_full(Mdec* mdec) {
    return mdec->data_out_count >= MDEC_DATA_OUT_FIFO_SIZE;
}

bool mdec_data_out_fifo_empty(Mdec* mdec) {
    return mdec->data_out_count == 0;
}

void mdec_push_data_in(Mdec* mdec, uint32_t data) {
    if (mdec_data_in_fifo_full(mdec)) return;
    
    int write_pos = (mdec->data_in_pos + mdec->data_in_count) % MDEC_DATA_IN_FIFO_SIZE;
    mdec->data_in_fifo[write_pos] = data;
    mdec->data_in_count++;
    
    LOG_TRACE("MDEC: Data input 0x%08X (FIFO: %d/%d)", data, mdec->data_in_count, MDEC_DATA_IN_FIFO_SIZE);
}

uint32_t mdec_pop_data_out(Mdec* mdec) {
    if (mdec_data_out_fifo_empty(mdec)) return 0;
    
    uint32_t data = mdec->data_out_fifo[mdec->data_out_pos];
    mdec->data_out_pos = (mdec->data_out_pos + 1) % MDEC_DATA_OUT_FIFO_SIZE;
    mdec->data_out_count--;
    
    LOG_TRACE("MDEC: Data output 0x%08X (FIFO: %d/%d)", data, mdec->data_out_count, MDEC_DATA_OUT_FIFO_SIZE);
    return data;
}

// DMA Interface Functions (stubs)
void mdec_dma_in_write(Mdec* mdec, uint32_t* data, uint32_t word_count) {
    LOG_WARN("MDEC: DMA input write of %u words (stub)", word_count);
    // TODO: Process input data for decoding
    (void)data; (void)mdec; // Suppress unused warnings
}

void mdec_dma_out_read(Mdec* mdec, uint32_t* data, uint32_t word_count) {
    LOG_WARN("MDEC: DMA output read of %u words (stub)", word_count);
    // TODO: Provide decoded output data
    memset(data, 0, word_count * sizeof(uint32_t)); // Return zeros for now
    (void)mdec; // Suppress unused warning
}

// Block Processing Functions (stubs)
void mdec_decode_block(Mdec* mdec, int16_t* input, uint16_t* output) {
    LOG_TRACE("MDEC: Block decode (stub)");
    // TODO: Implement block decoding (IDCT, dequantization, YUV->RGB)
    memset(output, 0, MDEC_BLOCK_SIZE * sizeof(uint16_t));
    (void)mdec; (void)input; // Suppress unused warnings
}

void mdec_inverse_dct(int16_t* block) {
    // TODO: Implement Inverse Discrete Cosine Transform
    (void)block; // Suppress unused warning
}

void mdec_yuv_to_rgb(uint8_t y, uint8_t u, uint8_t v, uint8_t* r, uint8_t* g, uint8_t* b) {
    // TODO: Implement YUV to RGB color conversion
    // For now, just pass through Y as grayscale
    *r = *g = *b = y;
    (void)u; (void)v; // Suppress unused warnings
}

// Update and Status Functions
void mdec_update(Mdec* mdec) {
    // TODO: Process pending commands and data
    if (mdec->busy && mdec->cmd_remaining == 0) {
        mdec->busy = false; // Mark command as complete
    }
    (void)mdec; // Suppress unused warning for now
}

bool mdec_is_busy(Mdec* mdec) {
    return mdec->busy;
}

bool mdec_data_available(Mdec* mdec) {
    return !mdec_data_out_fifo_empty(mdec);
}