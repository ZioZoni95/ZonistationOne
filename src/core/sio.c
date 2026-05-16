// SIO (PAD) Implementation based on DuckStation
// Implements PSX Serial I/O controller and memory card interface
// Based on DuckStation's pad.cpp: https://github.com/stenzek/duckstation

#include "sio.h"
#include "log.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define SIO_DBG(...) LOG_SYSTEM_TRACE(__VA_ARGS__)

// ============================================================================
// STATE MACHINE
// ============================================================================
typedef enum {
    STATE_IDLE,
    STATE_TRANSMITTING,
    STATE_WAITING_FOR_ACK
} SioState;

typedef enum {
    ACTIVE_DEVICE_NONE,
    ACTIVE_DEVICE_CONTROLLER,
    ACTIVE_DEVICE_MEMCARD,
    ACTIVE_DEVICE_MULTITAP
} ActiveDevice;

// ============================================================================
// REGISTER BITFIELDS (PSX-SPX Reference)
// ============================================================================

// JOY_CTRL (1F80104Ah)
#define CTRL_TXEN        (1 << 0)   // TX Enable
#define CTRL_SELECT      (1 << 1)   // /JOYn output (0=High, 1=Low/Select)
#define CTRL_RXEN        (1 << 2)   // RX Enable
#define CTRL_ACK         (1 << 4)   // ACK input level (read-only when pulsed via IRQ)
#define CTRL_RESET       (1 << 6)   // Soft reset
#define CTRL_RXIMODE     (2 << 8)   // RX Interrupt Mode (bits 9-8)
#define CTRL_TXINTEN     (1 << 10)  // TX Interrupt enable
#define CTRL_RXINTEN     (1 << 11)  // RX Interrupt enable
#define CTRL_ACKINTEN    (1 << 12)  // ACK Interrupt enable
#define CTRL_SLOT        (1 << 13)  // Multitap slot select

// JOY_STAT (1F801044h)
#define STAT_TXRDY       (1 << 0)   // TX Ready (FIFO not full)
#define STAT_RXFIFONEMPTY (1 << 1)  // RX FIFO not empty
#define STAT_TXDONE      (1 << 2)   // TX Done
#define STAT_PARITYERR   (1 << 3)   // Parity error
#define STAT_ACKINPUT    (1 << 7)   // ACK input
#define STAT_INTR        (1 << 9)   // Interrupt (IRQ7)
#define STAT_BAUDEN      (1 << 10)  // Baud enable

// ============================================================================
// CONTROLLER PROTOCOL
// ============================================================================

// Controller response bytes for digital pad
#define CTRL_RESPONSE_ID       0x41  // Digital controller ID
#define CTRL_RESPONSE_READY    0x5A  // Controller ready
#define CTRL_NO_RESPONSE       0xFF  // No device connected

// Memory card response
#define MEMCARD_RESPONSE_ID    0x5A  // Memory card ID
#define MEMCARD_NO_RESPONSE    0xFF

// ============================================================================
// SIO INTERNAL STATE
// ============================================================================

// Extended Sio structure (internal state)
typedef struct {
    // Registers
    uint8_t tx_data;
    uint8_t rx_data;
    uint32_t stat;
    uint16_t mode;
    uint16_t ctrl;
    uint16_t baud;

    // Transfer state machine
    SioState state;
    ActiveDevice active_device;
    uint8_t transmit_value;
    uint8_t receive_buffer;
    bool receive_buffer_full;
    bool transmit_buffer_full;
    uint8_t transmit_buffer;
    uint32_t transfer_ticks;
    uint32_t ack_delay_ticks;

    // Controller state
    bool controller_connected;
    uint16_t button_state;
    uint8_t controller_transfer_step;

    // Memory Cards
    MemoryCard card_slot1;
    MemoryCard card_slot2;
    bool card_slot1_present;
    bool card_slot2_present;

    // Pending events
    bool pending_irq;
    uint32_t pending_irq_type;  // 0=none, 1=TX, 2=RX, 3=ACK
} SioInternal;

static SioInternal sio_internal = {};

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

static void sio_soft_reset(void);
static void sio_begin_transfer(void);
static void sio_do_transfer(void);
static void sio_do_ack(void);
static void sio_end_transfer(void);
static void sio_update_joystat(void);
static void sio_trigger_irq(const char* type);
static uint8_t sio_controller_transfer(uint8_t tx_byte);
static bool sio_can_transfer(void);
static void sio_reset_device_transfer_state(void);

// ============================================================================
// INITIALIZATION
// ============================================================================

void sio_init(Sio* sio) {
    memset(sio, 0, sizeof(Sio));
    memset(&sio_internal, 0, sizeof(SioInternal));

    // Initialize register state
    sio_internal.stat = STAT_TXRDY | STAT_TXDONE;
    sio_internal.mode = 0x000D;   // 8-bit, no parity
    sio_internal.ctrl = 0x0000;
    sio_internal.baud = 0x0088;

    // Initialize state machine
    sio_internal.state = STATE_IDLE;
    sio_internal.active_device = ACTIVE_DEVICE_NONE;
    sio_internal.transmit_buffer_full = false;
    sio_internal.receive_buffer_full = false;

    // Initialize controller
    sio_internal.controller_connected = false;
    sio_internal.button_state = 0xFFFF;  // All buttons released
    sio_internal.controller_transfer_step = 0;

    // Initialize memory cards
    sio_internal.card_slot1_present = false;
    sio_internal.card_slot2_present = false;

    // Copy to public structure
    sio->stat = sio_internal.stat;
    sio->mode = sio_internal.mode;
    sio->ctrl = sio_internal.ctrl;
    sio->baud = sio_internal.baud;
    sio->button_state = sio_internal.button_state;
    sio->controller_connected = sio_internal.controller_connected;

    LOG_SYSTEM_INFO("[SYSTEM] SIO initialized (DuckStation-style implementation)");
}

// ============================================================================
// REGISTER ACCESS
// ============================================================================

uint8_t sio_read8(Sio* sio, uint32_t offset) {
    switch (offset) {
        case 0x00: {  // JOY_DATA (1F801040h)
            // Return RX buffer
            uint8_t value = sio_internal.receive_buffer_full ? sio_internal.receive_buffer : 0xFF;
            SIO_DBG("[SIO] Read JOY_DATA = 0x%02x (full=%d, step=%d, device=%d)",
                value, sio_internal.receive_buffer_full, sio_internal.controller_transfer_step,
                sio_internal.active_device);
            
            // Clear RX buffer full flag
            sio_internal.receive_buffer_full = false;
            sio_update_joystat();
            
            return value;
        }

        case 0x04:  // JOY_STAT low byte (1F801044h)
            return sio_internal.stat & 0xFF;

        case 0x05:  // JOY_STAT high byte
            return (sio_internal.stat >> 8) & 0xFF;

        default:
            return 0xFF;
    }
}

uint16_t sio_read16(Sio* sio, uint32_t offset) {
    switch (offset) {
        case 0x04:  // JOY_STAT (1F801044h)
            return sio_internal.stat;

        case 0x08:  // JOY_MODE (1F801048h)
            return sio_internal.mode;

        case 0x0A:  // JOY_CTRL (1F80104Ah)
            return sio_internal.ctrl;

        case 0x0E:  // JOY_BAUD (1F80104Eh)
            return sio_internal.baud;

        default:
            return 0xFFFF;
    }
}

uint32_t sio_read32(Sio* sio, uint32_t offset) {
    uint16_t low = sio_read16(sio, offset);
    uint16_t high = sio_read16(sio, offset + 2);
    return (high << 16) | low;
}

void sio_write8(Sio* sio, uint32_t offset, uint8_t value) {
    switch (offset) {
        case 0x00:  // JOY_DATA (1F801040h)
            SIO_DBG("[SIO] Write JOY_DATA = 0x%02x", value);
            
            if (sio_internal.transmit_buffer_full) {
                LOG_SYSTEM_WARN("[SYSTEM] SIO TX FIFO overrun");
            }

            sio_internal.transmit_buffer = value;
            sio_internal.transmit_buffer_full = true;

            // Fire TX interrupt if enabled
            if (sio_internal.ctrl & CTRL_TXINTEN) {
                sio_trigger_irq("TX");
            }

            // Start transfer if conditions allow
            if (sio_internal.state == STATE_IDLE && sio_can_transfer()) {
                sio_begin_transfer();
            }
            break;

        default:
            break;
    }
}

void sio_write16(Sio* sio, uint32_t offset, uint16_t value) {
    switch (offset) {
        case 0x00:  // JOY_DATA halfword write
            // JOY_DATA is effectively byte-oriented for pad transfers.
            // Keep low byte semantics for BIOS code using 16-bit stores.
            sio_write8(sio, 0x00, (uint8_t)(value & 0xFF));
            break;

        case 0x08:  // JOY_MODE (1F801048h)
            sio_internal.mode = value;
            LOG_SYSTEM_DEBUG("[SIO] JOY_MODE <- 0x%04x", value);
            break;

        case 0x0A: {  // JOY_CTRL (1F80104Ah)
            LOG_SYSTEM_DEBUG("[SIO] JOY_CTRL <- 0x%04x (SELECT=%d, TXEN=%d, RESET=%d, ACK=%d)",
                    value, !!(value & CTRL_SELECT), !!(value & CTRL_TXEN),
                    !!(value & CTRL_RESET), !!(value & CTRL_ACK));

            sio_internal.ctrl = value;

            // Handle soft reset
            if (value & CTRL_RESET) {
                sio_soft_reset();
                break;
            }

            // Handle ACK (write to clear IRQ)
            if (value & CTRL_ACK) {
                sio_internal.stat &= ~STAT_INTR;
                sio_internal.stat &= ~STAT_ACKINPUT;
            }

            // Handle SELECT deassert
            if (!(value & CTRL_SELECT)) {
                sio_reset_device_transfer_state();
            }

            // Handle transfer enable/disable
            if (!(value & CTRL_SELECT) || !(value & CTRL_TXEN)) {
                if (sio_internal.state != STATE_IDLE) {
                    sio_end_transfer();
                }
            } else {
                if (sio_internal.state == STATE_IDLE && sio_can_transfer()) {
                    sio_begin_transfer();
                }
            }

            sio_update_joystat();
            break;
        }

        case 0x0E:  // JOY_BAUD (1F80104Eh)
            sio_internal.baud = value;
            SIO_DBG("[SIO] Write JOY_BAUD = 0x%04x", value);
            break;

        default:
            break;
    }
}

void sio_write32(Sio* sio, uint32_t offset, uint32_t value) {
    sio_write16(sio, offset, value & 0xFFFF);
    sio_write16(sio, offset + 2, (value >> 16) & 0xFFFF);
}

// ============================================================================
// PUBLIC INTERFACE
// ============================================================================

void sio_set_button_state(Sio* sio, uint16_t buttons) {
    if (buttons != sio_internal.button_state) {
        static uint16_t last_logged = 0xFFFF;
        if (buttons != last_logged) {
            SIO_DBG("[SIO] Button state: 0x%04x (was 0x%04x)", buttons, sio_internal.button_state);
            last_logged = buttons;
        }
    }
    sio_internal.button_state = buttons;
    sio->button_state = buttons;
}

void sio_set_controller_connected(Sio* sio, bool connected) {
    sio_internal.controller_connected = connected;
    sio->controller_connected = connected;
    LOG_SYSTEM_INFO("[SYSTEM] SIO: Controller %s", connected ? "connected" : "disconnected");
}

// ============================================================================
// INTERNAL TRANSFER LOGIC (DuckStation-style)
// ============================================================================

static bool sio_can_transfer(void) {
    return sio_internal.transmit_buffer_full &&
           (sio_internal.ctrl & CTRL_SELECT) &&
           (sio_internal.ctrl & CTRL_TXEN);
}

static void sio_soft_reset(void) {
    if (sio_internal.state != STATE_IDLE) {
        sio_end_transfer();
    }

    sio_internal.ctrl = 0;
    sio_internal.stat = STAT_TXRDY | STAT_TXDONE;
    sio_internal.mode = 0;
    sio_internal.receive_buffer = 0;
    sio_internal.receive_buffer_full = false;
    sio_internal.transmit_buffer = 0;
    sio_internal.transmit_buffer_full = false;
    sio_reset_device_transfer_state();
    sio_update_joystat();
    SIO_DBG("[SIO] Soft reset");
}

static void sio_begin_transfer(void) {
    SIO_DBG("[SIO] BeginTransfer");
    
    if (sio_internal.state != STATE_IDLE || !sio_can_transfer()) {
        return;
    }

    sio_internal.state = STATE_TRANSMITTING;
    sio_internal.ctrl |= CTRL_RXEN;
    sio_internal.transmit_value = sio_internal.transmit_buffer;
    sio_internal.transmit_buffer_full = false;

    // In DuckStation this would schedule an event after some ticks
    // For our simplified implementation, do transfer immediately
    sio_do_transfer();
}

static void sio_do_transfer(void) {
    SIO_DBG("[SIO] DoTransfer");
    
    uint8_t data_out = sio_internal.transmit_value;
    uint8_t data_in = 0xFF;
    bool ack = false;

    // Try to transfer to active device
    if (sio_internal.active_device == ACTIVE_DEVICE_NONE) {
        // Autodetect connected device
        if (sio_internal.controller_connected) {
            const uint8_t prev_step = sio_internal.controller_transfer_step;
            data_in = sio_controller_transfer(data_out);
            // ACK is based on protocol progress, not data value.
            // Button bytes can legitimately be 0xFF when no buttons are pressed.
            ack = (sio_internal.controller_transfer_step != prev_step);
            if (ack) {
                sio_internal.active_device = ACTIVE_DEVICE_CONTROLLER;
                SIO_DBG("[SIO] Controller detected");
            }
        } else {
            ack = false;
        }
    } else if (sio_internal.active_device == ACTIVE_DEVICE_CONTROLLER) {
        const uint8_t prev_step = sio_internal.controller_transfer_step;
        data_in = sio_controller_transfer(data_out);
        // Continue ACKing while controller exchange advances through steps 0..3.
        ack = (sio_internal.controller_transfer_step != prev_step);
    }

    // Store response
    sio_internal.receive_buffer = data_in;
    sio_internal.receive_buffer_full = true;

    // Fire RX interrupt if enabled
    if (sio_internal.ctrl & CTRL_RXINTEN) {
        sio_trigger_irq("RX");
    }

    SIO_DBG("[SIO] Transfer done: TX=0x%02x, RX=0x%02x, ACK=%d", data_out, data_in, ack);

    // Device no longer responding
    if (!ack) {
        sio_internal.active_device = ACTIVE_DEVICE_NONE;
        sio_end_transfer();
    } else {
        // Device still responding - wait for ACK
        // In real DuckStation this schedules state = WAITING_FOR_ACK with a timer
        // For simplicity, we immediately mark ACK
        sio_internal.state = STATE_WAITING_FOR_ACK;
        // Simulate ACK delay (in real impl this would be scheduled timer)
        sio_do_ack();
    }

    sio_update_joystat();
}

static void sio_do_ack(void) {
    SIO_DBG("[SIO] DoACK");
    
    sio_internal.stat |= STAT_ACKINPUT;

    // Fire ACK interrupt if enabled
    if (sio_internal.ctrl & CTRL_ACKINTEN) {
        sio_trigger_irq("ACK");
    }

    sio_end_transfer();
    sio_update_joystat();

    // Chain next transfer if possible
    if (sio_can_transfer()) {
        sio_begin_transfer();
    }
}

static void sio_end_transfer(void) {
    SIO_DBG("[SIO] EndTransfer");
    
    if (sio_internal.state == STATE_IDLE) {
        return;  // Already idle
    }

    sio_internal.state = STATE_IDLE;
}

static void sio_reset_device_transfer_state(void) {
    SIO_DBG("[SIO] ResetDeviceTransferState (step=%d)", sio_internal.controller_transfer_step);
    
    sio_internal.active_device = ACTIVE_DEVICE_NONE;
    
    // Only reset transfer step after a complete 4-byte sequence
    // or if we're not in the middle of a sequence
    if (sio_internal.controller_transfer_step >= 4) {
        sio_internal.controller_transfer_step = 0;
        SIO_DBG("[SIO] Sequence complete, resetting step");
    } else if (sio_internal.controller_transfer_step > 0) {
        // Keep step across SELECT toggles - the BIOS does multiple transfers per sequence
        SIO_DBG("[SIO] Keeping step=%d for next transfer", sio_internal.controller_transfer_step);
    }
}

static void sio_update_joystat(void) {
    sio_internal.stat &= ~(STAT_TXRDY | STAT_RXFIFONEMPTY | STAT_TXDONE);

    if (!sio_internal.transmit_buffer_full) {
        sio_internal.stat |= STAT_TXRDY;
    }

    if (sio_internal.receive_buffer_full) {
        sio_internal.stat |= STAT_RXFIFONEMPTY;
    }

    if (!sio_internal.transmit_buffer_full && sio_internal.state == STATE_IDLE) {
        sio_internal.stat |= STAT_TXDONE;
    }
}

static void sio_trigger_irq(const char* type) {
    sio_internal.stat |= STAT_INTR;
    sio_internal.pending_irq = true;
    SIO_DBG("[SIO] IRQ: %s", type);
}

// ============================================================================
// CONTROLLER PROTOCOL (Digital Pad)
// ============================================================================

static uint8_t sio_controller_transfer(uint8_t tx_byte) {
    uint8_t response = 0xFF;

    SIO_DBG("[SIO_CTRL] Transfer step %d, TX=0x%02x, buttons=0x%04x",
            sio_internal.controller_transfer_step, tx_byte, sio_internal.button_state);

    switch (sio_internal.controller_transfer_step) {
        case 0:
            // First byte: check command (0x01 = read state)
            if (tx_byte == 0x01) {
                response = CTRL_RESPONSE_ID;  // 0x41 = digital controller
                sio_internal.controller_transfer_step = 1;
                SIO_DBG("[SIO_CTRL] Step 0->1: Recevied 0x01, sending controller ID 0x41");
            } else {
                response = 0xFF;
                SIO_DBG("[SIO_CTRL] Step 0: Unknown command 0x%02x", tx_byte);
            }
            break;

        case 1:
            // Second byte: usually data request (0x42 = read digital)
            response = CTRL_RESPONSE_READY;  // 0x5A
            sio_internal.controller_transfer_step = 2;
            SIO_DBG("[SIO_CTRL] Step 1->2: Sending separator 0x5A");
            break;

        case 2:
            // Third byte: button state HIGH byte
            response = (sio_internal.button_state >> 8) & 0xFF;
            sio_internal.controller_transfer_step = 3;
            SIO_DBG("[SIO_CTRL] Step 2->3: Sending button_high 0x%02x", response);
            break;

        case 3:
            // Fourth byte: button state LOW byte
            response = sio_internal.button_state & 0xFF;
            sio_internal.controller_transfer_step = 4;
            SIO_DBG("[SIO_CTRL] Step 3->4: Sending button_low 0x%02x", response);
            break;

        case 4:
        default:
            // End of transfer - keep returning 0xFF until next transfer
            response = 0xFF;
            sio_internal.controller_transfer_step = 4;  // Stay at 4 until reset
            SIO_DBG("[SIO_CTRL] Step 4+: Transfer complete, EOP");
            break;
    }

    SIO_DBG("[SIO_CTRL] Response: 0x%02x", response);
    return response;
}

// ============================================================================
// MEMORY CARD STUBS (Not implemented yet)
// ============================================================================

bool sio_load_memcard(MemoryCard* card, const char* filepath) {
    LOG_SYSTEM_WARN("[SYSTEM] Memory card loading not implemented");
    return false;
}

bool sio_save_memcard(MemoryCard* card) {
    LOG_SYSTEM_WARN("[SYSTEM] Memory card saving not implemented");
    return false;
}

void sio_create_memcard(MemoryCard* card, const char* filepath) {
    LOG_SYSTEM_WARN("[SYSTEM] Memory card creation not implemented");
}
