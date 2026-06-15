// SIO (PAD) Implementation based on DuckStation
// Implements PSX Serial I/O controller and memory card interface
// Based on DuckStation's pad.cpp: https://github.com/stenzek/duckstation

#include "sio.h"
#include "interconnect.h"
#include "event_scheduler.h"
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

    // Memory Cards (pointers into public Sio struct — set by sio_init)
    MemoryCard *mc1;
    MemoryCard *mc2;
    bool card_slot1_present;
    bool card_slot2_present;

    // Memory card transfer state machine
    uint8_t mc_step;           // step index (0xFF = sequence done)
    uint8_t mc_cmd;            // 0x52=Read 0x57=Write 0x53=GetID
    uint16_t mc_sector;        // sector address (0..0x3FF)
    uint8_t mc_byte_pos;       // position within 128-byte data block
    uint8_t mc_write_buf[128]; // accumulate write data
    uint8_t mc_checksum;       // running XOR over addr + data
    uint8_t mc_flag;           // FLAG byte: 0x08 on powerup, bit3 = unread-dir
    bool mc_write_ok;          // write checksum matched
    uint8_t mc_last_byte;      // last byte sent by host (for echo responses)

    // Pending events
    bool pending_irq;
    uint32_t pending_irq_type;  // 0=none, 1=TX, 2=RX, 3=ACK

    // Interconnect back-pointer (set by sio_set_interconnect after sio_init)
    struct Interconnect* inter;
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
static uint8_t sio_memcard_transfer(uint8_t tx_byte);
static bool sio_can_transfer(void);
static void sio_reset_device_transfer_state(void);

// ============================================================================
// INITIALIZATION
// ============================================================================

void sio_init(Sio* sio) {
    memset(sio, 0, sizeof(Sio));
    memset(&sio_internal, 0, sizeof(SioInternal));
    sio_internal.inter = NULL;  // set later by sio_set_interconnect

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

    // Wire memory card pointers to the public Sio struct
    sio_internal.mc1 = &sio->card_slot1;
    sio_internal.mc2 = &sio->card_slot2;

    // Initialize memory cards
    sio_internal.card_slot1_present = false;
    sio_internal.card_slot2_present = false;
    sio_internal.mc_step  = 0;
    sio_internal.mc_flag  = 0x08;  // "directory not read" on powerup
    sio_internal.mc_write_ok = false;

    // Copy to public structure
    sio->stat = sio_internal.stat;
    sio->mode = sio_internal.mode;
    sio->ctrl = sio_internal.ctrl;
    sio->baud = sio_internal.baud;
    sio->button_state = sio_internal.button_state;
    sio->controller_connected = sio_internal.controller_connected;

    LOG_SYSTEM_INFO("[SYSTEM] SIO initialized (DuckStation-style implementation)");
}

void sio_set_interconnect(Sio* sio, struct Interconnect* inter) {
    (void)sio;
    sio_internal.inter = inter;
}

// Called by EVQ_SIO event handler — executes the deferred byte transfer and
// delivers IRQ7 if enabled. Also reschedules for chained transfers.
void sio_execute_event(Sio* sio) {
    sio_do_transfer();

    // Deliver ACK IRQ if pending (set by sio_do_ack via sio_trigger_irq)
    if (sio_internal.pending_irq) {
        sio->pending_irq = true;
        sio_internal.pending_irq = false;
        if (sio_internal.inter) {
            LOG_SYSTEM_DEBUG("[SIO] EVQ_SIO: firing IRQ7 (CTRLMEMCARD)");
            interconnect_set_irq_line(sio_internal.inter, IRQ_CTRLMEMCARD, true);
            interconnect_set_irq_line(sio_internal.inter, IRQ_CTRLMEMCARD, false);
        }
    }
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
        case 0x04: {  // JOY_STAT (1F801044h)
            // ACK_INPUT (bit 7) is a momentary pulse — clear on read per DuckStation
            uint16_t stat = (uint16_t)sio_internal.stat;
            sio_internal.stat &= ~STAT_ACKINPUT;
            return stat;
        }

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

    if (sio_internal.pending_irq) {
        sio->pending_irq = true;
        sio_internal.pending_irq = false;
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

    if (sio_internal.pending_irq) {
        sio->pending_irq = true;
        sio_internal.pending_irq = false;
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

    // Defer transfer via event scheduler (DuckStation: BAUD*8 cycles delay).
    // This gives the BIOS time to clear stale I_STAT[7] and set up IRQ handlers
    // before the ACK IRQ arrives — required for IRQ-driven memory card access.
    if (sio_internal.inter) {
        uint32_t delay = (uint32_t)sio_internal.baud * 8;
        if (delay < 128) delay = 128;
        eventq_schedule(sio_internal.inter, EVQ_SIO, delay);
    } else {
        sio_do_transfer();  // fallback (no interconnect, e.g. unit tests)
    }
}

static void sio_do_transfer(void) {
    SIO_DBG("[SIO] DoTransfer");
    
    uint8_t data_out = sio_internal.transmit_value;
    uint8_t data_in = 0xFF;
    bool ack = false;

    // Try to transfer to active device
    if (sio_internal.active_device == ACTIVE_DEVICE_NONE) {
        if (data_out == 0x81 && sio_internal.card_slot1_present) {
            // Memory card device select
            sio_internal.active_device = ACTIVE_DEVICE_MEMCARD;
            sio_internal.mc_step = 0;
            data_in = 0xFF;  // N/A per PSX-SPX spec
            ack = true;
            SIO_DBG("[SIO] Memory card detected (0x81)");
        } else if (sio_internal.controller_connected) {
            const uint8_t prev_step = sio_internal.controller_transfer_step;
            data_in = sio_controller_transfer(data_out);
            // Button bytes can legitimately be 0xFF — ACK on step advance only
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
        ack = (sio_internal.controller_transfer_step != prev_step);
    } else if (sio_internal.active_device == ACTIVE_DEVICE_MEMCARD) {
        data_in = sio_memcard_transfer(data_out);
        ack = (sio_internal.mc_step != 0xFF);
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
    sio_internal.mc_step = 0;
    // Always reset controller step on CS deassert (matches DuckStation behavior)
    sio_internal.controller_transfer_step = 0;
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
// MEMORY CARD SPI TRANSFER PROTOCOL
// Implements PSX-SPX "Memory Card Read/Write Commands" byte-by-byte state machine
// ============================================================================

static uint8_t sio_memcard_transfer(uint8_t tx) {
    SioInternal* s = &sio_internal;
    MemoryCard* card = s->mc1;
    if (!card) return 0xFF;
    uint8_t resp = 0xFF;

    switch (s->mc_step) {
        case 0:  // Command byte: 0x52=Read, 0x57=Write, 0x53=GetID
            s->mc_cmd = tx;
            s->mc_checksum = 0;
            s->mc_byte_pos = 0;
            resp = s->mc_flag;
            s->mc_step++;
            LOG_SYSTEM_DEBUG("[MC] Command 0x%02x (flag=0x%02x)", tx, s->mc_flag);
            break;

        case 1: resp = 0x5A; s->mc_step++; break;  // ID1
        case 2: resp = 0x5D; s->mc_step++; break;  // ID2

        case 3:  // Addr MSB
            s->mc_sector = (uint16_t)(tx << 8);
            s->mc_checksum = tx;
            resp = 0x00;
            s->mc_last_byte = tx;
            s->mc_step++;
            break;

        case 4:  // Addr LSB — echo MSB back (DuckStation: m_last_byte)
            s->mc_sector |= tx;
            s->mc_checksum ^= tx;
            resp = s->mc_last_byte;  // echo MSBadr
            s->mc_last_byte = tx;
            s->mc_step++;
            break;

        default: {
            if (s->mc_cmd == 0x52) {
                // READ: steps 5..139
                int pos = (int)s->mc_step - 5;
                if (pos == 0) { resp = 0x5C; s->mc_step++; }          // CMD ack1
                else if (pos == 1) { resp = 0x5D; s->mc_step++; }     // CMD ack2
                else if (pos == 2) {                                    // confirmed MSB
                    resp = (s->mc_sector >> 8) & 0xFF;
                    s->mc_checksum = resp;
                    s->mc_step++;
                } else if (pos == 3) {                                  // confirmed LSB
                    resp = s->mc_sector & 0xFF;
                    s->mc_checksum ^= resp;
                    s->mc_step++;
                } else if (pos >= 4 && pos < 132) {                    // 128 data bytes
                    uint32_t off = (uint32_t)s->mc_sector * MEMCARD_SECTOR_SIZE + (pos - 4);
                    resp = (off < MEMCARD_SIZE) ? card->data[off] : 0xFF;
                    s->mc_checksum ^= resp;
                    s->mc_step++;
                } else if (pos == 132) {                                // checksum
                    resp = s->mc_checksum;
                    s->mc_step++;
                } else if (pos == 133) {                                // end byte 'G'
                    resp = 0x47;
                    s->mc_step = 0xFF;
                    LOG_SYSTEM_DEBUG("[MC] READ sector=%u complete chk=0x%02x", s->mc_sector, s->mc_checksum);
                }
            } else if (s->mc_cmd == 0x57) {
                // WRITE: steps 5..136
                int pos = (int)s->mc_step - 5;
                if (pos >= 0 && pos < 128) {                            // 128 data bytes
                    resp = s->mc_last_byte;  // echo previous host byte (DuckStation)
                    s->mc_write_buf[pos] = tx;
                    s->mc_checksum ^= tx;
                    s->mc_last_byte = tx;
                    s->mc_step++;
                } else if (pos == 128) {                                // host checksum
                    resp = s->mc_last_byte;  // echo last data byte (DuckStation)
                    s->mc_write_ok = (tx == s->mc_checksum) &&
                                     (s->mc_sector < MEMCARD_SECTORS);
                    LOG_SYSTEM_DEBUG("[MC] WRITE sector=%u chk_host=0x%02x chk_card=0x%02x %s",
                        s->mc_sector, tx, s->mc_checksum,
                        s->mc_write_ok ? "OK" : "CHKSUM_FAIL");
                    if (s->mc_write_ok) {
                        uint32_t off = (uint32_t)s->mc_sector * MEMCARD_SECTOR_SIZE;
                        memcpy(card->data + off, s->mc_write_buf, MEMCARD_SECTOR_SIZE);
                        card->dirty = true;
                        s->mc_flag &= ~0x08;
                    }
                    s->mc_step++;
                } else if (pos == 129) { resp = 0x5C; s->mc_step++; } // ack1
                else if (pos == 130) { resp = 0x5D; s->mc_step++; }   // ack2
                else if (pos == 131) {                                  // end byte
                    resp = s->mc_write_ok ? 0x47 : 0x4E;
                    s->mc_step = 0xFF;
                    LOG_SYSTEM_DEBUG("[MC] WRITE sector=%u result=%s", s->mc_sector,
                        s->mc_write_ok ? "GOOD(0x47)" : "BAD(0x4E)");
                    if (card->dirty) sio_save_memcard(card);
                }
            } else if (s->mc_cmd == 0x53) {
                // GET ID
                static const uint8_t id_resp[] = {0x5C, 0x5D, 0x04, 0x00, 0x00, 0x80};
                int pos = (int)s->mc_step - 5;
                if (pos >= 0 && pos < 6) {
                    resp = id_resp[pos];
                    s->mc_step = (pos == 5) ? 0xFF : s->mc_step + 1;
                }
            }
            break;
        }
    }

    SIO_DBG("[MC] step=%d cmd=0x%02x tx=0x%02x resp=0x%02x sector=%u",
            s->mc_step, s->mc_cmd, tx, resp, s->mc_sector);
    return resp;
}

// ============================================================================
// MEMORY CARD FILE I/O
// ============================================================================

bool sio_load_memcard(MemoryCard* card, const char* filepath) {
    strncpy(card->filepath, filepath, sizeof(card->filepath) - 1);
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        LOG_SYSTEM_INFO("[SIO] Memory card not found, creating: %s", filepath);
        sio_create_memcard(card, filepath);
        return true;
    }
    size_t n = fread(card->data, 1, MEMCARD_SIZE, f);
    fclose(f);
    if (n != MEMCARD_SIZE)
        memset(card->data + n, 0xFF, MEMCARD_SIZE - n);
    card->present = true;
    card->dirty = false;
    sio_internal.card_slot1_present = true;
    LOG_SYSTEM_INFO("[SIO] Memory card loaded: %s (%zu bytes)", filepath, n);
    return true;
}

bool sio_save_memcard(MemoryCard* card) {
    if (!card->present || !card->dirty) return true;
    FILE* f = fopen(card->filepath, "wb");
    if (!f) {
        LOG_SYSTEM_ERROR("[SIO] Memory card save failed: %s", card->filepath);
        return false;
    }
    fwrite(card->data, 1, MEMCARD_SIZE, f);
    fclose(f);
    card->dirty = false;
    LOG_SYSTEM_INFO("[SIO] Memory card saved: %s", card->filepath);
    return true;
}

void sio_create_memcard(MemoryCard* card, const char* filepath) {
    strncpy(card->filepath, filepath, sizeof(card->filepath) - 1);
    memset(card->data, 0x00, MEMCARD_SIZE);

    // Sector 0: header "MC" + 0x00×125 + XOR checksum at byte 127
    card->data[0] = 'M';
    card->data[1] = 'C';
    card->data[127] = 0x0E;  // XOR of 'M'^'C' = 0x0E

    // Sectors 1-15: free directory entries
    for (int i = 1; i <= 15; i++) {
        uint8_t* s = card->data + i * MEMCARD_SECTOR_SIZE;
        s[0]   = 0xA0;  // alloc_state: free
        s[8]   = 0xFF;
        s[9]   = 0xFF;
        s[127] = 0xA0;  // checksum: only s[0] is non-zero in range [0..126]
    }

    // Sectors 16-35: broken sector list (0xFFFFFFFF link, 0x00000000 padding)
    for (int i = 16; i <= 35; i++) {
        uint8_t* s = card->data + i * MEMCARD_SECTOR_SIZE;
        s[0] = 0xFF; s[1] = 0xFF; s[2] = 0xFF; s[3] = 0xFF;
        s[8] = 0xFF; s[9] = 0xFF;
    }

    card->present = true;
    card->dirty = true;
    sio_internal.card_slot1_present = true;
    sio_save_memcard(card);
    LOG_SYSTEM_INFO("[SIO] Memory card created: %s", filepath);
}
