#ifndef PSX_SIO_H
#define PSX_SIO_H

#include "psx_types.h"

// PSX-SPX: Serial I/O (SIO) Implementation
// Handles Controllers, Memory Cards, and Serial Communication

// PSX-SPX: SIO Register addresses
#define SIO_DATA        0x1F801040  // SIO Data
#define SIO_STAT        0x1F801044  // SIO Status
#define SIO_MODE        0x1F801048  // SIO Mode
#define SIO_CTRL        0x1F80104A  // SIO Control
#define SIO_BAUD        0x1F80104E  // SIO Baud Rate

// PSX-SPX: SIO Status bits
#define SIO_STAT_TX_READY       0x0001  // Transmit Ready
#define SIO_STAT_RX_READY       0x0002  // Receive Ready  
#define SIO_STAT_TX_EMPTY       0x0004  // Transmit Empty
#define SIO_STAT_PARITY_ERROR   0x0008  // Parity Error
#define SIO_STAT_RX_OVERRUN     0x0010  // Receive Overrun
#define SIO_STAT_FRAMING_ERROR  0x0020  // Framing Error
#define SIO_STAT_DSR            0x0080  // DSR Input Level
#define SIO_STAT_CTS            0x0100  // CTS Input Level
#define SIO_STAT_IRQ            0x0200  // IRQ Flag

// PSX-SPX: SIO Control bits
#define SIO_CTRL_TX_ENABLE      0x0001  // Transmit Enable
#define SIO_CTRL_DTR            0x0002  // DTR Output Level
#define SIO_CTRL_RX_ENABLE      0x0004  // Receive Enable
#define SIO_CTRL_TX_IRQ_ENABLE  0x0008  // Transmit IRQ Enable
#define SIO_CTRL_RX_IRQ_ENABLE  0x0010  // Receive IRQ Enable
#define SIO_CTRL_DSR_IRQ_ENABLE 0x0020  // DSR IRQ Enable
#define SIO_CTRL_RTS            0x0040  // RTS Output Level
#define SIO_CTRL_RESET          0x0040  // SIO Reset

// Controller/Memory Card state
typedef enum {
    SIO_DEVICE_NONE = 0,
    SIO_DEVICE_CONTROLLER,
    SIO_DEVICE_MEMORY_CARD
} sio_device_type_t;

typedef struct {
    // PSX-SPX: SIO registers
    u32 sio_data;       // Data register
    u32 sio_stat;       // Status register  
    u32 sio_mode;       // Mode register
    u32 sio_ctrl;       // Control register
    u32 sio_baud;       // Baud rate register
    
    // Communication state
    u8 tx_buffer[256];  // Transmit buffer
    u8 rx_buffer[256];  // Receive buffer
    int tx_ptr, rx_ptr;
    int tx_size, rx_size;
    
    // Device state
    sio_device_type_t current_device;
    int device_state;   // Device-specific state machine
    
    // Controller state
    struct {
        u16 buttons;        // Current button state
        u8 analog_lx, analog_ly;  // Left analog stick
        u8 analog_rx, analog_ry;  // Right analog stick
        bool connected;
    } controller[2];
    
    // Memory Card state  
    struct {
        u8 data[128 * 1024];  // 128KB memory card
        bool connected;
        u32 sector;
        int access_state;
    } memcard[2];
    
} psx_sio_t;

// SIO interface functions
void sio_init(void);
void sio_reset(void);
void sio_step(void);

// Register access
u32 sio_read32(u32 addr);
void sio_write32(u32 addr, u32 value);
u16 sio_read16(u32 addr);
void sio_write16(u32 addr, u16 value);
u8 sio_read8(u32 addr);
void sio_write8(u32 addr, u8 value);

// Device communication
void sio_select_device(int port);
void sio_deselect_device(void);
u8 sio_transfer_byte(u8 data);

// Controller interface
void sio_set_button_state(int controller, u16 buttons);
void sio_set_analog_state(int controller, u8 lx, u8 ly, u8 rx, u8 ry);

// Memory Card interface  
bool sio_memcard_read_sector(int card, u32 sector, u8* buffer);
bool sio_memcard_write_sector(int card, u32 sector, const u8* buffer);

#endif // PSX_SIO_H