/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
// SIO0 — the serial bus the controller and memory card ports hang off.
//
// Written against the hardware documentation:
//   DOCS/serialinterfacessio.md:8-108   TX_DATA / STAT / MODE / CTRL registers
//   DOCS/controllersandmemorycards.md:50-67   device addressing (01h pad, 81h card)
//   DOCS/controllersandmemorycards.md:127-178 /CS, SCK, /ACK signalling
//   DOCS/controllersandmemorycards.md:331-346 controller communication sequence
//   DOCS/controllersandmemorycards.md:2354-2400 memory card read/write/ID sequences
//
// The bus is modelled the way the documentation describes it rather than as an
// abstract transfer machine: /CS selects a port, the first byte after assertion
// addresses a device, each byte is a full-duplex shift, and the addressed device
// answers by pulling /ACK low to ask for another byte. When it stops pulsing
// /ACK the packet is over. Everything else here follows from those four facts.

#include "sio.h"
#include "interconnect.h"
#include "event_scheduler.h"
#include "log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define SIO_DBG(...) LOG_SYSTEM_TRACE(__VA_ARGS__)

// ============================================================================
// STATE MACHINE
// ============================================================================
/* Where the bus is in the byte cycle of DOCS/controllersandmemorycards.md:127-142:
 * idle with nothing on the wire, a byte being clocked out on SCK, or the byte
 * exchanged and the device holding /ACK low to request the next one. */
typedef enum {
    SIO_BUS_IDLE,
    SIO_BUS_SHIFTING,
    SIO_BUS_ACK
} SioBusPhase;

/* Which device answered the address byte (:50-67). Nothing is addressed until
 * the host sends one after asserting /CS. */
typedef enum {
    SIO_DEV_NONE,
    SIO_DEV_CONTROLLER,
    SIO_DEV_MEMCARD
} SioAddressedDevice;

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
#define CTRL_RESPONSE_ID_ANALOG 0x73 // Analog pad ID (normal analog mode, LED=red)
#define CTRL_RESPONSE_ID_STICK  0x53 // Analog stick ID ("flight mode", LED=green)
#define CTRL_RESPONSE_ID_CONFIG 0xF3 // Config-mode ID
#define CTRL_RESPONSE_READY    0x5A  // Controller ready
#define CTRL_NO_RESPONSE       0xFF  // No device connected

// Normal-mode commands (DOCS/controllersandmemorycards.md:1209-1215)
#define CTRL_CMD_READ_BUTTONS  0x42  // "B" Read buttons (+analog inputs in analog mode)
#define CTRL_CMD_CONFIG        0x43  // "C" Enter/Exit config mode

// Watchdog: entering config mode arms a timer that resets the pad to digital mode
// after ~1s without communication (DOCS/controllersandmemorycards.md:1274-1281).
// 1 second at the PSX system clock.
#define CTRL_WATCHDOG_CYCLES   (PSX_SYSCLK_HZ)

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
    SioBusPhase bus_phase;
    SioAddressedDevice addressed;
    bool txen_latched;   /* DOCS/serialinterfacessio.md:16-20 */
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

    // DualShock analog / config-mode state (DOCS/controllersandmemorycards.md:330-433,
    // :1202-1330). Analog pads boot in digital mode; analog mode is entered either by
    // software (config 43h/44h) or by the host-side analog toggle.
    bool analog_mode;          // analog inputs on: replies ID 0x73/0x53 and appends adc0-3
    bool stick_mode;           // with analog_mode: "flight mode", ID 0x53, L3/R3 disabled
                               // (DOCS/controllersandmemorycards.md:483-489). Meaningless on
                               // its own; analog_mode is what gates the adc bytes.
    bool config_mode;          // replies ID 0xF3, always-9-byte transfers, config commands
    bool transfer_in_config;   // latched at the command byte: the whole in-progress transfer
                               // replies in config style, even if config_mode flips mid-transfer
                               // (a config 43h xx=00 exits config but still answers with 00h bytes)
    bool analog_lock;          // set by config 44h Key=03h: ignore Analog-button toggles
    bool watchdog_active;      // armed on first entry into config mode; a ~1s comms gap
                               // resets the pad to digital mode (DOCS/controllersandmemorycards.md:1274-1281)
    uint32_t last_comms_cycle; // CPU cycle of the last controller transfer (watchdog)
    uint8_t right_x, right_y;  // right stick, adc0/adc1 (00h..FFh, 80h=centre)
    uint8_t left_x, left_y;    // left stick, adc2/adc3 (00h..FFh, 80h=centre)
    uint8_t cmd;               // command byte of the in-progress transfer
    uint8_t config_p1;         // buffered config parameter (Led/ii/xx, host send step 3)
    uint8_t config_p2;         // buffered config parameter (Key, host send step 4)

    // Rumble (SCPH-1200 DualShock, two motors; DOCS/controllersandmemorycards.md:1412-1478).
    // rumble_map[i] maps read-command byte (i+4) to a motor:
    //   0x00 = right/small motor M2 from bit0 of that byte
    //   0x01 = left/large motor M1 from bits0-7 of that byte
    //   0xFF = no motor (default: motors locked until config 4Dh unlocks them)
    uint8_t rumble_map[6];
    uint8_t rumble_m1;         // large motor level, 00h..FFh (analog slow/fast)
    uint8_t rumble_m2;         // small motor level, 00h or FFh (digital on/off)

    // Old rumble method, one motor, no config commands (SCPH-1150, and kept for
    // backwards compatibility on SCPH-1200/110) — DOCS/controllersandmemorycards.md:1428-1440.
    // The motor runs when the 42h read carries xx in 40h..7Fh (bit7=0, bit6=1) at
    // command byte 4 *and* yy with bit0=1 at byte 5; it drives the right/small
    // motor M2 only, digital on/off.
    //
    // "In the initial state, aa..ff are all FFh, and the controller does then use
    // the old rumble control method (with only one motor). However, that old
    // method gets disabled once when having messed with config commands" (:1478-1481).
    // The documentation does not say which config command disables it; 4Dh is the
    // one that redefines the protocol, so that is the trigger used here — entering
    // config mode merely to switch analog inputs on (44h) leaves old rumble working,
    // which is the reading that keeps the most titles vibrating.
    bool    rumble_config_used;  // a 4Dh has run: the old method is off for good
    uint8_t rumble_old_xx;       // byte 4 of the in-progress 42h read (the on/off gate)

    // Memory Cards (pointers into public Sio struct — set by sio_init).
    // Presence is tracked on MemoryCard.present itself (set by sio_load/create_memcard);
    // no separate slot1/slot2 "present" flags here — those were a redundant, buggy copy.
    MemoryCard *mc1;
    MemoryCard *mc2;
    MemoryCard *active_card;   // card targeted by the in-progress transfer (mc1 or mc2,
                               // selected via JOY_CTRL bit 13 / CTRL_SLOT when 0x81 is sent)

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
static void sio_start_shift(void);
static void sio_shift_byte(void);
static void sio_pulse_ack(void);
static void sio_release_bus(void);
static void sio_update_joystat(void);
static void sio_trigger_irq(const char* type);
static bool sio_controller_transfer(uint8_t tx_byte, uint8_t* out_byte);
static uint8_t sio_memcard_transfer(uint8_t tx_byte);
static bool sio_bus_ready(void);
static void sio_release_cs(void);
static void sio_capture_rumble(uint8_t tx_byte);

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
    sio_internal.bus_phase = SIO_BUS_IDLE;
    sio_internal.addressed = SIO_DEV_NONE;
    sio_internal.transmit_buffer_full = false;
    sio_internal.receive_buffer_full = false;

    // Initialize controller
    sio_internal.controller_connected = false;
    sio_internal.button_state = 0xFFFF;  // All buttons released
    sio_internal.controller_transfer_step = 0;
    // A real analog pad powers up in digital mode with its LED off
    // (DOCS/controllersandmemorycards.md:436) and the user presses the Analog
    // button. Powering up in analog instead — on the theory that a digital-only
    // game reads the same swlo/swhi bytes and just ignores adc0-3 — is not free:
    // the BIOS shell's pad driver does not cope with ID 73h. It never finishes
    // its init, and the main menu is drawn without its selection cursor (a
    // reference run of the same PAL BIOS answers the pad with 41h 5Ah and the
    // cursor is there). So the hardware default it is. ZS1_PAD_MODE=analog
    // restores the old behaviour, =stick selects the LED=green flight mode, and
    // F12 still cycles at runtime.
    // The config/lock/watchdog fields are zeroed by the memset above.
    sio_internal.analog_mode = false;
    sio_internal.stick_mode  = false;
    {
        const char* env = getenv("ZS1_PAD_MODE");
        if (env) {
            if (strcmp(env, "analog") == 0) {
                sio_internal.analog_mode = true;
            } else if (strcmp(env, "stick") == 0) {
                /* Stick mode is a flavour of analog, so it needs both flags —
                 * it used to set only stick_mode and rely on analog_mode already
                 * defaulting to true. */
                sio_internal.analog_mode = true;
                sio_internal.stick_mode  = true;
            } else if (strcmp(env, "digital") != 0) {
                LOG_SYSTEM_WARN("[SYSTEM] ZS1_PAD_MODE=%s not understood "
                                "(digital|analog|stick); using digital", env);
            }
        }
    }
    LOG_SYSTEM_INFO("[SYSTEM] Pad mode: %s",
                    sio_pad_mode_name(sio_internal.analog_mode
                                          ? (sio_internal.stick_mode ? SIO_PAD_STICK : SIO_PAD_ANALOG)
                                          : SIO_PAD_DIGITAL));
    // Rumble motors are locked until config 4Dh unlocks them (default: no motor
    // mapped onto any read-command byte).
    for (int i = 0; i < 6; i++) sio_internal.rumble_map[i] = 0xFF;

    // Wire memory card pointers to the public Sio struct
    sio_internal.mc1 = &sio->card_slot1;
    sio_internal.mc2 = &sio->card_slot2;
    sio_internal.active_card = NULL;

    // Initialize memory cards
    sio_internal.mc_step  = 0;
    sio_internal.mc_flag  = 0x08;  // "directory not read" on powerup
    sio_internal.mc_write_ok = false;

    // Copy to public structure
    sio->stat = sio_internal.stat;
    sio->mode = sio_internal.mode;
    sio->ctrl = sio_internal.ctrl;
    sio->baud = sio_internal.baud;
    sio->controller_connected = sio_internal.controller_connected;

    LOG_SYSTEM_INFO("[SYSTEM] SIO0 initialized");
}

void sio_set_interconnect(Sio* sio, struct Interconnect* inter) {
    (void)sio;
    sio_internal.inter = inter;
}

// Called by EVQ_SIO event handler — executes the deferred byte transfer and
// delivers IRQ7 if enabled. Also reschedules for chained transfers.
void sio_execute_event(Sio* sio) {
    sio_shift_byte();

    // Deliver ACK IRQ if pending (set by sio_pulse_ack via sio_trigger_irq)
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
    (void)sio;  /* sio_internal holds the register state — see SioInternal */
    switch (offset) {
        case 0x00: {  // JOY_DATA (1F801040h)
            // Return RX buffer
            uint8_t value = sio_internal.receive_buffer_full ? sio_internal.receive_buffer : 0xFF;
            SIO_DBG("[SIO] Read JOY_DATA = 0x%02x (full=%d, step=%d, device=%d)",
                value, sio_internal.receive_buffer_full, sio_internal.controller_transfer_step,
                sio_internal.addressed);
            
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
    (void)sio;  /* sio_internal holds the register state — see SioInternal */
    switch (offset) {
        case 0x04: {  // JOY_STAT (1F801044h)
            // STAT.7 mirrors the /ACK input, which the device holds low only for a
            // couple of microseconds (DOCS/controllersandmemorycards.md:167-169), so
            // it reads as a pulse: report it once, then let it go high again.
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
            /* Latch TXEN as of this write — see sio_bus_ready(). */
            sio_internal.txen_latched = (sio_internal.ctrl & CTRL_TXEN) != 0;

            // Fire TX interrupt if enabled
            if (sio_internal.ctrl & CTRL_TXINTEN) {
                sio_trigger_irq("TX");
            }

            // Start transfer if conditions allow
            if (sio_internal.bus_phase == SIO_BUS_IDLE && sio_bus_ready()) {
                sio_start_shift();
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
                sio_release_cs();
            }

            // Handle transfer enable/disable
            if (!(value & CTRL_SELECT) || !(value & CTRL_TXEN)) {
                if (sio_internal.bus_phase != SIO_BUS_IDLE) {
                    sio_release_bus();
                }
            } else {
                if (sio_internal.bus_phase == SIO_BUS_IDLE && sio_bus_ready()) {
                    sio_start_shift();
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
    (void)sio;  // sio_internal.button_state is the single source of truth; see SioInternal
    if (buttons != sio_internal.button_state) {
        static uint16_t last_logged = 0xFFFF;
        if (buttons != last_logged) {
            SIO_DBG("[SIO] Button state: 0x%04x (was 0x%04x)", buttons, sio_internal.button_state);
            last_logged = buttons;
        }
    }
    sio_internal.button_state = buttons;
}

void sio_set_controller_connected(Sio* sio, bool connected) {
    (void)sio;  // sio_internal.controller_connected is the single source of truth
    sio_internal.controller_connected = connected;
    LOG_SYSTEM_INFO("[SYSTEM] SIO: Controller %s", connected ? "connected" : "disconnected");
}

/**
 * Feed the host sticks into the SIO analog bytes. The values are the raw
 * -32768..32767 SDL axis positions from Controller; they map onto the 8-bit
 * adc0-3 range (00h=min, 80h=centre, FFh=max) as (v + 32768) >> 8.
 */
void sio_set_analog_state(Sio* sio, int16_t lx, int16_t ly, int16_t rx, int16_t ry) {
    (void)sio;
    sio_internal.left_x  = (uint8_t)(((int32_t)lx + 32768) >> 8);
    sio_internal.left_y  = (uint8_t)(((int32_t)ly + 32768) >> 8);
    sio_internal.right_x = (uint8_t)(((int32_t)rx + 32768) >> 8);
    sio_internal.right_y = (uint8_t)(((int32_t)ry + 32768) >> 8);
}

/* Put the rumble motors back to their power-on state: nothing mapped, both
 * motors stopped, and the old one-motor method available again. Shared by the
 * two events the documentation describes as resetting rumble — the watchdog
 * timeout and a press of the Analog button. */
static void sio_rumble_lock(void) {
    for (int i = 0; i < 6; i++) sio_internal.rumble_map[i] = 0xFF;
    sio_internal.rumble_m1 = 0x00;
    sio_internal.rumble_m2 = 0x00;
    sio_internal.rumble_old_xx = 0x00;
    sio_internal.rumble_config_used = false;
}

/**
 * Software side of the pad's Analog button: switch the emulated pad between
 * digital (ID 0x41) and analog (ID 0x73) mode. Ignored while the pad is locked
 * (config 44h Key=03h) — the same way the hardware ignores the Analog button.
 *
 * Pressing the button is not just a mode flip. DOCS/controllersandmemorycards.md:1283-1285
 * (Caution 2): "A similar reset occurs when the user pushes the Analog button;
 * this is causing rumble motors to be stopped and locked, and of course, the
 * analog/digital state gets changed." So a press lands the same rumble reset the
 * watchdog does.
 */
void sio_set_analog_mode(Sio* sio, bool analog) {
    sio_set_pad_mode(sio, analog ? SIO_PAD_ANALOG : SIO_PAD_DIGITAL);
}

bool sio_get_analog_mode(Sio* sio) {
    (void)sio;
    return sio_internal.analog_mode;
}

const char* sio_pad_mode_name(SioPadMode mode) {
    switch (mode) {
        case SIO_PAD_ANALOG: return "ANALOG (ID 73h, LED red)";
        case SIO_PAD_STICK:  return "STICK (ID 53h, LED green)";
        default:             return "DIGITAL (ID 41h, LED off)";
    }
}

SioPadMode sio_get_pad_mode(Sio* sio) {
    (void)sio;
    if (!sio_internal.analog_mode) return SIO_PAD_DIGITAL;
    return sio_internal.stick_mode ? SIO_PAD_STICK : SIO_PAD_ANALOG;
}

/* Same button press as sio_set_analog_mode() above, over all three modes. The
 * rumble reset is taken on any change of mode, not only on leaving digital:
 * a press is a press as far as the pad's own reset is concerned. */
void sio_set_pad_mode(Sio* sio, SioPadMode mode) {
    if (sio_internal.analog_lock) return;
    if (sio_get_pad_mode(sio) == mode) return;

    sio_internal.analog_mode = (mode != SIO_PAD_DIGITAL);
    sio_internal.stick_mode  = (mode == SIO_PAD_STICK);
    sio_rumble_lock();
    LOG_SYSTEM_INFO("[SIO] Pad mode -> %s (rumble stopped and locked)",
                    sio_pad_mode_name(mode));
}

void sio_cycle_pad_mode(Sio* sio) {
    SioPadMode next = (SioPadMode)((sio_get_pad_mode(sio) + 1) % 3);
    sio_set_pad_mode(sio, next);
    if (sio_internal.analog_lock)
        LOG_SYSTEM_INFO("[SIO] Analog button ignored: the game locked the pad mode (44h Key=03h)");
}

/**
 * Read the current rumble motor levels: m1 = large motor 00h..FFh, m2 = small
 * motor 00h or FFh (digital). Both 0 means "motors off". The host side maps
 * these onto SDL_GameControllerRumble.
 */
void sio_get_rumble(Sio* sio, uint8_t* m1, uint8_t* m2) {
    (void)sio;
    if (m1) *m1 = sio_internal.rumble_m1;
    if (m2) *m2 = sio_internal.rumble_m2;
}

// ============================================================================
// SAVESTATE ACCESS
// ============================================================================

size_t sio_internal_state_size(void) {
    return sizeof(SioInternal);
}

void sio_save_internal_state(void* dst) {
    memcpy(dst, &sio_internal, sizeof(SioInternal));
}

void sio_load_internal_state(const void* src) {
    /* The four pointers name objects that belong to this process — the
     * interconnect and the two memory-card slots inside the public Sio struct.
     * The values in the file are whatever those addresses happened to be when
     * the state was written, so they are held across the copy and put back,
     * exactly as savestate.c does for the renderer and the disc handles.
     *
     * active_card is derived rather than kept: it points at mc1 or mc2, so the
     * saved value has to be re-aimed at the corresponding live slot, and a state
     * written mid-transfer would otherwise resume against a dangling card. */
    struct Interconnect* inter = sio_internal.inter;
    MemoryCard* mc1 = sio_internal.mc1;
    MemoryCard* mc2 = sio_internal.mc2;

    /* Which slot active_card pointed at is read from the incoming state, by
     * comparing it against that state's own mc1/mc2 — the saved addresses are
     * only meaningful relative to each other. */
    const SioInternal* in = (const SioInternal*)src;
    bool active_was_set = (in->active_card != NULL);
    bool active_was_mc2 = (in->active_card == in->mc2);

    memcpy(&sio_internal, src, sizeof(SioInternal));

    sio_internal.inter = inter;
    sio_internal.mc1   = mc1;
    sio_internal.mc2   = mc2;
    sio_internal.active_card = active_was_set ? (active_was_mc2 ? mc2 : mc1) : NULL;
}

// ============================================================================
// BUS SEQUENCING — one byte of DOCS/controllersandmemorycards.md:127-142
// ============================================================================

/* DOCS/serialinterfacessio.md:16-20: writing TX_DATA latches TXEN, and the
 * transfer starts if the current TXEN value OR the latched one is set — so
 * clearing TXEN after the write does not cancel a transfer that the write
 * already armed. The documentation names Wipeout 2097 as the title that depends
 * on it. /CS must also be asserted (CTRL.1, :91-93). */
static bool sio_bus_ready(void) {
    return sio_internal.transmit_buffer_full &&
           (sio_internal.ctrl & CTRL_SELECT) &&
           ((sio_internal.ctrl & CTRL_TXEN) || sio_internal.txen_latched);
}

static void sio_soft_reset(void) {
    if (sio_internal.bus_phase != SIO_BUS_IDLE) {
        sio_release_bus();
    }

    sio_internal.ctrl = 0;
    sio_internal.stat = STAT_TXRDY | STAT_TXDONE;
    sio_internal.mode = 0;
    sio_internal.receive_buffer = 0;
    sio_internal.receive_buffer_full = false;
    sio_internal.transmit_buffer = 0;
    sio_internal.transmit_buffer_full = false;
    sio_internal.txen_latched = false;
    sio_release_cs();
    sio_update_joystat();
    SIO_DBG("[SIO] Soft reset");
}

static void sio_start_shift(void) {
    SIO_DBG("[SIO] BeginTransfer");

    if (sio_internal.bus_phase != SIO_BUS_IDLE || !sio_bus_ready()) {
        return;
    }

    sio_internal.bus_phase = SIO_BUS_SHIFTING;
    sio_internal.ctrl |= CTRL_RXEN;
    sio_internal.transmit_value = sio_internal.transmit_buffer;
    sio_internal.transmit_buffer_full = false;
    sio_internal.txen_latched = false;   /* consumed by this transfer */

    // A byte is 8 SCK periods, and SCK derives from the baud reload value
    // (DOCS/serialinterfacessio.md:105-113), so the exchange is scheduled rather
    // than immediate.
    // This gives the BIOS time to clear stale I_STAT[7] and set up IRQ handlers
    // before the ACK IRQ arrives — required for IRQ-driven memory card access.
    if (sio_internal.inter) {
        uint32_t delay = (uint32_t)sio_internal.baud * 8;
        if (delay < 128) delay = 128;
        eventq_schedule(sio_internal.inter, EVQ_SIO, delay);
    } else {
        sio_shift_byte();  // fallback (no interconnect, e.g. unit tests)
    }
}

static void sio_shift_byte(void) {
    SIO_DBG("[SIO] DoTransfer");
    
    uint8_t data_out = sio_internal.transmit_value;
    uint8_t data_in = 0xFF;
    bool ack = false;

    // Try to transfer to active device
    if (sio_internal.addressed == SIO_DEV_NONE) {
        // Memory card device select. JOY_CTRL bit 13 (CTRL_SLOT) picks which
        // physical port's card this transfer targets — mc1 when clear, mc2 when set.
        MemoryCard* target_card = (sio_internal.ctrl & CTRL_SLOT) ? sio_internal.mc2 : sio_internal.mc1;
        if (data_out == 0x81 && target_card && target_card->present) {
            sio_internal.addressed = SIO_DEV_MEMCARD;
            sio_internal.active_card = target_card;
            sio_internal.mc_step = 0;
            data_in = 0xFF;  // N/A per PSX-SPX spec
            ack = true;
            SIO_DBG("[SIO] Memory card detected (0x81, slot=%d)", (sio_internal.ctrl & CTRL_SLOT) ? 2 : 1);
        } else if (sio_internal.controller_connected && !(sio_internal.ctrl & CTRL_SLOT)) {
            /* Port 2 is empty. The two ports are wired in parallel and narrowed
             * by the address byte (:50-57), so a pad that answered whichever
             * slot happened to be selected put the same controller in both
             * ports — and the BIOS probes both. */
            ack = sio_controller_transfer(data_out, &data_in);
            if (ack) {
                sio_internal.addressed = SIO_DEV_CONTROLLER;
                SIO_DBG("[SIO] Controller detected");
            }
        } else {
            ack = false;
        }
    } else if (sio_internal.addressed == SIO_DEV_CONTROLLER) {
        ack = sio_controller_transfer(data_out, &data_in);
    } else if (sio_internal.addressed == SIO_DEV_MEMCARD) {
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
        sio_internal.addressed = SIO_DEV_NONE;
        sio_release_bus();
    } else {
        // The device asks for another byte by pulling /ACK low for at least 2 us
        // (DOCS/controllersandmemorycards.md:167-168). The pulse is raised here
        // rather than after a timer: the kernel only observes /ACK through the
        // interrupt, so nothing the guest can see distinguishes the two.
        sio_internal.bus_phase = SIO_BUS_ACK;
        sio_pulse_ack();
    }

    sio_update_joystat();
}

static void sio_pulse_ack(void) {
    SIO_DBG("[SIO] DoACK");
    
    sio_internal.stat |= STAT_ACKINPUT;

    // Fire ACK interrupt if enabled
    if (sio_internal.ctrl & CTRL_ACKINTEN) {
        sio_trigger_irq("ACK");
    }

    sio_release_bus();
    sio_update_joystat();

    // Chain next transfer if possible
    if (sio_bus_ready()) {
        sio_start_shift();
    }
}

static void sio_release_bus(void) {
    SIO_DBG("[SIO] EndTransfer");
    
    if (sio_internal.bus_phase == SIO_BUS_IDLE) {
        return;  // Already idle
    }

    sio_internal.bus_phase = SIO_BUS_IDLE;
}

static void sio_release_cs(void) {
    SIO_DBG("[SIO] ResetDeviceTransferState (step=%d)", sio_internal.controller_transfer_step);

    sio_internal.addressed = SIO_DEV_NONE;
    sio_internal.active_card = NULL;
    sio_internal.mc_step = 0;
    // Deasserting /CS ends the packet, so the next assertion starts a fresh one
    // beginning with an address byte (DOCS/controllersandmemorycards.md:54-57).
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

    if (!sio_internal.transmit_buffer_full && sio_internal.bus_phase == SIO_BUS_IDLE) {
        sio_internal.stat |= STAT_TXDONE;
    }
}

static void sio_trigger_irq(const char* type) {
    sio_internal.stat |= STAT_INTR;
    sio_internal.pending_irq = true;
    SIO_DBG("[SIO] IRQ: %s", type);
}

// ============================================================================
// CONTROLLER PROTOCOL (Digital Pad + DualShock analog/config)
// ============================================================================

// Communication sequence (DOCS/controllersandmemorycards.md:330-347): the pad
// answers "idlo idhi swlo swhi" (digital) and appends "adc0..adc3" in analog
// mode. The command byte is received at transfer step 1; reply bytes follow at
// steps 2..8, the last one sent without /ACK (EOP). Config-mode transfers are
// always 9 bytes with ID "F3h 5Ah" (DOCS/controllersandmemorycards.md:1236).

// Reply byte for the current step of a config-mode transfer (steps 3..8, after
// the "F3h 5Ah" header). The host's command parameters stream in during the
// same transfer (send step 3 = Led/ii/xx, send step 4 = Key), so state is
// updated here as each byte lands, before the byte that depends on it is sent.
// Command/response layouts: DOCS/controllersandmemorycards.md:1289-1396.
// The button word as the pad reports it. Stick ("flight") mode disables the two
// joystick buttons — "Left/right joy-buttons disabled (as for real analog stick,
// bits are always 1)" (DOCS/controllersandmemorycards.md:486). The word is
// active-low, so disabling them means forcing the bits to 1.
static uint16_t sio_pad_buttons(void) {
    uint16_t buttons = sio_internal.button_state;
    if (sio_internal.stick_mode)
        buttons |= (1u << 1) | (1u << 2);  // L3, R3
    return buttons;
}

static uint8_t sio_config_reply(uint8_t tx_byte) {
    SioInternal* s = &sio_internal;
    uint8_t step = s->controller_transfer_step;

    switch (s->cmd) {
        case 0x42:  // read buttons + analog inputs (even in digital mode)
            // The config-mode send is "01h 42h 00h M2 M1 00h 00h 00h 00h"
            // (DOCS/controllersandmemorycards.md:1289-1292): the motor bytes ride
            // along here exactly as they do in normal mode.
            sio_capture_rumble(tx_byte);
            switch (step) {
                case 3: return (uint8_t)(sio_pad_buttons() & 0xFF);        // swlo
                case 4: return (uint8_t)((sio_pad_buttons() >> 8) & 0xFF); // swhi
                case 5: return s->right_x;                               // adc0
                case 6: return s->right_y;                               // adc1
                case 7: return s->left_x;                                // adc2
                case 8: return s->left_y;                                // adc3
            }
            return 0x00;

        case 0x43:  // exit (xx=00) / stay (xx=01) in config mode
            if (step == 3) {
                s->config_mode = (tx_byte == 0x01);
                LOG_SYSTEM_DEBUG("[SIO_CTRL] Config mode %s", s->config_mode ? "stay" : "exit");
            }
            return 0x00;

        case 0x44:  // set LED state (analog mode on/off) + Analog-button lock
            if (step == 3) {
                s->config_p1 = tx_byte;  // Led: 00=digital/LED off, 01=analog/LED on,
                                         // 02..FF ignored (DOCS/controllersandmemorycards.md:1316-1321)
                if (tx_byte == 0x00 || tx_byte == 0x01) {
                    s->analog_mode = (tx_byte == 0x01);
                    // The software switch is the DualShock's red LED; nothing in the
                    // config command set selects the Dual Analog's green flight mode,
                    // so a game driving 44h always lands on the pad, not the stick.
                    s->stick_mode = false;
                }
            } else if (step == 4) {
                s->config_p2 = tx_byte;  // Key: 00..02 unlock, 03 lock, others AND 03
                s->analog_lock = ((tx_byte & 0x03) == 0x03);
            }
            return 0x00;  // Err byte: 00h for an analog pad (only DS2 returns FFh)

        case 0x45:  // get LED state + type/constants
            switch (step) {
                case 3: return 0x01;                       // Typ: PSX/Analog Pad
                case 4: return 0x02;
                case 5: return s->analog_mode ? 0x01 : 0x00;  // Led
                case 6: return 0x02;
                case 7: return 0x01;
                case 8: return 0x00;
            }
            return 0x00;

        case 0x46:  // get variable response A (PadInfoAct)
            if (step == 3) s->config_p1 = tx_byte;  // ii
            switch (step) {
                case 5: return (s->config_p1 == 0x00 || s->config_p1 == 0x01) ? 0x01 : 0x00;
                case 6: return (s->config_p1 == 0x00) ? 0x02 : 0x00;
                case 7: return (s->config_p1 == 0x01) ? 0x01 : 0x00;
                case 8: return (s->config_p1 == 0x00) ? 0x0A : (s->config_p1 == 0x01 ? 0x14 : 0x00);
            }
            return 0x00;

        case 0x47:  // fixed response
            switch (step) {
                case 5: return 0x02;
                case 7: return 0x01;
            }
            return 0x00;

        case 0x48:  // unknown
            if (step == 3) s->config_p1 = tx_byte;  // ii
            return (step == 7 && s->config_p1 <= 1) ? 0x01 : 0x00;

        case 0x4C:  // get variable response B
            if (step == 3) s->config_p1 = tx_byte;  // ii
            if (step == 6) {
                if (s->config_p1 == 0x00) return 0x04;
                if (s->config_p1 == 0x01) return 0x07;
                return 0x00;
            }
            return 0x00;

        case 0x4D:  // get/set rumble protocol (DOCS/controllersandmemorycards.md:1460-1478)
            // Reply returns the OLD map value for each position; the incoming
            // byte becomes the new map entry. The standard unlock is
            // "01 4Dh 00h 00h 01h FFh FFh FFh FFh" (M2→byte4 bit0, M1→byte5).
            if (step >= 3 && step <= 8) {
                uint8_t old = s->rumble_map[step - 3];
                s->rumble_map[step - 3] = tx_byte;
                s->rumble_config_used = true;  /* the old method is off from here on */
                if (s->rumble_map[step - 3] == 0xFF) {  // unmapped: drop any level
                    if (step - 3 == 0) s->rumble_m2 = 0x00;
                    else if (step - 3 == 1) s->rumble_m1 = 0x00;
                }
                return old;
            }
            return 0x00;

        default:  // 40h/41h/49h/4Ah/4Bh/4Eh/4Fh: unused, reply with 00h bytes
            return 0x00;
    }
}

// Is the pad still on the old one-motor rumble method? True until a 4Dh has run,
// and only while the map is untouched (all FFh) — the two conditions the
// documentation gives at DOCS/controllersandmemorycards.md:1478-1481.
static bool sio_rumble_old_method(void) {
    if (sio_internal.rumble_config_used) return false;
    for (int i = 0; i < 6; i++)
        if (sio_internal.rumble_map[i] != 0xFF) return false;
    return true;
}

// Decode a rumble byte arriving on a 42h read, in either normal or config mode
// (the config-mode 42h send is "01h 42h 00h M2 M1 00h 00h 00h 00h",
// DOCS/controllersandmemorycards.md:1289-1292, so it carries the motor bytes too).
//
// New method: each read-command byte (send step 3..8 = command byte 4..9) is
// mapped by rumble_map to a motor — 0x00 takes M2 from bit0, 0x01 takes M1 from
// the whole byte, 0xFF maps nothing (:1462-1470).
//
// Old method: bytes 4 and 5 are a two-part gate on the small motor alone
// (:1434-1440), so byte 4 is only latched here and the decision is taken at
// byte 5, once both halves are known.
static void sio_capture_rumble(uint8_t tx_byte) {
    uint8_t step = sio_internal.controller_transfer_step;
    if (step < 3 || step > 8) return;

    if (sio_rumble_old_method()) {
        if (step == 3) {
            sio_internal.rumble_old_xx = tx_byte;
        } else if (step == 4) {
            bool xx_on = (sio_internal.rumble_old_xx & 0xC0u) == 0x40u;  /* bit7=0, bit6=1 */
            bool yy_on = (tx_byte & 0x01u) != 0;
            sio_internal.rumble_m2 = (xx_on && yy_on) ? 0xFFu : 0x00u;
            sio_internal.rumble_m1 = 0x00u;   /* the old method drives one motor */
        }
        return;
    }

    switch (sio_internal.rumble_map[step - 3]) {
        case 0x00:  sio_internal.rumble_m2 = (tx_byte & 1) ? 0xFF : 0x00; break;
        case 0x01:  sio_internal.rumble_m1 = tx_byte; break;
        default:    break;  // 0xFF or unknown: no motor on this byte
    }
}

static bool sio_controller_transfer(uint8_t tx_byte, uint8_t* out_byte) {
    bool ack = false;

    // Watchdog (DOCS/controllersandmemorycards.md:1274-1281): entering config
    // mode arms a ~1s timer that resets the pad to digital mode after a comms
    // gap. Enforced lazily on the next byte after the gap — the only point the
    // reset is observable. The uint32 difference is wrap-safe (cpu_cycle_counter
    // is a 32-bit monotonic counter).
    if (sio_internal.watchdog_active) {
        uint32_t now = sio_internal.inter ? sio_internal.inter->cpu_cycle_counter : 0;
        if ((now - sio_internal.last_comms_cycle) > CTRL_WATCHDOG_CYCLES) {
            LOG_SYSTEM_DEBUG("[SIO_CTRL] Watchdog reset: ~1s without communication");
            sio_internal.config_mode = false;
            sio_internal.analog_mode = false;
            sio_internal.stick_mode  = false;
            sio_internal.analog_lock = false;
            // The reset "disables and locks rumble motors" — back to the
            // locked default (nothing mapped), motors off.
            sio_rumble_lock();
            // Disarm. The timer is armed by entering config mode (:1275-1277) and
            // survives an Exit Config, but nothing re-arms it after it has fired:
            // the pad it left behind is a plain digital one. Leaving it armed made
            // every later comms gap re-run the reset, which silently undid the
            // host-side Analog toggle over and over.
            sio_internal.watchdog_active = false;
        }
    }
    if (sio_internal.inter)
        sio_internal.last_comms_cycle = sio_internal.inter->cpu_cycle_counter;

    SIO_DBG("[SIO_CTRL] Transfer step %d, TX=0x%02x, buttons=0x%04x%s%s",
            sio_internal.controller_transfer_step, tx_byte, sio_internal.button_state,
            sio_internal.config_mode ? ", config" : "",
            sio_internal.analog_mode ? ", analog" : "");

    switch (sio_internal.controller_transfer_step) {
        case 0:
            // Idle: only 0x01 (select controller) advances the state machine
            *out_byte = 0xFF;
            if (tx_byte == 0x01) {
                sio_internal.cmd = 0;
                sio_internal.controller_transfer_step = 1;
                ack = true;
                SIO_DBG("[SIO_CTRL] Step 0->1: Received 0x01 (select)");
            }
            break;

        case 1:
            // Command byte. In config mode, 0x40..0x4F all answer with ID 0xF3;
            // in normal mode, only 0x42 (read) and 0x43 (enter/exit config)
            // advance, answering with the digital/analog ID.
            sio_internal.cmd = tx_byte;
            if (tx_byte >= 0x40 && tx_byte <= 0x4F && sio_internal.config_mode) {
                *out_byte = CTRL_RESPONSE_ID_CONFIG;
                sio_internal.transfer_in_config = true;
                sio_internal.controller_transfer_step = 2;
                ack = true;
            } else if (tx_byte == CTRL_CMD_READ_BUTTONS || tx_byte == CTRL_CMD_CONFIG) {
                *out_byte = !sio_internal.analog_mode ? CTRL_RESPONSE_ID
                          : sio_internal.stick_mode   ? CTRL_RESPONSE_ID_STICK
                                                      : CTRL_RESPONSE_ID_ANALOG;
                sio_internal.transfer_in_config = false;
                sio_internal.controller_transfer_step = 2;
                ack = true;
            } else {
                *out_byte = 0xFF;
                SIO_DBG("[SIO_CTRL] Step 1: Unexpected command 0x%02x", tx_byte);
            }
            break;

        case 2:
            *out_byte = CTRL_RESPONSE_READY;  // 0x5A (ID high)
            sio_internal.controller_transfer_step = 3;
            ack = true;
            SIO_DBG("[SIO_CTRL] Step 2->3: Sending ID high 0x5A");
            break;

        case 3:
        default: {
            if (sio_internal.transfer_in_config) {
                *out_byte = sio_config_reply(tx_byte);
                sio_internal.controller_transfer_step++;
                if (sio_internal.controller_transfer_step > 8) {
                    sio_internal.controller_transfer_step = 0;  // packet complete
                    ack = false;  // no ack on final byte — signals end of packet
                } else {
                    ack = true;
                }
                break;
            }

            // Normal mode: a 43h command carries the enter/stay byte at send step 3
            // (00h=stay normal, 01h=enter config). Reply data is the same as a 42h
            // read either way (DOCS/controllersandmemorycards.md:1261-1273).
            if (sio_internal.cmd == CTRL_CMD_CONFIG &&
                sio_internal.controller_transfer_step == 3 && tx_byte == 0x01) {
                sio_internal.config_mode = true;
                sio_internal.watchdog_active = true;
                LOG_SYSTEM_DEBUG("[SIO_CTRL] Entered config mode");
            }

            // Rumble: a normal-mode 42h read carries the motor bytes at send steps
            // 3..8, mapped onto M1/M2 via the 4Dh-unlocked map. Config-mode reads
            // do not control rumble (DOCS/controllersandmemorycards.md:1308-1309).
            if (sio_internal.cmd == CTRL_CMD_READ_BUTTONS)
                sio_capture_rumble(tx_byte);

            if (sio_internal.analog_mode) {
                switch (sio_internal.controller_transfer_step) {
                    case 3:
                        *out_byte = (uint8_t)(sio_pad_buttons() & 0xFF);  // swlo
                        sio_internal.controller_transfer_step = 4;
                        ack = true;
                        break;
                    case 4:
                        *out_byte = (uint8_t)((sio_pad_buttons() >> 8) & 0xFF);  // swhi
                        sio_internal.controller_transfer_step = 5;
                        ack = true;
                        break;
                    case 5:
                        *out_byte = sio_internal.right_x;  // adc0 RightJoyX
                        sio_internal.controller_transfer_step = 6;
                        ack = true;
                        break;
                    case 6:
                        *out_byte = sio_internal.right_y;  // adc1 RightJoyY
                        sio_internal.controller_transfer_step = 7;
                        ack = true;
                        break;
                    case 7:
                        *out_byte = sio_internal.left_x;  // adc2 LeftJoyX
                        sio_internal.controller_transfer_step = 8;
                        ack = true;
                        break;
                    case 8:
                    default:
                        *out_byte = sio_internal.left_y;  // adc3 LeftJoyY (EOP)
                        sio_internal.controller_transfer_step = 0;
                        ack = false;
                        break;
                }
            } else {
                switch (sio_internal.controller_transfer_step) {
                    case 3:
                        *out_byte = (uint8_t)(sio_pad_buttons() & 0xFF);  // swlo
                        sio_internal.controller_transfer_step = 4;
                        ack = true;
                        break;
                    case 4:
                    default:
                        *out_byte = (uint8_t)((sio_pad_buttons() >> 8) & 0xFF);  // swhi (EOP)
                        sio_internal.controller_transfer_step = 0;
                        ack = false;
                        break;
                }
            }
            break;
        }
    }

    SIO_DBG("[SIO_CTRL] Response: 0x%02x ack=%d", *out_byte, ack);
    return ack;
}

// ============================================================================
// MEMORY CARD SPI TRANSFER PROTOCOL
// Implements PSX-SPX "Memory Card Read/Write Commands" byte-by-byte state machine
// ============================================================================

static uint8_t sio_memcard_transfer(uint8_t tx) {
    SioInternal* s = &sio_internal;
    MemoryCard* card = s->active_card;
    if (!card) return 0xFF;
    uint8_t resp = 0xFF;

    switch (s->mc_step) {
        case 0:  // Command byte: 0x52=Read, 0x57=Write, 0x53=GetID
            s->mc_cmd = tx;
            s->mc_checksum = 0;
            s->mc_byte_pos = 0;
            resp = s->mc_flag;
            LOG_SYSTEM_DEBUG("[MC] Command 0x%02x (flag=0x%02x)", tx, s->mc_flag);
            /* "Transfer aborts immediately after the faulty command byte"
             * (psx-spx-docs/docs/controllersandmemorycards.md:2409-2415). Without
             * this the card went on acknowledging a command it was never going to
             * answer, so the host waited on a device that would not let go of the
             * bus. */
            if (tx != 0x52 && tx != 0x57 && tx != 0x53) {
                s->mc_step = 0xFF;
                LOG_SYSTEM_DEBUG("[MC] Invalid command 0x%02x — aborting transfer", tx);
                break;
            }
            s->mc_step++;
            break;

        case 1: resp = 0x5A; s->mc_step++; break;  // ID1
        case 2:
            resp = 0x5D;                            // ID2
            /* Get ID takes no address byte-pair: after ID2 the card answers 5Ch,
             * 5Dh, 04h, 00h, 00h, 80h
             * (psx-spx-docs/docs/controllersandmemorycards.md:2386-2397), while
             * Read and Write send the sector number first (:2360-2361,
             * :2380-2381). Running every command through the address steps
             * shifted the whole Get ID reply by two bytes: the host read 00h and
             * the echoed address byte where the two acknowledge bytes belong,
             * which reads as a card that failed to identify itself — that is, as
             * no card at all. */
            s->mc_step = (s->mc_cmd == 0x53) ? 5 : 3;
            break;

        case 3:  // Addr MSB
            s->mc_sector = (uint16_t)(tx << 8);
            s->mc_checksum = tx;
            resp = 0x00;
            s->mc_last_byte = tx;
            s->mc_step++;
            break;

        case 4:  // Addr LSB — reply is "(pre)", the previously sent byte
                 // (DOCS/controllersandmemorycards.md:2361)
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
                    /* An out-of-range sector answers FFFFh as the confirmed
                     * address and then aborts — no data, no checksum, no end
                     * flag. That is the original Sony card
                     * (psx-spx-docs/docs/controllersandmemorycards.md:2371-2375);
                     * third-party cards mask to 3FFh instead, and the console's
                     * own driver is written against the Sony behaviour. */
                    bool bad_sector = (s->mc_sector >= MEMCARD_SECTORS);
                    resp = bad_sector ? 0xFF : (uint8_t)((s->mc_sector >> 8) & 0xFF);
                    s->mc_checksum = resp;
                    s->mc_step++;
                } else if (pos == 3) {                                  // confirmed LSB
                    if (s->mc_sector >= MEMCARD_SECTORS) {
                        resp = 0xFF;
                        s->mc_step = 0xFF;                              // abort
                        LOG_SYSTEM_DEBUG("[MC] READ sector=%u out of range — FFFFh, abort",
                                         s->mc_sector);
                        break;
                    }
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
                    resp = s->mc_last_byte;  // "(pre)" — DOCS:2385-2387
                    s->mc_write_buf[pos] = tx;
                    s->mc_checksum ^= tx;
                    s->mc_last_byte = tx;
                    s->mc_step++;
                } else if (pos == 128) {                                // host checksum
                    resp = s->mc_last_byte;  // "(pre)" — DOCS:2385-2387
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
    LOG_SYSTEM_INFO("[SIO] Memory card loaded: %s (%zu bytes)", filepath, n);
    return true;
}

/* Copy the card file aside once per session, the first time this process is
 * about to overwrite it.
 *
 * A memory card is the one file here that holds something the user cannot
 * regenerate, and a save rewrites all 128 KB of it — every boot writes frame 63
 * as its write test, so the rewrite happens in the first twenty seconds of
 * every run whether or not the guest saved anything. One bad in-memory card is
 * therefore one boot away from replacing a card full of saves. The .bak is what
 * makes that recoverable; it is taken before the first write and not touched
 * again, so it always holds the card as this session found it. */
static void memcard_backup_once(MemoryCard* card) {
    if (card->backed_up) return;
    card->backed_up = true;

    FILE* src = fopen(card->filepath, "rb");
    if (!src) return;                       /* nothing to lose yet */

    char bak[sizeof(card->filepath) + 8];
    snprintf(bak, sizeof(bak), "%s.bak", card->filepath);
    FILE* dst = fopen(bak, "wb");
    if (!dst) { fclose(src); return; }

    static uint8_t buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
        fwrite(buf, 1, n, dst);
    fclose(src);
    fclose(dst);
    LOG_SYSTEM_INFO("[SIO] Memory card backed up: %s", bak);
}

bool sio_save_memcard(MemoryCard* card) {
    if (!card->present || !card->dirty) return true;

    memcard_backup_once(card);

    /* Written to a temporary file and renamed over the original: a crash or a
     * kill in the middle of the write would otherwise leave a truncated card,
     * and rename() is atomic on the same filesystem. */
    char tmp[sizeof(card->filepath) + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", card->filepath);

    FILE* f = fopen(tmp, "wb");
    if (!f) {
        LOG_SYSTEM_ERROR("[SIO] Memory card save failed: %s", tmp);
        return false;
    }
    size_t written = fwrite(card->data, 1, MEMCARD_SIZE, f);
    bool ok = (written == MEMCARD_SIZE) && (fflush(f) == 0);
    fclose(f);
    if (!ok || rename(tmp, card->filepath) != 0) {
        LOG_SYSTEM_ERROR("[SIO] Memory card save failed: %s", card->filepath);
        remove(tmp);
        return false;
    }

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
    sio_save_memcard(card);
    LOG_SYSTEM_INFO("[SIO] Memory card created: %s", filepath);
}
