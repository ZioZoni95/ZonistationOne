#ifndef SIO_H
#define SIO_H

#include <stdint.h>
#include <stdbool.h>

// SIO (Serial I/O) - Controller and Memory Card Interface
// PSX-SPEX: I/O Ports 1F801040h-1F80104Fh and 1F801050h-1F80105Fh

#define MEMCARD_SIZE (128 * 1024)  // 128KB per memory card
#define MEMCARD_SECTOR_SIZE 128     // Bytes per sector
#define MEMCARD_SECTORS (MEMCARD_SIZE / MEMCARD_SECTOR_SIZE)

// Memory Card structure
typedef struct {
    uint8_t data[MEMCARD_SIZE];  // 128KB card data
    char filepath[256];           // Path to .mcd file
    bool present;                 // Card inserted flag
    bool dirty;                   // Needs save to file
} MemoryCard;

// SIO State
typedef struct {
    // Registers (1F801040h-1F80104Fh for Port 1, 1F801050h-1F80105Fh for Port 2)
    uint8_t tx_data;      // JOY_DATA (1F801040h/1F801050h) - Data to send
    uint8_t rx_data;      // JOY_DATA (read) - Data received
    uint32_t stat;        // JOY_STAT (1F801044h/1F801054h) - Status
    uint16_t mode;        // JOY_MODE (1F801048h/1F801058h) - Mode
    uint16_t ctrl;        // JOY_CTRL (1F80104Ah/1F80105Ah) - Control
    uint16_t baud;        // JOY_BAUD (1F80104Eh/1F80105Eh) - Baud rate
    
    // Transfer state
    uint8_t selected_device;  // 0=none, 1=controller, 2=memcard
    uint8_t transfer_step;    // Current step in transfer sequence
    uint8_t rx_buffer[256];   // Receive buffer
    uint8_t tx_buffer[256];   // Transmit buffer
    uint32_t rx_count;        // Bytes in RX buffer
    uint32_t tx_count;        // Bytes in TX buffer
    
    // Memory Cards (Slot 1 and Slot 2)
    MemoryCard card_slot1;    // Port 1 memory card (bu00:)
    MemoryCard card_slot2;    // Port 2 memory card (bu10:)
    
    // Controller state (basic stub for now)
    bool controller_connected;
    uint16_t button_state;    // Digital button state
} Sio;

// Initialization
void sio_init(Sio* sio);

// Memory Card file operations
bool sio_load_memcard(MemoryCard* card, const char* filepath);
bool sio_save_memcard(MemoryCard* card);
void sio_create_memcard(MemoryCard* card, const char* filepath);

// SIO Register access
uint8_t sio_read8(Sio* sio, uint32_t offset);
uint16_t sio_read16(Sio* sio, uint32_t offset);
uint32_t sio_read32(Sio* sio, uint32_t offset);

void sio_write8(Sio* sio, uint32_t offset, uint8_t value);
void sio_write16(Sio* sio, uint32_t offset, uint16_t value);
void sio_write32(Sio* sio, uint32_t offset, uint32_t value);

// Controller input (for future implementation)
void sio_set_button_state(Sio* sio, uint16_t buttons);

#endif // SIO_H
