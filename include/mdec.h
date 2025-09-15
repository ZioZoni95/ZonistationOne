#ifndef MDEC_H
#define MDEC_H

#include <stdint.h>
#include <stdbool.h>

// MDEC Memory Map (based on PSX-SPX documentation)
#define MDEC_CMD_ADDR    0x1F801820  // MDEC Command Register
#define MDEC_CTRL_ADDR   0x1F801824  // MDEC Control/Status Register
#define MDEC_DATA_IN     0x1F801820  // MDEC Data Input (write)
#define MDEC_DATA_OUT    0x1F801820  // MDEC Data Output (read)

// MDEC Commands (based on PSX-SPX)
#define MDEC_CMD_DECODE_MACROBLOCK  0x01  // Decode 15-bit macroblock
#define MDEC_CMD_SET_QUANT_TABLE    0x02  // Set quantization table
#define MDEC_CMD_SET_SCALE_TABLE    0x03  // Set scale table  
#define MDEC_CMD_NOP                0x00  // No operation

// MDEC Status/Control Register bits
#define MDEC_STATUS_DATA_OUT_FIFO_EMPTY  (1 << 31)
#define MDEC_STATUS_DATA_IN_FIFO_FULL    (1 << 30)
#define MDEC_STATUS_CMD_BUSY             (1 << 29)
#define MDEC_STATUS_DATA_IN_REQUEST      (1 << 28)
#define MDEC_STATUS_DATA_OUT_REQUEST     (1 << 27)
#define MDEC_STATUS_DMA_REQUEST          (1 << 26)
#define MDEC_CTRL_RESET                  (1 << 31)
#define MDEC_CTRL_ENABLE_DMA_IN          (1 << 30)  
#define MDEC_CTRL_ENABLE_DMA_OUT         (1 << 29)
#define MDEC_CTRL_OUTPUT_DEPTH_24BIT     (1 << 27)  // 0=16bit, 1=24bit
#define MDEC_CTRL_OUTPUT_SIGNED          (1 << 26)  // 0=unsigned, 1=signed
#define MDEC_CTRL_OUTPUT_BIT15_SET       (1 << 25)  // Set bit 15 in 16bit mode

// MDEC FIFO sizes
#define MDEC_DATA_IN_FIFO_SIZE   32   // 32 words input FIFO
#define MDEC_DATA_OUT_FIFO_SIZE  32   // 32 words output FIFO

// MDEC Quantization table (64 entries for 8x8 blocks)
#define MDEC_QUANT_TABLE_SIZE   64

// MDEC Scale table (64 entries for IDCT scaling)
#define MDEC_SCALE_TABLE_SIZE   64

// MDEC Block processing
#define MDEC_BLOCK_SIZE         64    // 8x8 block = 64 coefficients
#define MDEC_MACROBLOCK_BLOCKS  6     // Y0, Y1, Y2, Y3, Cr, Cb

// Main MDEC State Structure
typedef struct {
    // Control and Status
    uint32_t status;            // Status register value
    uint32_t control;           // Control register value
    bool enabled;               // MDEC enabled flag
    bool dma_in_enabled;        // DMA input enabled
    bool dma_out_enabled;       // DMA output enabled
    bool busy;                  // Command processing busy flag
    
    // Output format settings
    bool output_24bit;          // Output format: false=16bit, true=24bit
    bool output_signed;         // Output format: false=unsigned, true=signed
    bool output_bit15_set;      // Set bit 15 in 16-bit mode
    
    // Command processing
    uint8_t current_cmd;        // Current command being processed
    uint32_t cmd_param;         // Current command parameter
    uint32_t cmd_remaining;     // Remaining words for current command
    
    // Data FIFOs
    uint32_t data_in_fifo[MDEC_DATA_IN_FIFO_SIZE];
    uint32_t data_out_fifo[MDEC_DATA_OUT_FIFO_SIZE];
    int data_in_count;          // Number of words in input FIFO
    int data_out_count;         // Number of words in output FIFO
    int data_in_pos;            // Input FIFO read position
    int data_out_pos;           // Output FIFO write position
    
    // Quantization and Scale tables
    uint8_t quant_table_y[MDEC_QUANT_TABLE_SIZE];   // Luminance quantization table
    uint8_t quant_table_c[MDEC_QUANT_TABLE_SIZE];   // Chrominance quantization table
    uint16_t scale_table[MDEC_SCALE_TABLE_SIZE];    // IDCT scale table
    
    // Block processing workspace
    int16_t block_buffer[MDEC_BLOCK_SIZE];          // Current block being processed
    uint16_t rgb_output[256];                       // RGB output buffer (16x16 pixels max)
    
    // Statistics and debugging
    uint32_t blocks_decoded;     // Total blocks decoded
    uint32_t commands_processed; // Total commands processed
} Mdec;

// Function Prototypes
void mdec_init(Mdec* mdec);
void mdec_reset(Mdec* mdec);

// Register Access Functions
uint32_t mdec_load32(Mdec* mdec, uint32_t offset);
void mdec_store32(Mdec* mdec, uint32_t offset, uint32_t value);

// Command Processing Functions
void mdec_execute_command(Mdec* mdec, uint32_t cmd);
void mdec_process_decode_macroblock(Mdec* mdec);
void mdec_process_set_quant_table(Mdec* mdec, uint32_t* data);
void mdec_process_set_scale_table(Mdec* mdec, uint32_t* data);

// FIFO Management Functions
bool mdec_data_in_fifo_full(Mdec* mdec);
bool mdec_data_in_fifo_empty(Mdec* mdec);
bool mdec_data_out_fifo_full(Mdec* mdec);
bool mdec_data_out_fifo_empty(Mdec* mdec);
void mdec_push_data_in(Mdec* mdec, uint32_t data);
uint32_t mdec_pop_data_out(Mdec* mdec);

// DMA Interface Functions (stubs for now)
void mdec_dma_in_write(Mdec* mdec, uint32_t* data, uint32_t word_count);
void mdec_dma_out_read(Mdec* mdec, uint32_t* data, uint32_t word_count);

// Block Processing Functions (stubs for now)
void mdec_decode_block(Mdec* mdec, int16_t* input, uint16_t* output);
void mdec_inverse_dct(int16_t* block);
void mdec_yuv_to_rgb(uint8_t y, uint8_t u, uint8_t v, uint8_t* r, uint8_t* g, uint8_t* b);

// Update and Status Functions
void mdec_update(Mdec* mdec);
bool mdec_is_busy(Mdec* mdec);
bool mdec_data_available(Mdec* mdec);

#endif // MDEC_H