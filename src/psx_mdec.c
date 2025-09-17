#include "../include/psx_mdec.h"
#include <stdio.h>
#include <string.h>

// PSX-SPX: MDEC (Motion Decoder) implementation (skeleton)
static psx_mdec_t mdec;

void mdec_init(void) {
    memset(&mdec, 0, sizeof(mdec));
    mdec_reset();
    printf("[MDEC] Motion Decoder initialized\n");
}

void mdec_reset(void) {
    // PSX-SPX: MDEC reset state
    mdec.command = 0;
    mdec.status = MDEC_STAT_DATA_IN_REQ;  // Ready to receive commands
    
    mdec.state = MDEC_STATE_IDLE;
    mdec.current_command = 0;
    mdec.parameters_remaining = 0;
    
    // Clear FIFOs
    memset(mdec.input_fifo, 0, sizeof(mdec.input_fifo));
    memset(mdec.output_fifo, 0, sizeof(mdec.output_fifo));
    mdec.input_fifo_size = 0;
    mdec.output_fifo_size = 0;
    mdec.input_fifo_ptr = 0;
    mdec.output_fifo_ptr = 0;
    
    // Initialize default quantization tables
    for (int i = 0; i < 64; i++) {
        mdec.luminance_table[i] = 16;      // Default luminance quantization
        mdec.chrominance_table[i] = 16;    // Default chrominance quantization
        mdec.scale_table[i] = 4096;        // Default scale (1.0 in fixed point)
    }
    
    mdec.blocks_remaining = 0;
    mdec.current_block = 0;
    mdec.output_signed = false;
    mdec.output_15bit = true;  // Default to 15-bit mode
    
    printf("[MDEC] Motion Decoder reset\n");
}

void mdec_step(void) {
    // TODO: Process MDEC state machine
    switch (mdec.state) {
        case MDEC_STATE_IDLE:
            // Waiting for commands
            break;
            
        case MDEC_STATE_COMMAND:
            // Processing command parameters
            break;
            
        case MDEC_STATE_RECEIVING_DATA:
            // Receiving macro block data
            break;
            
        case MDEC_STATE_PROCESSING:
            // Decoding macro blocks
            break;
            
        case MDEC_STATE_OUTPUT_READY:
            // Output data ready
            break;
    }
}

u32 mdec_read32(u32 addr) {
    switch (addr) {
        case MDEC_COMMAND:
            // PSX-SPX: Reading command returns output data
            if (mdec.output_fifo_size > 0) {
                u32 data = mdec.output_fifo[mdec.output_fifo_ptr];
                mdec.output_fifo_ptr = (mdec.output_fifo_ptr + 1) % 32;
                mdec.output_fifo_size--;
                
                // Update status
                if (mdec.output_fifo_size == 0) {
                    mdec.status &= ~MDEC_STAT_DATA_OUT_REQ;
                }
                
                printf("[MDEC] Command/Data read = 0x%08X (FIFO: %d remaining)\n", 
                       data, mdec.output_fifo_size);
                return data;
            } else {
                printf("[MDEC] Command/Data read = 0x00000000 (FIFO empty)\n");
                return 0;
            }
            
        case MDEC_STATUS:
            // Update FIFO status in status register
            mdec.status = (mdec.status & 0x0000FFFF) |
                         ((mdec.input_fifo_size & 0x1F) << 24) |
                         ((mdec.output_fifo_size & 0x0F) << 20);
            
            printf("[MDEC] Status read = 0x%08X\n", mdec.status);
            return mdec.status;
            
        default:
            printf("[MDEC] ERROR: Unmapped read32 at 0x%08X\n", addr);
            return 0;
    }
}

void mdec_write32(u32 addr, u32 value) {
    switch (addr) {
        case MDEC_COMMAND:
            // PSX-SPX: Writing to command register
            printf("[MDEC] Command/Data write = 0x%08X\n", value);
            
            if (mdec.state == MDEC_STATE_IDLE) {
                // New command
                mdec_execute_command(value);
            } else {
                // Data for current command
                mdec_process_data(value);
            }
            break;
            
        case MDEC_STATUS:
            printf("[MDEC] Status write = 0x%08X (ignored - read-only)\n", value);
            break;
            
        default:
            printf("[MDEC] ERROR: Unmapped write32 at 0x%08X = 0x%08X\n", addr, value);
            break;
    }
}

void mdec_execute_command(u32 command) {
    mdec.current_command = command;
    u32 cmd = command & 0xF0000000;
    
    switch (cmd) {
        case MDEC_CMD_DECODE:
            {
                // Decode macro blocks
                u32 blocks = command & 0xFFFF;
                bool signed_output = (command & 0x08000000) != 0;
                bool bit15_mode = (command & 0x04000000) != 0;
                
                printf("[MDEC] Decode command: %d blocks, %s output, %s mode\n",
                       blocks, signed_output ? "signed" : "unsigned",
                       bit15_mode ? "15-bit" : "24-bit");
                
                mdec.blocks_remaining = blocks;
                mdec.current_block = 0;
                mdec.output_signed = signed_output;
                mdec.output_15bit = bit15_mode;
                mdec.state = MDEC_STATE_RECEIVING_DATA;
                
                // Update status
                mdec.status |= MDEC_STAT_CMD_BUSY;
                mdec.status |= MDEC_STAT_DATA_IN_REQ;
            }
            break;
            
        case MDEC_CMD_SET_QUANT:
            printf("[MDEC] Set quantization table command\n");
            mdec.state = MDEC_STATE_COMMAND;
            mdec.parameters_remaining = 64;  // 64 bytes for quantization table
            break;
            
        case MDEC_CMD_SET_SCALE:
            printf("[MDEC] Set scale table command\n"); 
            mdec.state = MDEC_STATE_COMMAND;
            mdec.parameters_remaining = 64;  // 64 words for scale table
            break;
            
        case MDEC_CMD_NOP:
            printf("[MDEC] NOP command\n");
            mdec.state = MDEC_STATE_IDLE;
            break;
            
        default:
            printf("[MDEC] Unknown command: 0x%08X\n", command);
            mdec.state = MDEC_STATE_IDLE;
            break;
    }
}

void mdec_process_data(u32 data) {
    // Add data to input FIFO if there's space
    if (mdec.input_fifo_size < 32) {
        int write_ptr = (mdec.input_fifo_ptr + mdec.input_fifo_size) % 32;
        mdec.input_fifo[write_ptr] = data;
        mdec.input_fifo_size++;
        
        printf("[MDEC] Data received: 0x%08X (FIFO: %d/%d)\n", 
               data, mdec.input_fifo_size, 32);
        
        // TODO: Process the data based on current command
        // For now, just echo data to output (placeholder)
        if (mdec.output_fifo_size < 32) {
            int out_write_ptr = (mdec.output_fifo_ptr + mdec.output_fifo_size) % 32;
            mdec.output_fifo[out_write_ptr] = data ^ 0x12345678;  // Dummy processing
            mdec.output_fifo_size++;
            mdec.status |= MDEC_STAT_DATA_OUT_REQ;
        }
    }
    
    // Check if command is complete
    if (mdec.parameters_remaining > 0) {
        mdec.parameters_remaining--;
        if (mdec.parameters_remaining == 0) {
            mdec.state = MDEC_STATE_IDLE;
            mdec.status &= ~MDEC_STAT_CMD_BUSY;
        }
    }
}

u32 mdec_read_output(void) {
    if (mdec.output_fifo_size > 0) {
        u32 data = mdec.output_fifo[mdec.output_fifo_ptr];
        mdec.output_fifo_ptr = (mdec.output_fifo_ptr + 1) % 32;
        mdec.output_fifo_size--;
        return data;
    }
    return 0;
}

// DMA interface
bool mdec_dma_ready_input(void) {
    return (mdec.status & MDEC_STAT_DATA_IN_REQ) != 0;
}

bool mdec_dma_ready_output(void) {
    return (mdec.status & MDEC_STAT_DATA_OUT_REQ) != 0;
}

void mdec_dma_write(u32 data) {
    mdec_process_data(data);
}

u32 mdec_dma_read(void) {
    return mdec_read_output();
}