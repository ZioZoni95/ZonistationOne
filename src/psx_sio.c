#include "../include/psx_sio.h"
#include "../include/psx_irq.h"
#include <stdio.h>
#include <string.h>

// PSX-SPX: Serial I/O implementation (skeleton)
static psx_sio_t sio;

void sio_init(void) {
    memset(&sio, 0, sizeof(sio));
    sio_reset();
    printf("[SIO] Serial I/O initialized\n");
}

void sio_reset(void) {
    // PSX-SPX: SIO reset state
    sio.sio_data = 0;
    sio.sio_stat = SIO_STAT_TX_READY | SIO_STAT_TX_EMPTY;  // Ready to transmit
    sio.sio_mode = 0;
    sio.sio_ctrl = 0;
    sio.sio_baud = 0;
    
    // Clear buffers
    memset(sio.tx_buffer, 0, sizeof(sio.tx_buffer));
    memset(sio.rx_buffer, 0, sizeof(sio.rx_buffer));
    sio.tx_ptr = sio.rx_ptr = 0;
    sio.tx_size = sio.rx_size = 0;
    
    // Reset device state
    sio.current_device = SIO_DEVICE_NONE;
    sio.device_state = 0;
    
    // Initialize controllers
    for (int i = 0; i < 2; i++) {
        sio.controller[i].buttons = 0xFFFF;  // All buttons released (active low)
        sio.controller[i].analog_lx = 0x80;  // Center position
        sio.controller[i].analog_ly = 0x80;
        sio.controller[i].analog_rx = 0x80;
        sio.controller[i].analog_ry = 0x80;
        sio.controller[i].connected = true;  // Controller connected by default
    }
    
    // Initialize memory cards
    for (int i = 0; i < 2; i++) {
        memset(sio.memcard[i].data, 0, sizeof(sio.memcard[i].data));
        sio.memcard[i].connected = false;  // No memory cards by default
        sio.memcard[i].sector = 0;
        sio.memcard[i].access_state = 0;
    }
    
    printf("[SIO] Serial I/O reset - Controllers: %d, Memory Cards: %d\n", 
           (sio.controller[0].connected ? 1 : 0) + (sio.controller[1].connected ? 1 : 0),
           (sio.memcard[0].connected ? 1 : 0) + (sio.memcard[1].connected ? 1 : 0));
}

void sio_step(void) {
    // TODO: Handle SIO timing and communication
}

u32 sio_read32(u32 addr) {
    switch (addr) {
        case SIO_DATA:
            printf("[SIO] Data read = 0x%08X\n", sio.sio_data);
            return sio.sio_data;
            
        case SIO_STAT:
            printf("[SIO] Status read = 0x%08X\n", sio.sio_stat);
            return sio.sio_stat;
            
        case SIO_MODE:
            printf("[SIO] Mode read = 0x%08X\n", sio.sio_mode);
            return sio.sio_mode;
            
        case SIO_CTRL:
            printf("[SIO] Control read = 0x%08X\n", sio.sio_ctrl);
            return sio.sio_ctrl;
            
        case SIO_BAUD:
            printf("[SIO] Baud read = 0x%08X\n", sio.sio_baud);
            return sio.sio_baud;
            
        default:
            printf("[SIO] ERROR: Unmapped read32 at 0x%08X\n", addr);
            return 0;
    }
}

void sio_write32(u32 addr, u32 value) {
    switch (addr) {
        case SIO_DATA:
            sio.sio_data = value;
            printf("[SIO] Data write = 0x%08X\n", value);
            // TODO: Handle data transmission
            break;
            
        case SIO_STAT:
            // PSX-SPX: Some status bits are write-to-clear
            sio.sio_stat &= ~(value & 0x000F);  // Clear error flags
            printf("[SIO] Status write = 0x%08X (clear bits)\n", value);
            break;
            
        case SIO_MODE:
            sio.sio_mode = value;
            printf("[SIO] Mode write = 0x%08X\n", value);
            break;
            
        case SIO_CTRL:
            sio.sio_ctrl = value;
            printf("[SIO] Control write = 0x%08X\n", value);
            
            // Handle reset
            if (value & SIO_CTRL_RESET) {
                printf("[SIO] SIO Reset triggered\n");
                sio_reset();
            }
            break;
            
        case SIO_BAUD:
            sio.sio_baud = value;
            printf("[SIO] Baud write = 0x%08X\n", value);
            break;
            
        default:
            printf("[SIO] ERROR: Unmapped write32 at 0x%08X = 0x%08X\n", addr, value);
            break;
    }
}

// 16-bit and 8-bit access (common for SIO)
u16 sio_read16(u32 addr) {
    u32 value = sio_read32(addr & ~3);
    return (addr & 2) ? (value >> 16) : (value & 0xFFFF);
}

void sio_write16(u32 addr, u16 value) {
    // TODO: Implement proper 16-bit SIO access
    printf("[SIO] 16-bit write at 0x%08X = 0x%04X\n", addr, value);
}

u8 sio_read8(u32 addr) {
    u32 value = sio_read32(addr & ~3);
    return (value >> ((addr & 3) * 8)) & 0xFF;
}

void sio_write8(u32 addr, u8 value) {
    // TODO: Implement proper 8-bit SIO access
    printf("[SIO] 8-bit write at 0x%08X = 0x%02X\n", addr, value);
}

// Device communication (TODO: Implement communication protocols)
void sio_select_device(int port) {
    printf("[SIO] TODO: Select device on port %d\n", port);
}

void sio_deselect_device(void) {
    printf("[SIO] TODO: Deselect current device\n");
}

u8 sio_transfer_byte(u8 data) {
    printf("[SIO] TODO: Transfer byte 0x%02X\n", data);
    return 0xFF;  // Default response
}

// Controller interface
void sio_set_button_state(int controller, u16 buttons) {
    if (controller >= 0 && controller < 2) {
        sio.controller[controller].buttons = buttons;
        printf("[SIO] Controller %d buttons = 0x%04X\n", controller, buttons);
    }
}

void sio_set_analog_state(int controller, u8 lx, u8 ly, u8 rx, u8 ry) {
    if (controller >= 0 && controller < 2) {
        sio.controller[controller].analog_lx = lx;
        sio.controller[controller].analog_ly = ly;
        sio.controller[controller].analog_rx = rx;
        sio.controller[controller].analog_ry = ry;
        printf("[SIO] Controller %d analog: L(%d,%d) R(%d,%d)\n", 
               controller, lx, ly, rx, ry);
    }
}

// Memory Card interface
bool sio_memcard_read_sector(int card, u32 sector, u8* buffer) {
    if (card < 0 || card >= 2 || !sio.memcard[card].connected) {
        return false;
    }
    
    if (sector >= (128 * 1024 / 128)) {  // 128KB / 128 bytes per sector
        return false;
    }
    
    memcpy(buffer, &sio.memcard[card].data[sector * 128], 128);
    printf("[SIO] Memory Card %d: Read sector %d\n", card, sector);
    return true;
}

bool sio_memcard_write_sector(int card, u32 sector, const u8* buffer) {
    if (card < 0 || card >= 2 || !sio.memcard[card].connected) {
        return false;
    }
    
    if (sector >= (128 * 1024 / 128)) {  // 128KB / 128 bytes per sector
        return false;
    }
    
    memcpy(&sio.memcard[card].data[sector * 128], buffer, 128);
    printf("[SIO] Memory Card %d: Write sector %d\n", card, sector);
    return true;
}