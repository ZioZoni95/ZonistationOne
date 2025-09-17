#ifndef PSX_MDEC_H
#define PSX_MDEC_H

#include "psx_types.h"

// PSX-SPX: MDEC (Motion Decoder) Implementation
// Handles FMV video decompression and image processing

// PSX-SPX: MDEC Register addresses
#define MDEC_COMMAND    0x1F801820  // MDEC Command/Parameter
#define MDEC_STATUS     0x1F801824  // MDEC Status

// PSX-SPX: MDEC Commands
#define MDEC_CMD_DECODE         0x10000000  // Decode macro block
#define MDEC_CMD_SET_QUANT      0x20000000  // Set quantization table
#define MDEC_CMD_SET_SCALE      0x30000000  // Set scale table
#define MDEC_CMD_NOP            0x00000000  // No operation

// PSX-SPX: MDEC Status bits
#define MDEC_STAT_DATA_IN_REQ   0x80000000  // Data-In Request (Ready to receive data)
#define MDEC_STAT_DATA_OUT_REQ  0x40000000  // Data-Out Request (Data ready for output)
#define MDEC_STAT_CMD_BUSY      0x20000000  // Command Busy
#define MDEC_STAT_DATA_IN_FIFO  0x1F000000  // Data-In FIFO (bits 24-28)
#define MDEC_STAT_DATA_OUT_FIFO 0x00F00000  // Data-Out FIFO (bits 20-23)
#define MDEC_STAT_CURRENT_BLOCK 0x0000FFFF  // Current Block (bits 0-15)

// MDEC processing state
typedef enum {
    MDEC_STATE_IDLE = 0,
    MDEC_STATE_COMMAND,
    MDEC_STATE_RECEIVING_DATA,
    MDEC_STATE_PROCESSING,
    MDEC_STATE_OUTPUT_READY
} mdec_state_t;

typedef struct {
    // PSX-SPX: MDEC registers
    u32 command;        // Command register
    u32 status;         // Status register
    
    // Processing state
    mdec_state_t state;
    u32 current_command;
    u32 parameters_remaining;
    
    // Data buffers
    u32 input_fifo[32];     // Input FIFO buffer
    u32 output_fifo[32];    // Output FIFO buffer
    int input_fifo_size;
    int output_fifo_size;
    int input_fifo_ptr;
    int output_fifo_ptr;
    
    // Quantization tables
    u8 luminance_table[64];     // Y component quantization
    u8 chrominance_table[64];   // Cb/Cr component quantization
    
    // Scale table  
    s16 scale_table[64];        // IDCT scale factors
    
    // Macro block processing
    u32 blocks_remaining;
    u32 current_block;
    
    // Output format
    bool output_signed;     // Signed/Unsigned output
    bool output_15bit;      // 15-bit/24-bit mode
    
} psx_mdec_t;

// MDEC interface functions
void mdec_init(void);
void mdec_reset(void);
void mdec_step(void);

// Register access
u32 mdec_read32(u32 addr);
void mdec_write32(u32 addr, u32 value);

// Command processing
void mdec_execute_command(u32 command);
void mdec_process_data(u32 data);
u32 mdec_read_output(void);

// DMA interface (used by DMA channels 0 and 1)
bool mdec_dma_ready_input(void);
bool mdec_dma_ready_output(void);
void mdec_dma_write(u32 data);
u32 mdec_dma_read(void);

#endif // PSX_MDEC_H