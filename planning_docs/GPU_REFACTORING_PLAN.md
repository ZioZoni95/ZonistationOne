# GPU Refactoring Plan - DuckStation Architecture
**Date**: January 7, 2026  
**Goal**: Fully modular GPU with optimal complexity  
**Architecture**: DuckStation-style with working GPU threading

---

## 🎯 **Module Structure** (DuckStation-style)

### Core Modules
```
src/gpu/
├── gpu_core.c          # Main GPU state, MMIO, DMA interface
├── gpu_commands.c      # GP0/GP1 command dispatch and handling  
├── gpu_rendering.c     # Drawing primitives (triangles, quads, rects, lines)
├── gpu_vram.c          # VRAM transfers (CPU↔VRAM, VRAM↔VRAM)
├── gpu_timing.c        # CRTC timing, scanlines, VBlank
└── gpu_state.c         # Save states, reset, initialization

include/gpu/
├── gpu_core.h          # GPU state structure and public API
├── gpu_types.h         # Enums, constants, bitfields
├── gpu_commands.h      # Command handler prototypes
└── gpu_internal.h      # Internal shared structures
```

---

## 📊 **Current vs Target Architecture**

### Current (Monolithic)
```
gpu.c (1255 lines)
├── State management
├── GP0/GP1 commands
├── Drawing primitives
├── VRAM operations
├── CRTC timing
└── DMA handling (mixed)
```

### Target (Modular)
```
gpu_core.c (~300 lines)
├── State structure
├── MMIO read/write
├── DMA interface
└── Initialization/reset

gpu_commands.c (~400 lines)
├── GP0 dispatch table
├── GP1 dispatch table
└── Command parameter parsing

gpu_rendering.c (~300 lines)
├── Draw primitives
├── Clipping logic
└── Renderer calls

gpu_vram.c (~200 lines)
├── CPU→VRAM transfers
├── VRAM→CPU transfers
└── VRAM→VRAM copies

gpu_timing.c (~150 lines)
├── CRTC state
├── Scanline tracking
└── VBlank generation
```

---

## 🔑 **Key DuckStation Concepts to Adopt**

### 1. GPUSTAT Register (Centralized)
```c
typedef union {
    uint32_t bits;
    struct {
        uint32_t texture_page_x_base : 4;       // [3:0]
        uint32_t texture_page_y_base : 1;       // [4]
        uint32_t semi_transparency_mode : 2;    // [6:5]
        uint32_t texture_color_mode : 2;        // [8:7]
        uint32_t dither_enable : 1;             // [9]
        uint32_t draw_to_displayed_field : 1;   // [10]
        uint32_t set_mask_while_drawing : 1;    // [11]
        uint32_t check_mask_before_draw : 1;    // [12]
        uint32_t interlaced_field : 1;          // [13]
        uint32_t reverse_flag : 1;              // [14]
        uint32_t texture_disable : 1;           // [15]
        uint32_t horizontal_resolution_2 : 1;   // [16]
        uint32_t horizontal_resolution_1 : 2;   // [18:17]
        uint32_t vertical_resolution : 1;       // [19]
        uint32_t pal_mode : 1;                  // [20]
        uint32_t display_area_color_depth_24 : 1; // [21]
        uint32_t vertical_interlace : 1;        // [22]
        uint32_t display_disable : 1;           // [23]
        uint32_t interrupt_request : 1;         // [24]
        uint32_t dma_data_request : 1;          // [25]
        uint32_t ready_to_receive_cmd : 1;      // [26]
        uint32_t ready_to_send_vram : 1;        // [27]
        uint32_t ready_to_receive_dma : 1;      // [28]
        uint32_t dma_direction : 2;             // [30:29]
        uint32_t drawing_even_odd_lines : 1;    // [31]
    };
} GPUSTAT;
```

### 2. Drawing Area (Clamped)
```c
typedef struct {
    uint16_t left;    // X1 (0-1023)
    uint16_t top;     // Y1 (0-511)
    uint16_t right;   // X2 (0-1023)
    uint16_t bottom;  // Y2 (0-511)
} GPUDrawingArea;
```

### 3. Drawing Offset (Signed)
```c
typedef struct {
    int16_t x;  // -1024 to +1023
    int16_t y;  // -1024 to +1023
} GPUDrawingOffset;
```

### 4. CRTC State (Separate)
```c
typedef struct {
    uint16_t display_vram_left;
    uint16_t display_vram_top;
    uint16_t display_vram_width;
    uint16_t display_vram_height;
    uint16_t horizontal_display_start;
    uint16_t horizontal_display_end;
    uint16_t vertical_display_start;
    uint16_t vertical_display_end;
    uint32_t dot_clock_divider;
    uint32_t current_scanline;
    uint32_t current_tick_in_scanline;
} CRTCState;
```

### 5. Command FIFO (Lock-Free)
```c
typedef struct {
    uint64_t* fifo_buffer;     // [address << 32 | value]
    uint32_t fifo_size;
    atomic_uint32_t read_ptr;
    atomic_uint32_t write_ptr;
} CommandFIFO;
```

---

## 🚨 **DMA Integration** (Critical!)

### Current DMA Issue
- DMA and GPU are tightly coupled in gpu.c
- Need clean separation

### DuckStation Approach
```c
// In DMA (dma.c):
void dma_channel_gpu_write(uint32_t* data, uint32_t word_count) {
    for (uint32_t i = 0; i < word_count; i++) {
        gpu_dma_write(data[i]);  // Clean interface
    }
    gpu_end_dma_write();
}

// In GPU (gpu_core.c):
void gpu_dma_write(uint32_t value) {
    // Push to command FIFO
    gpu_write_gp0(gpu, value);
}

void gpu_end_dma_write() {
    // Process queued commands
    gpu_process_fifo(gpu);
}
```

### Key Separation
- **DMA**: Manages channel state, burst mode, linked list traversal
- **GPU**: Receives data via clean API, processes commands
- **No circular dependencies**: DMA → GPU only (not GPU → DMA)

---

## 📝 **Implementation Steps**

### Phase 1: Extract Core State (Day 1)
1. Create `include/gpu/gpu_types.h` with all enums/structs
2. Create `include/gpu/gpu_core.h` with main GPU state
3. Move GPUSTAT to bitfield union
4. Extract drawing area/offset structures
5. Extract CRTC state structure
6. **Build test**: Ensure it compiles

### Phase 2: Modularize Commands (Day 2)
1. Create `src/gpu/gpu_commands.c`
2. Extract all GP0 handlers to gpu_commands.c
3. Extract all GP1 handlers to gpu_commands.c
4. Create dispatch tables (function pointer arrays)
5. Keep command buffer management in gpu_core.c
6. **Build test**: Ensure rendering still works

### Phase 3: Separate Rendering (Day 3)
1. Create `src/gpu/gpu_rendering.c`
2. Extract primitive drawing functions
3. Extract texture mapping logic
4. Extract clipping calculations
5. Keep renderer interface in gpu_core.c
6. **Build test**: Logo still renders correctly

### Phase 4: Extract VRAM Operations (Day 4)
1. Create `src/gpu/gpu_vram.c`
2. Extract CPU→VRAM transfer (image load)
3. Extract VRAM→CPU transfer (image store)
4. Extract VRAM→VRAM copy (copy rectangle)
5. **Build test**: VRAM transfers work

### Phase 5: Separate Timing (Day 5)
1. Create `src/gpu/gpu_timing.c`
2. Extract CRTC timing logic
3. Extract scanline tracking
4. Extract VBlank generation
5. **Build test**: VBlank timing correct

### Phase 6: Clean DMA Interface (Day 6)
1. Review src/dma.c GPU channel code
2. Create clean `gpu_dma_write()` / `gpu_dma_read()` API
3. Remove GPU→DMA dependencies
4. Update dma.c to use new API
5. **Build test**: DMA transfers work

### Phase 7: GPU Threading Integration (Day 7)
1. Keep existing gpu_thread.c structure
2. Update FIFO to work with modular GPU
3. Add command batching for threading
4. Test thread safety
5. **Build test**: Threading still works

### Phase 8: Fix Duplication Bug (Day 8)
1. Fix interlaced field handling
2. Fix display area mapping
3. Fix double-rendering issue
4. Verify CRTC timing
5. **Validation**: Logo renders once, correctly

---

## ✅ **Success Criteria**

- ✅ **Modularity**: GPU split into 5-6 clean modules
- ✅ **Complexity**: Each module < 400 lines
- ✅ **DMA**: Clean separation (DMA → GPU, not GPU → DMA)
- ✅ **Threading**: GPU thread still works
- ✅ **Rendering**: Logo displays correctly (no duplication)
- ✅ **Performance**: No regression
- ✅ **DuckStation Parity**: Core architecture matches

---

## 🔍 **Files to Modify**

### New Files (Create)
- `include/gpu/gpu_types.h`
- `include/gpu/gpu_core.h`
- `include/gpu/gpu_commands.h`
- `include/gpu/gpu_internal.h`
- `src/gpu/gpu_core.c`
- `src/gpu/gpu_commands.c`
- `src/gpu/gpu_rendering.c`
- `src/gpu/gpu_vram.c`
- `src/gpu/gpu_timing.c`

### Modify
- `src/dma.c` (clean GPU interface)
- `src/gpu_thread.c` (adapt to new GPU)
- `src/interconnect.c` (update GPU includes)
- `src/main.c` (update GPU includes)
- `Makefile` (add new GPU modules)

### Backup (Already Done)
- `backup/gpu_old/gpu.c`
- `backup/gpu_old/gpu.h`
- `backup/gpu_old/dma.c`
- `backup/gpu_old/dma.h`
- `backup/gpu_old/gpu_thread.c`
- `backup/gpu_old/gpu_thread.h`

---

**Ready to start Phase 1?**
