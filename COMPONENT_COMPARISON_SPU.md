# Component Comparison: SPU (Sound Processing Unit)

## 🔍 **SPU SYSTEM COMPARISON**

### **Your SPU System: COMPLETELY MISSING** ❌

#### **What You Have:**
- ❌ **No SPU files** - `spu.h` and `spu.c` don't exist
- ❌ **No SPU structure** - No SPU state in your Interconnect
- ❌ **No SPU registers** - No SPU register handling
- ❌ **No sound processing** - No audio output capability

#### **What PCSX ReARMed Has:**
- ✅ **Complete SPU system** (`spu.h` + `spu.c`)
- ✅ **24 voice channels** with individual control
- ✅ **ADPCM decoding** for compressed audio
- ✅ **Reverb effects** and audio processing
- ✅ **DMA integration** for audio data transfer
- ✅ **Event system integration** for timing
- ✅ **512KB SPU RAM** for audio data storage

---

## ❌ **MISSING FROM YOUR SPU SYSTEM**

### **1. Complete SPU System Files**

#### **Missing File 1: `spu.h`**
```c
// PCSX ReARMed has this complete header
#ifndef __SPU_H__
#define __SPU_H__

#include "psxcommon.h"

// SPU Register Addresses
#define H_SPUirqAddr     0x0da4
#define H_SPUaddr        0x0da6
#define H_SPUdata        0x0da8
#define H_SPUctrl        0x0daa
#define H_SPUstat        0x0dae
#define H_SPUon1         0x0d88
#define H_SPUon2         0x0d8a
#define H_SPUoff1        0x0d8c
#define H_SPUoff2        0x0d8e

// SPU Functions
void CALLBACK SPUirq(int cycles_after);
void CALLBACK SPUschedule(unsigned int cycles_after);
void spuDelayedIrq();
void spuUpdate();

#endif
```

#### **Missing File 2: `spu.c`**
```c
// PCSX ReARMed has this complete implementation
#include "spu.h"
#include "psxevents.h"
#include "psxdma.h"

// SPU state and voice management
// 24 voice channels with individual control
// ADPCM decoding and audio processing
// Reverb effects and audio output
```

### **2. Critical Missing Functions**

#### **Missing Function 1: SPU Register Access**
```c
// PCSX ReARMed has these
void SPU_writeRegister(unsigned long reg, unsigned short val, unsigned int cycles);
unsigned short SPU_readRegister(unsigned long reg, unsigned int cycles);

// You're missing SPU register access entirely
// Need to add: void spu_write_register(Interconnect* inter, uint32_t reg, uint16_t val);
// Need to add: uint16_t spu_read_register(Interconnect* inter, uint32_t reg);
```

#### **Missing Function 2: SPU DMA Functions**
```c
// PCSX ReARMed has these
void SPU_writeDMAMem(unsigned short *spuaddr, int size, unsigned int cycles);
void SPU_readDMAMem(unsigned short *spuaddr, int size, unsigned int cycles);

// You're missing SPU DMA entirely
// Need to add: void spu_write_dma(Interconnect* inter, uint16_t* data, uint32_t size);
// Need to add: void spu_read_dma(Interconnect* inter, uint16_t* data, uint32_t size);
```

#### **Missing Function 3: SPU Update and Timing**
```c
// PCSX ReARMed has these
void spuUpdate();
void spuDelayedIrq();
void CALLBACK SPUirq(int cycles_after);

// You're missing SPU timing entirely
// Need to add: void spu_update(Interconnect* inter);
// Need to add: void spu_schedule_irq(Interconnect* inter, uint32_t cycles);
```

### **3. Missing Data Structures**

#### **Missing SPU State:**
```c
// PCSX ReARMed has extensive SPU state
typedef struct SPUFreeze {
    unsigned char SPUPorts[0x200];  // SPU registers
    unsigned char SPURam[0x80000];  // 512KB SPU RAM
    // ... voice states, effects, etc.
} SPUFreeze_t;

// You're missing SPU state entirely
// Need to add to Interconnect struct:
// Spu spu_state;  // SPU state structure
```

#### **Missing Voice Management:**
```c
// PCSX ReARMed manages 24 voice channels
// Each voice has:
// - Volume control (left/right)
// - Frequency/pitch
// - Sample data
// - ADSR envelope
// - Reverb settings

// You're missing voice management entirely
// Need to add: Voice voices[24];  // 24 voice channels
```

### **4. Missing Audio Processing**

#### **Missing ADPCM Decoding:**
```c
// PCSX ReARMed has ADPCM decoding
void SPU_playADPCMchannel(xa_decode_t *xap, unsigned int cycles, int channel);

// You're missing ADPCM decoding entirely
// Need to add: void spu_decode_adpcm(Interconnect* inter, uint8_t* data, uint32_t size);
```

#### **Missing Audio Output:**
```c
// PCSX ReARMed has audio output
void SPU_async(unsigned int cycles, unsigned int cycles_after);

// You're missing audio output entirely
// Need to add: void spu_output_audio(Interconnect* inter, int16_t* buffer, uint32_t samples);
```

---

## 🔧 **WHAT YOU NEED TO ADD**

### **Step 1: Create SPU System Files**

#### **Create `include/spu.h`:**
```c
#ifndef SPU_H
#define SPU_H

#include <stdint.h>
#include <stdbool.h>

// SPU Register Addresses (relative to SPU_START)
#define SPU_MAIN_VOL_LEFT    0x0d80
#define SPU_MAIN_VOL_RIGHT   0x0d82
#define SPU_REVERB_VOL_LEFT  0x0d84
#define SPU_REVERB_VOL_RIGHT 0x0d86
#define SPU_VOICE_ON_LOW     0x0d88
#define SPU_VOICE_ON_HIGH    0x0d8a
#define SPU_VOICE_OFF_LOW    0x0d8c
#define SPU_VOICE_OFF_HIGH   0x0d8e
#define SPU_FM_MODE_LOW      0x0d90
#define SPU_FM_MODE_HIGH     0x0d92
#define SPU_NOISE_MODE_LOW   0x0d94
#define SPU_NOISE_MODE_HIGH  0x0d96
#define SPU_REVERB_MODE_LOW  0x0d98
#define SPU_REVERB_MODE_HIGH 0x0d9a
#define SPU_CHANNEL_FM_LOW   0x0d9c
#define SPU_CHANNEL_FM_HIGH  0x0d9e
#define SPU_CHANNEL_NOISE_LOW 0x0da0
#define SPU_CHANNEL_NOISE_HIGH 0x0da2
#define SPU_CHANNEL_REVERB_LOW 0x0da4
#define SPU_CHANNEL_REVERB_HIGH 0x0da6
#define SPU_IRQ_ADDR         0x0da4
#define SPU_ADDR             0x0da6
#define SPU_DATA             0x0da8
#define SPU_CTRL             0x0daa
#define SPU_STAT             0x0dae

// Voice structure (simplified)
typedef struct {
    uint16_t volume_left;
    uint16_t volume_right;
    uint16_t sample_rate;
    uint16_t start_addr;
    uint16_t loop_addr;
    uint16_t adsr1;
    uint16_t adsr2;
    bool active;
    bool key_on;
    bool key_off;
} SpuVoice;

// SPU state structure
typedef struct {
    // Main volume controls
    uint16_t main_volume_left;
    uint16_t main_volume_right;
    uint16_t reverb_volume_left;
    uint16_t reverb_volume_right;
    
    // Voice control
    uint32_t voice_on;
    uint32_t voice_off;
    uint32_t voice_fm_mode;
    uint32_t voice_noise_mode;
    uint32_t voice_reverb_mode;
    
    // Control registers
    uint16_t irq_addr;
    uint16_t addr;
    uint16_t data;
    uint16_t ctrl;
    uint16_t stat;
    
    // 24 voice channels
    SpuVoice voices[24];
    
    // SPU RAM (512KB)
    uint8_t ram[512 * 1024];
    
    // Timing
    uint32_t update_cycles;
    bool irq_pending;
} Spu;

// SPU functions
void spu_init(Spu* spu);
uint16_t spu_read_register(Spu* spu, uint32_t reg);
void spu_write_register(Spu* spu, uint32_t reg, uint16_t val);
void spu_write_dma(Spu* spu, uint16_t* data, uint32_t size);
void spu_read_dma(Spu* spu, uint16_t* data, uint32_t size);
void spu_update(Spu* spu);
void spu_schedule_irq(Spu* spu, uint32_t cycles);

#endif
```

#### **Create `src/spu.c`:**
```c
#include "spu.h"
#include "log.h"
#include <string.h>

void spu_init(Spu* spu) {
    memset(spu, 0, sizeof(Spu));
    
    // Initialize default values
    spu->main_volume_left = 0x3fff;
    spu->main_volume_right = 0x3fff;
    spu->reverb_volume_left = 0x3fff;
    spu->reverb_volume_right = 0x3fff;
    
    // Initialize all voices
    for (int i = 0; i < 24; i++) {
        spu->voices[i].volume_left = 0x3fff;
        spu->voices[i].volume_right = 0x3fff;
        spu->voices[i].sample_rate = 0x1000;
        spu->voices[i].active = false;
        spu->voices[i].key_on = false;
        spu->voices[i].key_off = false;
    }
    
    LOG_INFO("SPU initialized");
}

uint16_t spu_read_register(Spu* spu, uint32_t reg) {
    uint32_t offset = reg - 0x1f801c00;
    
    switch (offset) {
        case SPU_MAIN_VOL_LEFT:
            return spu->main_volume_left;
        case SPU_MAIN_VOL_RIGHT:
            return spu->main_volume_right;
        case SPU_REVERB_VOL_LEFT:
            return spu->reverb_volume_left;
        case SPU_REVERB_VOL_RIGHT:
            return spu->reverb_volume_right;
        case SPU_VOICE_ON_LOW:
            return spu->voice_on & 0xFFFF;
        case SPU_VOICE_ON_HIGH:
            return (spu->voice_on >> 16) & 0xFFFF;
        case SPU_VOICE_OFF_LOW:
            return spu->voice_off & 0xFFFF;
        case SPU_VOICE_OFF_HIGH:
            return (spu->voice_off >> 16) & 0xFFFF;
        case SPU_ADDR:
            return spu->addr;
        case SPU_DATA:
            return spu->data;
        case SPU_STAT:
            return spu->stat;
        default:
            LOG_WARN("Unhandled SPU read at offset 0x%x\n", offset);
            return 0;
    }
}

void spu_write_register(Spu* spu, uint32_t reg, uint16_t val) {
    uint32_t offset = reg - 0x1f801c00;
    
    switch (offset) {
        case SPU_MAIN_VOL_LEFT:
            spu->main_volume_left = val;
            break;
        case SPU_MAIN_VOL_RIGHT:
            spu->main_volume_right = val;
            break;
        case SPU_REVERB_VOL_LEFT:
            spu->reverb_volume_left = val;
            break;
        case SPU_REVERB_VOL_RIGHT:
            spu->reverb_volume_right = val;
            break;
        case SPU_VOICE_ON_LOW:
            spu->voice_on = (spu->voice_on & 0xFFFF0000) | val;
            // TODO: Handle voice activation
            break;
        case SPU_VOICE_ON_HIGH:
            spu->voice_on = (spu->voice_on & 0xFFFF) | ((uint32_t)val << 16);
            // TODO: Handle voice activation
            break;
        case SPU_VOICE_OFF_LOW:
            spu->voice_off = (spu->voice_off & 0xFFFF0000) | val;
            // TODO: Handle voice deactivation
            break;
        case SPU_VOICE_OFF_HIGH:
            spu->voice_off = (spu->voice_off & 0xFFFF) | ((uint32_t)val << 16);
            // TODO: Handle voice deactivation
            break;
        case SPU_ADDR:
            spu->addr = val;
            break;
        case SPU_DATA:
            spu->data = val;
            // TODO: Write to SPU RAM at current address
            break;
        case SPU_CTRL:
            spu->ctrl = val;
            // TODO: Handle control register
            break;
        default:
            LOG_WARN("Unhandled SPU write at offset 0x%x = 0x%04x\n", offset, val);
            break;
    }
}

void spu_write_dma(Spu* spu, uint16_t* data, uint32_t size) {
    // TODO: Implement SPU DMA write
    // This should write data to SPU RAM
    LOG_INFO("SPU DMA write: %d words", size);
}

void spu_read_dma(Spu* spu, uint16_t* data, uint32_t size) {
    // TODO: Implement SPU DMA read
    // This should read data from SPU RAM
    LOG_INFO("SPU DMA read: %d words", size);
}

void spu_update(Spu* spu) {
    // TODO: Implement SPU audio processing
    // This should:
    // 1. Process active voices
    // 2. Generate audio samples
    // 3. Apply effects (reverb, etc.)
    // 4. Output audio
}

void spu_schedule_irq(Spu* spu, uint32_t cycles) {
    // TODO: Schedule SPU interrupt
    spu->irq_pending = true;
    // interconnect_schedule_event(inter, PSXINT_SPU_IRQ, cycles);
}
```

### **Step 2: Add SPU to Interconnect**

#### **Update `include/interconnect.h`:**
```c
// Add to your Interconnect struct
typedef struct Interconnect {
    // ... existing fields
    
    Spu spu;  // SPU state
    
    // ... existing fields
} Interconnect;
```

### **Step 3: Add SPU Register Handling**

#### **Update `src/interconnect.c`:**
```c
// Add SPU register handling to interconnect_read/write functions
case SPU_START ... SPU_END:
    if (size == 2) {
        return spu_read_register(&inter->spu, address);
    }
    break;

// In write function:
case SPU_START ... SPU_END:
    if (size == 2) {
        spu_write_register(&inter->spu, address, (uint16_t)value);
    }
    break;
```

---

## 🎯 **IMPLEMENTATION PRIORITY**

### **CRITICAL (Blocking Boot)**
1. **Create SPU system files** - Essential for sound functionality
2. **Add SPU to Interconnect** - Essential for register access
3. **Add basic register handling** - Essential for SPU communication
4. **Add DMA integration** - Essential for audio data transfer

### **HIGH PRIORITY**
5. **Add voice management** - Essential for sound playback
6. **Add basic audio processing** - Essential for sound output
7. **Add event system integration** - Essential for timing
8. **Add interrupt generation** - Essential for SPU events

### **MEDIUM PRIORITY**
9. **Add ADPCM decoding** - For compressed audio
10. **Add reverb effects** - For audio quality
11. **Add audio output** - For actual sound

---

## 📋 **NEXT STEPS**

1. **Create `include/spu.h`** with SPU definitions
2. **Create `src/spu.c`** with basic SPU implementation
3. **Add SPU to Interconnect struct**
4. **Add SPU register handling** to interconnect
5. **Add SPU DMA integration** with DMA system

**The SPU is essential for sound output. Without it, games will be silent and some may not work properly.**

Would you like me to help you implement the SPU system, or should we move on to analyze the next component? 