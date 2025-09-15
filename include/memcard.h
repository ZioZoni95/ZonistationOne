#ifndef MEMCARD_H
#define MEMCARD_H

#include <stdint.h>
#include <stdbool.h>

// Memory Card specifications based on PSX-SPX documentation
#define MEMCARD_BLOCK_SIZE      128     // 128 bytes per block
#define MEMCARD_TOTAL_BLOCKS    1024    // 1024 blocks total (128KB)
#define MEMCARD_TOTAL_SIZE      (MEMCARD_BLOCK_SIZE * MEMCARD_TOTAL_BLOCKS)
#define MEMCARD_SAVE_SLOTS      15      // 15 save game slots
#define MEMCARD_DIRECTORY_BLOCKS 1      // 1 block for directory

// Memory Card Frame structure (based on PSX-SPX)
#define MEMCARD_FRAME_SIZE      128     // Frame size in bytes
#define MEMCARD_ID_SIZE         2       // Memory card ID size
#define MEMCARD_CMD_SIZE        1       // Command size
#define MEMCARD_ADDR_SIZE       2       // Address size
#define MEMCARD_DATA_SIZE       128     // Data payload size
#define MEMCARD_CHECKSUM_SIZE   1       // Checksum size

// Memory Card Commands (based on PSX-SPX)
#define MEMCARD_CMD_READ        0x52    // Read command
#define MEMCARD_CMD_WRITE       0x57    // Write command  
#define MEMCARD_CMD_ID          0x53    // Get ID command

// Memory Card Responses
#define MEMCARD_RESP_ID1        0x5A    // Standard response 1
#define MEMCARD_RESP_ID2        0x5D    // Standard response 2
#define MEMCARD_RESP_ACK        0x47    // Command acknowledged
#define MEMCARD_RESP_COMPLETE   0x4E    // Transfer complete

// Memory Card Status flags
#define MEMCARD_STATUS_PRESENT  (1 << 0) // Card is inserted
#define MEMCARD_STATUS_READY    (1 << 1) // Card is ready for operations
#define MEMCARD_STATUS_ERROR    (1 << 2) // Error occurred
#define MEMCARD_STATUS_BUSY     (1 << 3) // Operation in progress
#define MEMCARD_STATUS_WRITE_PROTECT (1 << 4) // Write protected

// Memory Card Directory Entry (16 bytes)
typedef struct {
    uint32_t status;        // Status flags (0x51 = used, 0xA0 = deleted, 0xFF = free)
    uint32_t next_block;    // Next block in chain (or 0xFFFFFFFF if last)
    char title[8];          // Save game title (8 characters)
} MemcardDirEntry;

// Memory Card Directory Block (first block contains directory)
typedef struct {
    MemcardDirEntry entries[MEMCARD_SAVE_SLOTS];  // 15 directory entries
    uint8_t unused[53];     // Remaining bytes in block
} MemcardDirectory;

// Memory Card Save Header (first frame of save data)
typedef struct {
    char magic[2];          // "SC" magic identifier
    uint8_t icon_flag;      // Icon display flag
    uint8_t block_count;    // Number of blocks used by this save
    char title[64];         // Save title (64 bytes, SJIS)
    uint8_t reserved[28];   // Reserved bytes
    uint16_t icon_clut[16]; // Icon palette (16 colors)
    uint16_t icon_data[128]; // Icon bitmap data (16x16, 4-bit)
} MemcardSaveHeader;

// Memory Card Communication State
typedef enum {
    MEMCARD_STATE_IDLE = 0,
    MEMCARD_STATE_WAIT_CMD,
    MEMCARD_STATE_WAIT_ADDR_HI,
    MEMCARD_STATE_WAIT_ADDR_LO,
    MEMCARD_STATE_READ_DATA,
    MEMCARD_STATE_WRITE_DATA,
    MEMCARD_STATE_CHECKSUM,
    MEMCARD_STATE_COMPLETE
} MemcardState;

// Main Memory Card Structure
typedef struct {
    // Card presence and status
    bool present;           // Card is inserted
    bool initialized;       // Card is initialized
    bool write_protected;   // Write protection enabled
    uint8_t status_flags;   // Status flag register
    
    // Memory storage
    uint8_t data[MEMCARD_TOTAL_SIZE];  // 128KB memory card data
    MemcardDirectory directory;        // Directory structure overlay
    
    // Communication state
    MemcardState state;     // Current communication state
    uint8_t current_cmd;    // Current command being processed
    uint16_t current_addr;  // Current address for read/write
    uint16_t frame_addr;    // Frame address (block * 128)
    
    // Transfer buffers
    uint8_t tx_buffer[MEMCARD_FRAME_SIZE];  // Transmit buffer
    uint8_t rx_buffer[MEMCARD_FRAME_SIZE];  // Receive buffer
    int tx_pos;             // Current transmit position
    int rx_pos;             // Current receive position
    int transfer_count;     // Number of bytes in current transfer
    
    // Checksum calculation
    uint8_t checksum;       // Running checksum
    uint8_t expected_checksum; // Expected checksum for verification
    
    // Timing and delays
    uint32_t last_access;   // Last access time
    uint32_t busy_until;    // Busy until this time
    
    // Error handling
    bool error;             // Error flag
    uint8_t error_code;     // Last error code
    
    // Statistics
    uint32_t read_count;    // Total reads performed
    uint32_t write_count;   // Total writes performed
    uint32_t error_count;   // Total errors encountered
    
    // File path for persistence
    char file_path[256];    // Path to .mcr file
    bool file_dirty;        // Data needs to be saved
} Memcard;

// Function Prototypes

// Initialization and Management
void memcard_init(Memcard* card);
void memcard_reset(Memcard* card);
void memcard_insert(Memcard* card);
void memcard_remove(Memcard* card);
void memcard_set_write_protect(Memcard* card, bool protected);

// File I/O (save/load .mcr files)
bool memcard_load_from_file(Memcard* card, const char* filepath);
bool memcard_save_to_file(Memcard* card, const char* filepath);
void memcard_set_file_path(Memcard* card, const char* filepath);
void memcard_flush_to_file(Memcard* card);

// Communication Protocol (SIO interface)
uint8_t memcard_exchange_byte(Memcard* card, uint8_t data);
void memcard_begin_transfer(Memcard* card);
void memcard_end_transfer(Memcard* card);
bool memcard_is_ready(Memcard* card);
bool memcard_is_busy(Memcard* card);

// Command Processing
void memcard_process_command(Memcard* card, uint8_t cmd);
void memcard_process_read_command(Memcard* card);
void memcard_process_write_command(Memcard* card);
void memcard_process_id_command(Memcard* card);

// Data Access
uint8_t memcard_read_byte(Memcard* card, uint16_t address);
void memcard_write_byte(Memcard* card, uint16_t address, uint8_t data);
void memcard_read_frame(Memcard* card, uint16_t frame_addr, uint8_t* buffer);
void memcard_write_frame(Memcard* card, uint16_t frame_addr, uint8_t* buffer);

// Directory Management
void memcard_init_directory(Memcard* card);
int memcard_find_free_slot(Memcard* card);
int memcard_find_save_by_name(Memcard* card, const char* name);
bool memcard_delete_save(Memcard* card, int slot);
void memcard_defragment(Memcard* card);

// Checksum and Validation
uint8_t memcard_calculate_checksum(uint8_t* data, int length);
bool memcard_verify_checksum(Memcard* card);
void memcard_update_checksum(Memcard* card, uint8_t data);

// Status and Information
uint8_t memcard_get_status(Memcard* card);
void memcard_update_status(Memcard* card);
int memcard_get_free_blocks(Memcard* card);
int memcard_get_used_blocks(Memcard* card);

// Update and Maintenance
void memcard_update(Memcard* card);
void memcard_format(Memcard* card);

#endif // MEMCARD_H