# Component Comparison: SIO (Serial I/O)

## 🔍 **SIO SYSTEM COMPARISON**

### **Your SIO System: COMPLETELY MISSING** ❌

#### **What You Have:**
- ❌ **No SIO files** - `sio.h` and `sio.c` don't exist
- ❌ **No SIO structure** - No SIO state in your Interconnect
- ❌ **No SIO registers** - No SIO register handling
- ❌ **No controller support** - No input device support
- ❌ **No memory card support** - No save data support

#### **What PCSX ReARMed Has:**
- ✅ **Complete SIO system** (`sio.h` + `sio.c`)
- ✅ **Controller support** - DualShock controller emulation
- ✅ **Memory card support** - Save/load game data
- ✅ **Serial communication** - Link cable support
- ✅ **Event system integration** - SIO timing coordination
- ✅ **Interrupt generation** - SIO completion interrupts

---

## ❌ **MISSING FROM YOUR SIO SYSTEM**

### **1. Complete SIO System Files**

#### **Missing File 1: `sio.h`**
```c
// PCSX ReARMed has this complete header
#ifndef _SIO_H_
#define _SIO_H_

#include "psxcommon.h"

// SIO Functions
void sioWrite8(unsigned char value);
void sioWriteStat16(unsigned short value);
void sioWriteMode16(unsigned short value);
void sioWriteCtrl16(unsigned short value);
void sioWriteBaud16(unsigned short value);

unsigned char sioRead8();
unsigned short sioReadStat16();
unsigned short sioReadMode16();
unsigned short sioReadCtrl16();
unsigned short sioReadBaud16();

void sioInterrupt();
int sioFreeze(void *f, int Mode);

#endif
```

#### **Missing File 2: `sio.c`**
```c
// PCSX ReARMed has this complete implementation
#include "sio.h"
#include "psxevents.h"

// SIO state and controller/memory card management
// Serial communication and timing
// Interrupt generation and event handling
```

### **2. Critical Missing Functions**

#### **Missing Function 1: SIO Register Access**
```c
// PCSX ReARMed has these
void sioWrite8(unsigned char value);
void sioWriteStat16(unsigned short value);
void sioWriteMode16(unsigned short value);
void sioWriteCtrl16(unsigned short value);
void sioWriteBaud16(unsigned short value);

unsigned char sioRead8();
unsigned short sioReadStat16();
unsigned short sioReadMode16();
unsigned short sioReadCtrl16();
unsigned short sioReadBaud16();

// You're missing SIO register access entirely
// Need to add: void sio_write_register(Interconnect* inter, uint32_t reg, uint8_t val);
// Need to add: uint8_t sio_read_register(Interconnect* inter, uint32_t reg);
```

#### **Missing Function 2: SIO Interrupt and Timing**
```c
// PCSX ReARMed has these
void sioInterrupt();
void CALLBACK SIOirq(int cycles_after);

// You're missing SIO timing entirely
// Need to add: void sio_interrupt(Interconnect* inter);
// Need to add: void sio_schedule_irq(Interconnect* inter, uint32_t cycles);
```

#### **Missing Function 3: Controller Support**
```c
// PCSX ReARMed has controller support
// Controller state management
// Button/analog stick input
// Controller identification

// You're missing controller support entirely
// Need to add: void sio_controller_update(Interconnect* inter);
// Need to add: uint16_t sio_controller_read(Interconnect* inter);
```

#### **Missing Function 4: Memory Card Support**
```c
// PCSX ReARMed has memory card support
// Save/load game data
// Memory card formatting
// Data validation

// You're missing memory card support entirely
// Need to add: void sio_memory_card_read(Interconnect* inter, uint8_t* data, uint32_t addr);
// Need to add: void sio_memory_card_write(Interconnect* inter, uint8_t* data, uint32_t addr);
```

### **3. Missing Data Structures**

#### **Missing SIO State:**
```c
// PCSX ReARMed has extensive SIO state
typedef struct {
    // SIO registers
    uint8_t data;
    uint16_t stat;
    uint16_t mode;
    uint16_t ctrl;
    uint16_t baud;
    
    // Controller state
    uint16_t controller_data;
    bool controller_connected;
    
    // Memory card state
    uint8_t memory_card[128 * 1024];  // 128KB memory card
    bool memory_card_connected;
    
    // Timing
    uint32_t transfer_cycles;
    bool transfer_active;
} SioState;

// You're missing SIO state entirely
// Need to add to Interconnect struct:
// Sio sio_state;  // SIO state structure
```

#### **Missing Controller State:**
```c
// PCSX ReARMed manages controller state
// Button states (cross, circle, square, triangle, etc.)
// Analog stick positions
// Controller type detection
// Vibration support

// You're missing controller state entirely
// Need to add: Controller controller;  // Controller state
```

#### **Missing Memory Card State:**
```c
// PCSX ReARMed manages memory card state
// 128KB memory card data
// File system management
// Data integrity checks
// Save/load operations

// You're missing memory card state entirely
// Need to add: MemoryCard memory_card;  // Memory card state
```

### **4. Missing Input/Output Support**

#### **Missing Input Handling:**
```c
// PCSX ReARMed has input handling
// Keyboard/mouse input mapping
// Controller button mapping
// Analog stick calibration
// Input event processing

// You're missing input handling entirely
// Need to add: void sio_handle_input(Interconnect* inter);
```

#### **Missing Save/Load Support:**
```c
// PCSX ReARMed has save/load support
// Memory card file I/O
// Save state management
// Data validation and error handling
// File system operations

// You're missing save/load support entirely
// Need to add: bool sio_save_memory_card(Interconnect* inter, const char* filename);
// Need to add: bool sio_load_memory_card(Interconnect* inter, const char* filename);
```

---

## 🔧 **WHAT YOU NEED TO ADD**

### **Step 1: Create SIO System Files**

#### **Create `include/sio.h`:**
```c
#ifndef SIO_H
#define SIO_H

#include <stdint.h>
#include <stdbool.h>

// SIO Register Addresses (relative to SIO_START)
#define SIO_DATA    0x00
#define SIO_STAT    0x04
#define SIO_MODE    0x08
#define SIO_CTRL    0x0a
#define SIO_BAUD    0x0e

// SIO Status Register Bits
#define SIO_STAT_TX_RDY    0x0001
#define SIO_STAT_RX_RDY    0x0002
#define SIO_STAT_TX_EMPTY  0x0004
#define SIO_STAT_RX_PARITY 0x0008
#define SIO_STAT_IRQ       0x0010

// SIO Control Register Bits
#define SIO_CTRL_TX_EN     0x0001
#define SIO_CTRL_RX_EN     0x0002
#define SIO_CTRL_TX_IRQ    0x0004
#define SIO_CTRL_RX_IRQ    0x0008
#define SIO_CTRL_DTR       0x0010
#define SIO_CTRL_RTS       0x0020

// Controller Button Masks
#define CTRL_SELECT    0x0001
#define CTRL_START     0x0008
#define CTRL_UP        0x0010
#define CTRL_RIGHT     0x0020
#define CTRL_DOWN      0x0040
#define CTRL_LEFT      0x0080
#define CTRL_L2        0x0100
#define CTRL_R2        0x0200
#define CTRL_L1        0x0400
#define CTRL_R1        0x0800
#define CTRL_TRIANGLE  0x1000
#define CTRL_CIRCLE    0x2000
#define CTRL_CROSS     0x4000
#define CTRL_SQUARE    0x8000

// SIO state structure
typedef struct {
    // SIO registers
    uint8_t data;
    uint16_t stat;
    uint16_t mode;
    uint16_t ctrl;
    uint16_t baud;
    
    // Transfer state
    bool transfer_active;
    uint32_t transfer_cycles;
    uint8_t transfer_data;
    
    // Controller state
    uint16_t controller_buttons;
    uint8_t controller_analog_left_x;
    uint8_t controller_analog_left_y;
    uint8_t controller_analog_right_x;
    uint8_t controller_analog_right_y;
    bool controller_connected;
    
    // Memory card state
    uint8_t memory_card[128 * 1024];  // 128KB
    bool memory_card_connected;
    uint32_t memory_card_addr;
    
    // Timing
    uint32_t irq_cycles;
    bool irq_pending;
} Sio;

// SIO functions
void sio_init(Sio* sio);
uint8_t sio_read_register(Sio* sio, uint32_t reg);
void sio_write_register(Sio* sio, uint32_t reg, uint8_t val);
void sio_update(Sio* sio);
void sio_schedule_irq(Sio* sio, uint32_t cycles);

// Controller functions
void sio_controller_set_button(Sio* sio, uint16_t button, bool pressed);
void sio_controller_set_analog(Sio* sio, uint8_t left_x, uint8_t left_y, uint8_t right_x, uint8_t right_y);

// Memory card functions
bool sio_memory_card_load(Sio* sio, const char* filename);
bool sio_memory_card_save(Sio* sio, const char* filename);

#endif
```

#### **Create `src/sio.c`:**
```c
#include "sio.h"
#include "log.h"
#include <string.h>
#include <stdio.h>

void sio_init(Sio* sio) {
    memset(sio, 0, sizeof(Sio));
    
    // Initialize default values
    sio->stat = SIO_STAT_TX_EMPTY;
    sio->mode = 0x0005;  // 8-bit data, 1 stop bit, no parity
    sio->baud = 0x0088;  // 115200 baud
    
    // Initialize controller
    sio->controller_connected = true;
    sio->controller_buttons = 0xFFFF;  // All buttons released
    
    // Initialize memory card
    sio->memory_card_connected = true;
    
    LOG_INFO("SIO initialized");
}

uint8_t sio_read_register(Sio* sio, uint32_t reg) {
    uint32_t offset = reg - 0x1f801050;  // SIO_START
    
    switch (offset) {
        case SIO_DATA:
            return sio->data;
        case SIO_STAT:
            return sio->stat;
        case SIO_MODE:
            return sio->mode;
        case SIO_CTRL:
            return sio->ctrl;
        case SIO_BAUD:
            return sio->baud;
        default:
            LOG_WARN("Unhandled SIO read at offset 0x%x\n", offset);
            return 0;
    }
}

void sio_write_register(Sio* sio, uint32_t reg, uint8_t val) {
    uint32_t offset = reg - 0x1f801050;  // SIO_START
    
    switch (offset) {
        case SIO_DATA:
            sio->data = val;
            // TODO: Handle data transfer
            break;
        case SIO_STAT:
            // Read-only register
            break;
        case SIO_MODE:
            sio->mode = val;
            break;
        case SIO_CTRL:
            sio->ctrl = val;
            // TODO: Handle control register changes
            break;
        case SIO_BAUD:
            sio->baud = val;
            break;
        default:
            LOG_WARN("Unhandled SIO write at offset 0x%x = 0x%02x\n", offset, val);
            break;
    }
}

void sio_update(Sio* sio) {
    // TODO: Implement SIO update
    // This should:
    // 1. Process pending transfers
    // 2. Handle controller communication
    // 3. Handle memory card operations
    // 4. Generate interrupts when needed
}

void sio_schedule_irq(Sio* sio, uint32_t cycles) {
    sio->irq_cycles = cycles;
    sio->irq_pending = true;
    // interconnect_schedule_event(inter, PSXINT_SIO, cycles);
}

void sio_controller_set_button(Sio* sio, uint16_t button, bool pressed) {
    if (pressed) {
        sio->controller_buttons &= ~button;
    } else {
        sio->controller_buttons |= button;
    }
}

void sio_controller_set_analog(Sio* sio, uint8_t left_x, uint8_t left_y, uint8_t right_x, uint8_t right_y) {
    sio->controller_analog_left_x = left_x;
    sio->controller_analog_left_y = left_y;
    sio->controller_analog_right_x = right_x;
    sio->controller_analog_right_y = right_y;
}

bool sio_memory_card_load(Sio* sio, const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        LOG_WARN("Could not open memory card file: %s\n", filename);
        return false;
    }
    
    size_t read = fread(sio->memory_card, 1, sizeof(sio->memory_card), file);
    fclose(file);
    
    if (read != sizeof(sio->memory_card)) {
        LOG_WARN("Memory card file size mismatch: expected %zu, got %zu\n", 
                 sizeof(sio->memory_card), read);
        return false;
    }
    
    LOG_INFO("Memory card loaded: %s", filename);
    return true;
}

bool sio_memory_card_save(Sio* sio, const char* filename) {
    FILE* file = fopen(filename, "wb");
    if (!file) {
        LOG_ERROR("Could not create memory card file: %s\n", filename);
        return false;
    }
    
    size_t written = fwrite(sio->memory_card, 1, sizeof(sio->memory_card), file);
    fclose(file);
    
    if (written != sizeof(sio->memory_card)) {
        LOG_ERROR("Failed to write memory card file: %s\n", filename);
        return false;
    }
    
    LOG_INFO("Memory card saved: %s", filename);
    return true;
}
```

### **Step 2: Add SIO to Interconnect**

#### **Update `include/interconnect.h`:**
```c
// Add to your Interconnect struct
typedef struct Interconnect {
    // ... existing fields
    
    Sio sio;  // SIO state
    
    // ... existing fields
} Interconnect;
```

### **Step 3: Add SIO Register Handling**

#### **Update `src/interconnect.c`:**
```c
// Add SIO register handling to interconnect_read/write functions
case SIO_START ... SIO_END:
    if (size == 1) {
        return sio_read_register(&inter->sio, address);
    }
    break;

// In write function:
case SIO_START ... SIO_END:
    if (size == 1) {
        sio_write_register(&inter->sio, address, (uint8_t)value);
    }
    break;
```

---

## 🎯 **IMPLEMENTATION PRIORITY**

### **CRITICAL (Blocking Boot)**
1. **Create SIO system files** - Essential for input/output
2. **Add SIO to Interconnect** - Essential for register access
3. **Add basic register handling** - Essential for SIO communication
4. **Add controller support** - Essential for game input

### **HIGH PRIORITY**
5. **Add memory card support** - Essential for save data
6. **Add event system integration** - Essential for timing
7. **Add interrupt generation** - Essential for SIO events
8. **Add input mapping** - Essential for user interaction

### **MEDIUM PRIORITY**
9. **Add save/load functionality** - For persistent data
10. **Add controller calibration** - For accurate input
11. **Add link cable support** - For multiplayer games

---

## 📋 **NEXT STEPS**

1. **Create `include/sio.h`** with SIO definitions
2. **Create `src/sio.c`** with basic SIO implementation
3. **Add SIO to Interconnect struct**
4. **Add SIO register handling** to interconnect
5. **Add controller input support**

**The SIO is essential for user input and save data. Without it, games can't receive controller input or save progress.**

Would you like me to help you implement the SIO system, or should we move on to analyze the next component? 