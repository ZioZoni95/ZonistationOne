# Component Comparison: MDEC (Motion Decoder)

## 🔍 **MDEC SYSTEM COMPARISON**

### **Your MDEC System: COMPLETELY MISSING** ❌

#### **What You Have:**
- ❌ **No MDEC files** - `mdec.h` and `mdec.c` don't exist
- ❌ **No MDEC structure** - No MDEC state in your Interconnect
- ❌ **No MDEC registers** - No MDEC register handling
- ❌ **No video decompression** - No compressed video support
- ❌ **No DMA integration** - No MDEC DMA channels

#### **What PCSX ReARMed Has:**
- ✅ **Complete MDEC system** (`mdec.h` + `mdec.c`)
- ✅ **Video decompression** - MPEG-1 and I-Frame decoding
- ✅ **DMA integration** - MDEC in/out DMA channels
- ✅ **Event system integration** - MDEC timing coordination
- ✅ **Interrupt generation** - MDEC completion interrupts
- ✅ **RGB/YUV conversion** - Color space processing

---

## ❌ **MISSING FROM YOUR MDEC SYSTEM**

### **1. Complete MDEC System Files**

#### **Missing File 1: `mdec.h`**
```c
// PCSX ReARMed has this complete header
#ifndef __MDEC_H__
#define __MDEC_H__

#include "psxcommon.h"

// MDEC Functions
void mdecInit();
void mdecWrite0(u32 data);
void mdecWrite1(u32 data);
u32 mdecRead0();
u32 mdecRead1();

// MDEC Interrupts
void mdec0Interrupt();
void mdec1Interrupt();
int mdecFreeze(void *f, int Mode);

#endif
```

#### **Missing File 2: `mdec.c`**
```c
// PCSX ReARMed has this complete implementation
#include "mdec.h"
#include "psxevents.h"
#include "psxdma.h"

// MDEC state and video decompression
// MPEG-1 and I-Frame decoding
// RGB/YUV color space conversion
// DMA transfer management
```

### **2. Critical Missing Functions**

#### **Missing Function 1: MDEC Register Access**
```c
// PCSX ReARMed has these
void mdecWrite0(u32 data);
void mdecWrite1(u32 data);
u32 mdecRead0();
u32 mdecRead1();

// You're missing MDEC register access entirely
// Need to add: void mdec_write_register(Interconnect* inter, uint32_t reg, uint32_t val);
// Need to add: uint32_t mdec_read_register(Interconnect* inter, uint32_t reg);
```

#### **Missing Function 2: MDEC Interrupt and Timing**
```c
// PCSX ReARMed has these
void mdec0Interrupt();
void mdec1Interrupt();

// You're missing MDEC timing entirely
// Need to add: void mdec_interrupt(Interconnect* inter, int channel);
// Need to add: void mdec_schedule_irq(Interconnect* inter, uint32_t cycles);
```

#### **Missing Function 3: Video Decompression**
```c
// PCSX ReARMed has video decompression
// MPEG-1 decoding
// I-Frame decoding
// RGB/YUV conversion
// Block-based decompression

// You're missing video decompression entirely
// Need to add: void mdec_decompress_frame(Interconnect* inter, uint8_t* input, uint8_t* output);
// Need to add: void mdec_convert_yuv_to_rgb(Interconnect* inter, uint8_t* yuv, uint8_t* rgb);
```

#### **Missing Function 4: DMA Integration**
```c
// PCSX ReARMed has MDEC DMA integration
// MDEC in DMA (Channel 0) - compressed data input
// MDEC out DMA (Channel 1) - decompressed data output
// DMA transfer coordination

// You're missing MDEC DMA entirely
// Need to add: void mdec_dma_in(Interconnect* inter, uint8_t* data, uint32_t size);
// Need to add: void mdec_dma_out(Interconnect* inter, uint8_t* data, uint32_t size);
```

### **3. Missing Data Structures**

#### **Missing MDEC State:**
```c
// PCSX ReARMed has extensive MDEC state
typedef struct {
    // MDEC registers
    uint32_t command;
    uint32_t status;
    uint32_t control;
    
    // Decompression state
    uint8_t* input_buffer;
    uint8_t* output_buffer;
    uint32_t input_size;
    uint32_t output_size;
    
    // Timing
    uint32_t decompress_cycles;
    bool decompress_active;
    
    // DMA state
    bool dma_in_active;
    bool dma_out_active;
} MdecState;

// You're missing MDEC state entirely
// Need to add to Interconnect struct:
// Mdec mdec_state;  // MDEC state structure
```

#### **Missing Decompression Tables:**
```c
// PCSX ReARMed has decompression tables
// Quantization tables
// Zigzag scan tables
// IDCT tables
// Color conversion tables

// You're missing decompression tables entirely
// Need to add: uint8_t quantization_tables[2][64];
// Need to add: uint8_t zigzag_table[64];
```

### **4. Missing Video Processing**

#### **Missing MPEG-1 Decoding:**
```c
// PCSX ReARMed has MPEG-1 decoding
// I-Frame decoding
// P-Frame decoding (partial)
// B-Frame decoding (partial)
// Motion compensation

// You're missing MPEG-1 decoding entirely
// Need to add: void mdec_decode_mpeg1(Interconnect* inter, uint8_t* data, uint32_t size);
```

#### **Missing Color Space Conversion:**
```c
// PCSX ReARMed has color space conversion
// YUV to RGB conversion
// Color matrix operations
// Gamma correction
// Color space handling

// You're missing color space conversion entirely
// Need to add: void mdec_yuv_to_rgb(Interconnect* inter, uint8_t* yuv, uint8_t* rgb);
```

---

## 🔧 **WHAT YOU NEED TO ADD**

### **Step 1: Create MDEC System Files**

#### **Create `include/mdec.h`:**
```c
#ifndef MDEC_H
#define MDEC_H

#include <stdint.h>
#include <stdbool.h>

// MDEC Register Addresses (relative to MDEC_START)
#define MDEC_CMD    0x00
#define MDEC_STAT   0x04
#define MDEC_CTRL   0x08

// MDEC Command Register Bits
#define MDEC_CMD_DECODE    0x00000001
#define MDEC_CMD_STOP      0x00000002
#define MDEC_CMD_RESET     0x00000004
#define MDEC_CMD_RGB24     0x00000008
#define MDEC_CMD_RGB16     0x00000010
#define MDEC_CMD_YUV       0x00000020

// MDEC Status Register Bits
#define MDEC_STAT_BUSY     0x00000001
#define MDEC_STAT_DMA_IN   0x00000002
#define MDEC_STAT_DMA_OUT  0x00000004
#define MDEC_STAT_IRQ      0x00000008
#define MDEC_STAT_DATA_OUT 0x00000010

// MDEC Control Register Bits
#define MDEC_CTRL_DMA_IN_EN  0x00000001
#define MDEC_CTRL_DMA_OUT_EN 0x00000002
#define MDEC_CTRL_IRQ_EN     0x00000004

// MDEC state structure
typedef struct {
    // MDEC registers
    uint32_t command;
    uint32_t status;
    uint32_t control;
    
    // Decompression state
    uint8_t input_buffer[8192];  // 8KB input buffer
    uint8_t output_buffer[32768]; // 32KB output buffer
    uint32_t input_pos;
    uint32_t output_pos;
    uint32_t input_size;
    uint32_t output_size;
    
    // Decompression tables
    uint8_t quantization_table[64];
    uint8_t zigzag_table[64];
    
    // Timing
    uint32_t decompress_cycles;
    bool decompress_active;
    
    // DMA state
    bool dma_in_active;
    bool dma_out_active;
    uint32_t dma_in_addr;
    uint32_t dma_out_addr;
    uint32_t dma_in_size;
    uint32_t dma_out_size;
    
    // Interrupt state
    bool irq_pending;
    uint32_t irq_cycles;
} Mdec;

// MDEC functions
void mdec_init(Mdec* mdec);
uint32_t mdec_read_register(Mdec* mdec, uint32_t reg);
void mdec_write_register(Mdec* mdec, uint32_t reg, uint32_t val);
void mdec_update(Mdec* mdec);
void mdec_schedule_irq(Mdec* mdec, uint32_t cycles);

// DMA functions
void mdec_dma_in(Mdec* mdec, uint8_t* data, uint32_t size);
void mdec_dma_out(Mdec* mdec, uint8_t* data, uint32_t size);

// Decompression functions
void mdec_decompress_frame(Mdec* mdec);
void mdec_yuv_to_rgb(uint8_t* yuv, uint8_t* rgb, uint32_t width, uint32_t height);

#endif
```

#### **Create `src/mdec.c`:**
```c
#include "mdec.h"
#include "log.h"
#include <string.h>

// Zigzag scan table for DCT coefficients
static const uint8_t zigzag_table[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

void mdec_init(Mdec* mdec) {
    memset(mdec, 0, sizeof(Mdec));
    
    // Initialize default values
    mdec->status = MDEC_STAT_DATA_OUT;
    mdec->control = 0;
    
    // Initialize quantization table (default values)
    for (int i = 0; i < 64; i++) {
        mdec->quantization_table[i] = 8;
    }
    
    // Copy zigzag table
    memcpy(mdec->zigzag_table, zigzag_table, 64);
    
    LOG_INFO("MDEC initialized");
}

uint32_t mdec_read_register(Mdec* mdec, uint32_t reg) {
    uint32_t offset = reg - 0x1f801820;  // MDEC_START
    
    switch (offset) {
        case MDEC_CMD:
            return mdec->command;
        case MDEC_STAT:
            return mdec->status;
        case MDEC_CTRL:
            return mdec->control;
        default:
            LOG_WARN("Unhandled MDEC read at offset 0x%x\n", offset);
            return 0;
    }
}

void mdec_write_register(Mdec* mdec, uint32_t reg, uint32_t val) {
    uint32_t offset = reg - 0x1f801820;  // MDEC_START
    
    switch (offset) {
        case MDEC_CMD:
            mdec->command = val;
            
            if (val & MDEC_CMD_RESET) {
                // Reset MDEC
                mdec->status = MDEC_STAT_DATA_OUT;
                mdec->input_pos = 0;
                mdec->output_pos = 0;
                mdec->decompress_active = false;
                mdec->dma_in_active = false;
                mdec->dma_out_active = false;
            }
            
            if (val & MDEC_CMD_DECODE) {
                // Start decompression
                mdec->decompress_active = true;
                mdec->status |= MDEC_STAT_BUSY;
                // TODO: Start decompression process
            }
            
            if (val & MDEC_CMD_STOP) {
                // Stop decompression
                mdec->decompress_active = false;
                mdec->status &= ~MDEC_STAT_BUSY;
            }
            break;
            
        case MDEC_STAT:
            // Read-only register
            break;
            
        case MDEC_CTRL:
            mdec->control = val;
            break;
            
        default:
            LOG_WARN("Unhandled MDEC write at offset 0x%x = 0x%08x\n", offset, val);
            break;
    }
}

void mdec_update(Mdec* mdec) {
    // TODO: Implement MDEC update
    // This should:
    // 1. Process pending decompression
    // 2. Handle DMA transfers
    // 3. Generate interrupts when needed
    // 4. Update status register
}

void mdec_schedule_irq(Mdec* mdec, uint32_t cycles) {
    mdec->irq_cycles = cycles;
    mdec->irq_pending = true;
    // interconnect_schedule_event(inter, PSXINT_MDECOUTDMA, cycles);
}

void mdec_dma_in(Mdec* mdec, uint8_t* data, uint32_t size) {
    // TODO: Implement MDEC DMA in
    // This should copy compressed data to input buffer
    if (mdec->input_pos + size <= sizeof(mdec->input_buffer)) {
        memcpy(mdec->input_buffer + mdec->input_pos, data, size);
        mdec->input_pos += size;
        mdec->input_size += size;
    }
    
    LOG_INFO("MDEC DMA in: %d bytes", size);
}

void mdec_dma_out(Mdec* mdec, uint8_t* data, uint32_t size) {
    // TODO: Implement MDEC DMA out
    // This should copy decompressed data from output buffer
    if (mdec->output_pos < mdec->output_size) {
        uint32_t copy_size = (size < (mdec->output_size - mdec->output_pos)) ? 
                             size : (mdec->output_size - mdec->output_pos);
        memcpy(data, mdec->output_buffer + mdec->output_pos, copy_size);
        mdec->output_pos += copy_size;
    }
    
    LOG_INFO("MDEC DMA out: %d bytes", size);
}

void mdec_decompress_frame(Mdec* mdec) {
    // TODO: Implement frame decompression
    // This should:
    // 1. Parse compressed data
    // 2. Apply inverse quantization
    // 3. Apply inverse DCT
    // 4. Convert to RGB/YUV
    // 5. Store in output buffer
    
    LOG_INFO("MDEC frame decompression (not implemented)");
}

void mdec_yuv_to_rgb(uint8_t* yuv, uint8_t* rgb, uint32_t width, uint32_t height) {
    // TODO: Implement YUV to RGB conversion
    // This should convert YUV color space to RGB
    
    LOG_INFO("MDEC YUV to RGB conversion (not implemented)");
}
```

### **Step 2: Add MDEC to Interconnect**

#### **Update `include/interconnect.h`:**
```c
// Add to your Interconnect struct
typedef struct Interconnect {
    // ... existing fields
    
    Mdec mdec;  // MDEC state
    
    // ... existing fields
} Interconnect;
```

### **Step 3: Add MDEC Register Handling**

#### **Update `src/interconnect.c`:**
```c
// Add MDEC register handling to interconnect_read/write functions
case MDEC_START ... MDEC_END:
    if (size == 4) {
        return mdec_read_register(&inter->mdec, address);
    }
    break;

// In write function:
case MDEC_START ... MDEC_END:
    if (size == 4) {
        mdec_write_register(&inter->mdec, address, value);
    }
    break;
```

---

## 🎯 **IMPLEMENTATION PRIORITY**

### **CRITICAL (Blocking Boot)**
1. **Create MDEC system files** - Essential for video decompression
2. **Add MDEC to Interconnect** - Essential for register access
3. **Add basic register handling** - Essential for MDEC communication
4. **Add DMA integration** - Essential for data transfer

### **HIGH PRIORITY**
5. **Add basic decompression** - Essential for video playback
6. **Add event system integration** - Essential for timing
7. **Add interrupt generation** - Essential for MDEC events
8. **Add color space conversion** - Essential for video output

### **MEDIUM PRIORITY**
9. **Add MPEG-1 decoding** - For compressed video
10. **Add advanced decompression** - For better video quality
11. **Add optimization** - For performance

---

## 📋 **NEXT STEPS**

1. **Create `include/mdec.h`** with MDEC definitions
2. **Create `src/mdec.c`** with basic MDEC implementation
3. **Add MDEC to Interconnect struct**
4. **Add MDEC register handling** to interconnect
5. **Add MDEC DMA integration** with DMA system

**The MDEC is essential for video decompression. Without it, games with compressed video (FMVs, cutscenes) won't display properly.**

Would you like me to help you implement the MDEC system, or should we move on to analyze the next component? 