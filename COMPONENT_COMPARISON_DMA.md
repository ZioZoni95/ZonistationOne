# Component Comparison: DMA System

## 🔍 **DMA SYSTEM COMPARISON**

### **Your DMA System: PARTIALLY IMPLEMENTED** ⚠️

#### **What You Have:**
- ✅ **Basic DMA structure** - `Dma` struct with 7 channels
- ✅ **Channel control registers** - CHCR, MADR, BCR
- ✅ **DMA control registers** - DPCR, DICR
- ✅ **Register read/write functions** - `dma_read()`, `dma_write()`
- ✅ **Channel state management** - `dma_channel_is_active()`, `dma_channel_done()`

#### **What PCSX ReARMed Has:**
- ✅ **Complete DMA system** with all 7 channels
- ✅ **Channel-specific handlers** - `psxDma2()`, `psxDma3()`, `psxDma4()`, `psxDma6()`
- ✅ **Event system integration** - DMA events scheduled via `set_event()`
- ✅ **Interrupt generation** - DMA interrupts via `DMA_INTERRUPT()`
- ✅ **Linked list support** - GPU command list processing
- ✅ **Memory validation** - `getDmaRam()` with bounds checking

---

## ❌ **MISSING FROM YOUR DMA SYSTEM**

### **1. Channel-Specific DMA Handlers**

#### **Missing Function 1: Channel-Specific Handlers**
```c
// PCSX ReARMed has these specific handlers
void psxDma2(u32 madr, u32 bcr, u32 chcr);  // GPU DMA
void psxDma3(u32 madr, u32 bcr, u32 chcr);  // CDROM DMA
void psxDma4(u32 madr, u32 bcr, u32 chcr);  // SPU DMA
void psxDma6(u32 madr, u32 bcr, u32 chcr);  // GPU OTC DMA

// You're missing channel-specific handlers entirely
// Need to add: void dma_channel_2_handler(Interconnect* inter, DmaChannel* ch);
// Need to add: void dma_channel_3_handler(Interconnect* inter, DmaChannel* ch);
// Need to add: void dma_channel_4_handler(Interconnect* inter, DmaChannel* ch);
// Need to add: void dma_channel_6_handler(Interconnect* inter, DmaChannel* ch);
```

#### **Missing Function 2: DMA Transfer Execution**
```c
// PCSX ReARMed has this in psxDma2()
void psxDma2(u32 madr, u32 bcr, u32 chcr) { // GPU
    u32 words = (bcr >> 16) * (bcr & 0xffff);
    
    switch (chcr) {
        case 0x01000200: // vram2mem
            GPU_readDataMem(ptr, words_copy);
            set_event(PSXINT_GPUDMA, words / 4);
            return;
        case 0x01000201: // mem2vram
            GPU_writeDataMem(ptr, words_copy);
            set_event(PSXINT_GPUDMA, words / 4);
            return;
        case 0x01000401: // dma chain
            // Linked list processing
            break;
    }
}

// You're missing DMA transfer execution entirely
// Need to add: void dma_execute_transfer(Interconnect* inter, DmaChannel* ch);
```

### **2. Event System Integration**

#### **Missing Event Scheduling:**
```c
// PCSX ReARMed schedules DMA events
set_event(PSXINT_GPUDMA, words / 4);
set_event(PSXINT_SPUDMA, words * 4 * 4);
set_event(PSXINT_MDECOUTDMA, words * 4);
set_event(PSXINT_MDECINDMA, words * 4);

// You're missing event scheduling entirely
// Need to add: interconnect_schedule_event(inter, PSXINT_GPUDMA, cycles);
```

#### **Missing Interrupt Generation:**
```c
// PCSX ReARMed generates DMA interrupts
void gpuInterrupt() {
    if (HW_DMA2_CHCR & SWAP32(0x01000000)) {
        HW_DMA2_CHCR &= SWAP32(~0x01000000);
        DMA_INTERRUPT(2);
    }
}

// You're missing interrupt generation entirely
// Need to add: interconnect_request_irq(inter, IRQ_DMA, "DMA");
```

### **3. Memory Validation and Bounds Checking**

#### **Missing Memory Validation:**
```c
// PCSX ReARMed has this
static inline void *getDmaRam(u32 madr, u32 *max_words) {
    if (!(madr & 0x800000)) {
        madr &= 0x1ffffc;
        *max_words = (0x200000 - madr) / 4;
        return psxM + madr;
    }
    return INVALID_PTR;
}

// You're missing memory validation entirely
// Need to add: void* dma_validate_memory(Interconnect* inter, uint32_t addr, uint32_t* max_words);
```

### **4. Linked List Support**

#### **Missing Linked List Processing:**
```c
// PCSX ReARMed has linked list support for GPU
case 0x01000401: // dma chain
    // Process linked list of GPU commands
    do {
        addr &= 0x1ffffc;
        size += psxMu8(addr + 3);
        addr = psxMu32(addr & ~0x3) & 0xffffff;
        size += 1;
    } while (!(addr & 0x800000));

// You're missing linked list support entirely
// Need to add: uint32_t dma_process_linked_list(Interconnect* inter, uint32_t addr);
```

### **5. DMA Transfer Timing**

#### **Missing Transfer Timing:**
```c
// PCSX ReARMed has timing for DMA transfers
psxRegs.gpuIdleAfter = psxRegs.cycle + words / 4 + 16;
set_event(PSXINT_GPUDMA, words / 4);

// You're missing transfer timing entirely
// Need to add: uint32_t dma_calculate_transfer_cycles(DmaChannel* ch, uint32_t words);
```

---

## 🔧 **WHAT YOU NEED TO ADD**

### **Step 1: Add Channel-Specific Handlers**

#### **Add to `include/dma.h`:**
```c
// Channel-specific DMA handlers
void dma_channel_2_handler(Interconnect* inter, DmaChannel* ch);  // GPU
void dma_channel_3_handler(Interconnect* inter, DmaChannel* ch);  // CDROM
void dma_channel_4_handler(Interconnect* inter, DmaChannel* ch);  // SPU
void dma_channel_6_handler(Interconnect* inter, DmaChannel* ch);  // GPU OTC

// DMA transfer execution
void dma_execute_transfer(Interconnect* inter, DmaChannel* ch);

// Memory validation
void* dma_validate_memory(Interconnect* inter, uint32_t addr, uint32_t* max_words);

// Linked list processing
uint32_t dma_process_linked_list(Interconnect* inter, uint32_t addr);

// Transfer timing
uint32_t dma_calculate_transfer_cycles(DmaChannel* ch, uint32_t words);
```

#### **Add to `src/dma.c`:**
```c
// Memory validation function
void* dma_validate_memory(Interconnect* inter, uint32_t addr, uint32_t* max_words) {
    if (!(addr & 0x800000)) {
        addr &= 0x1ffffc;
        *max_words = (0x200000 - addr) / 4;
        return inter->ram->data + addr;
    }
    return NULL;
}

// Channel 2 (GPU) handler
void dma_channel_2_handler(Interconnect* inter, DmaChannel* ch) {
    uint32_t words = ch->block_count * ch->block_size;
    uint32_t* ptr;
    uint32_t max_words;
    
    ptr = dma_validate_memory(inter, ch->base_addr, &max_words);
    if (!ptr) {
        LOG_ERROR("Invalid DMA2 address: 0x%08x\n", ch->base_addr);
        return;
    }
    
    uint32_t chcr = channel_get_control(ch);
    switch (chcr) {
        case 0x01000200: // vram2mem
            // TODO: Implement GPU read
            break;
        case 0x01000201: // mem2vram
            // TODO: Implement GPU write
            break;
        case 0x01000401: // dma chain
            // TODO: Implement linked list
            break;
    }
    
    // Schedule completion event
    uint32_t cycles = dma_calculate_transfer_cycles(ch, words);
    interconnect_schedule_event(inter, PSXINT_GPUDMA, cycles);
}

// Channel 4 (SPU) handler
void dma_channel_4_handler(Interconnect* inter, DmaChannel* ch) {
    uint32_t words = ch->block_count * ch->block_size;
    uint16_t* ptr;
    uint32_t max_words;
    
    ptr = dma_validate_memory(inter, ch->base_addr, &max_words);
    if (!ptr) {
        LOG_ERROR("Invalid DMA4 address: 0x%08x\n", ch->base_addr);
        return;
    }
    
    uint32_t chcr = channel_get_control(ch);
    switch (chcr) {
        case 0x01000201: // cpu to spu
            // TODO: Implement SPU write
            break;
        case 0x01000200: // spu to cpu
            // TODO: Implement SPU read
            break;
    }
    
    // Schedule completion event
    uint32_t cycles = dma_calculate_transfer_cycles(ch, words);
    interconnect_schedule_event(inter, PSXINT_SPUDMA, cycles);
}

// DMA transfer execution
void dma_execute_transfer(Interconnect* inter, DmaChannel* ch) {
    uint32_t channel_index = ch - inter->dma.channels;
    
    switch (channel_index) {
        case 2: // GPU
            dma_channel_2_handler(inter, ch);
            break;
        case 3: // CDROM
            dma_channel_3_handler(inter, ch);
            break;
        case 4: // SPU
            dma_channel_4_handler(inter, ch);
            break;
        case 6: // GPU OTC
            dma_channel_6_handler(inter, ch);
            break;
        default:
            LOG_WARN("Unhandled DMA channel %d\n", channel_index);
            break;
    }
}

// Transfer timing calculation
uint32_t dma_calculate_transfer_cycles(DmaChannel* ch, uint32_t words) {
    // Basic timing: 4 cycles per word for most transfers
    return words * 4;
}
```

### **Step 2: Add Event System Integration**

#### **Update `dma_channel_done()`:**
```c
void dma_channel_done(DmaChannel* ch) {
    ch->enable = false;
    ch->trigger = false;
    
    // Set interrupt flag
    // TODO: Add interrupt generation
    // if (dma->channel_irq_enable & (1 << channel_index)) {
    //     interconnect_request_irq(inter, IRQ_DMA, "DMA");
    // }
}
```

### **Step 3: Add Interconnect Integration**

#### **Update `interconnect.c` to call DMA execution:**
```c
// In interconnect_store32, when DMA channel becomes active
if (channel_became_active) {
    dma_execute_transfer(inter, &inter->dma.channels[channel_index]);
}
```

---

## 🎯 **IMPLEMENTATION PRIORITY**

### **CRITICAL (Blocking Boot)**
1. **Add memory validation** - Essential for safe DMA transfers
2. **Add basic transfer execution** - Essential for DMA functionality
3. **Add event system integration** - Essential for timing coordination
4. **Add interrupt generation** - Essential for DMA completion

### **HIGH PRIORITY**
5. **Add GPU DMA handlers** - Essential for graphics
6. **Add SPU DMA handlers** - Essential for sound
7. **Add CDROM DMA handlers** - Essential for CD access
8. **Add linked list support** - Essential for GPU command lists

### **MEDIUM PRIORITY**
9. **Add transfer timing** - For accurate emulation
10. **Add bounds checking** - For stability
11. **Add error handling** - For robustness

---

## 📋 **NEXT STEPS**

1. **Add memory validation function** - `dma_validate_memory()`
2. **Add channel-specific handlers** - Start with GPU DMA (Channel 2)
3. **Add event system integration** - Schedule DMA completion events
4. **Add interrupt generation** - Generate DMA interrupts on completion
5. **Add transfer execution** - Execute actual DMA transfers

**The DMA system is critical for data transfer between components. Without proper DMA handlers, your GPU, SPU, and CDROM won't work correctly.**

Would you like me to help you implement the DMA system, or should we move on to analyze the next component (SPU or CDROM)? 