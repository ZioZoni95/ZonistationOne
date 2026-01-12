# GPU Modular Refactoring Plan
## Comprehensive Analysis and Implementation Strategy

**Status**: In Progress  
**Date**: 2025-01-06  
**Reference**: DuckStation GPU Architecture (duckstation/src/core/gpu*.cpp)

---

## Executive Summary

Current GPU implementation is monolithic (1239 lines in `src/gpu.c`). This refactoring will split it into modular components following DuckStation's proven architecture while **preserving all three critical bug fixes**:

1. ✅ **UV Coordinate Fix**: `(w-1, h-1)` pattern
2. ✅ **Handler Mapping Fix**: 0x78 → `gp0_rect_tex_16x16_opaque` (3 words, textured)
3. ✅ **DMA Packet Boundary Detection**: Force new command when `looks_like_new_cmd && words_remaining > 2`

---

## Part 1: Architecture Comparison

### DuckStation Structure (Reference)

```
duckstation/src/core/
├── gpu.h                      # Main GPU class interface
├── gpu.cpp                    # Core GPU state machine
├── gpu_types.h                # Type definitions, enums, constants
├── gpu_commands.cpp           # GP0/GP1 command parsing ⭐
├── gpu_backend.h/.cpp         # Backend abstraction interface
├── gpu_hw.h/.cpp              # Hardware (OpenGL) renderer
├── gpu_sw.h/.cpp              # Software renderer
├── gpu_sw_rasterizer.cpp      # Software rasterization engine
├── gpu_hw_texture_cache.cpp   # Texture caching system
├── gpu_shadergen.cpp          # Shader generation
└── gpu_thread_commands.h      # Threading support
```

**Key Design Patterns**:
- **Command dispatch via function pointer table**: `s_GP0_command_handler_table[256]`
- **Blitter state machine**: `Idle`, `WritingVRAM`, `ReadingVRAM`, `DrawingPolyLine`
- **FIFO-based command buffering**: `m_fifo.GetSize()`, `FifoPeek()`, `FifoPop()`
- **Backend abstraction**: `GPUBackend::PushCommand()` for renderer independence
- **Per-command tick accounting**: `AddCommandTicks(n)` for accurate timing

### ZonistationOne Current Structure

```
ZonistationOne/
├── include/
│   ├── gpu.h                 # Legacy wrapper (forwards to gpu/gpu_core.h)
│   └── gpu/
│       ├── gpu_types.h       # ✅ Type defs complete (248 lines)
│       ├── gpu_core.h        # ✅ Core header (partial)
│       └── gpu_commands.h    # ⏳ Empty/stub
├── src/
│   ├── gpu.c                 # 🔴 MONOLITHIC (1239 lines) - TO SPLIT
│   └── gpu/
│       └── gpu_core.c        # ✅ Initialization only (partial)
└── src/renderer.c            # OpenGL backend
```

**Problems with Current Implementation**:
1. **All logic in one file**: Commands, rendering, VRAM, timing mixed
2. **No backend abstraction**: Direct calls to `renderer_push_quad()`
3. **Manual command dispatch**: Giant switch statement (lines 950-1000)
4. **Words buffering confused**: `gp0_words_remaining` vs `gp0_command_buffer.count`
5. **DMA packet handling hacky**: Recent fix works but not architecturally clean

---

## Part 2: Critical Bug Fixes to Preserve

### Bug Fix #1: UV Coordinate Calculation

**Location**: `draw_rectangle()` in `src/gpu.c:468-470`

```c
// ✅ CORRECT (Current Fixed Version)
t[1].u = tex->u + (w - 1);
t[1].v = tex->v + (h - 1);

// ❌ WRONG (Original Bug)
// t[1].u = tex->u + w;
// t[1].v = tex->v + h;
```

**Why**: Texture coordinates are inclusive (0-15 for 16x16 rect), not exclusive.

**Migration**: Move to new `gpu_rendering.c::draw_rectangle_internal()`

---

### Bug Fix #2: Command Handler Mapping

**Location**: `gpu_gp0_handle_word()` in `src/gpu.c:964-969`

```c
// ✅ CORRECT (Current Fixed Version)
case 0x78: expected_len = 3; handler = gp0_rect_tex_16x16_opaque; break; // TEXTURED
case 0x7A: expected_len = 3; handler = gp0_rect_tex_16x16_opaque; break; // TEXTURED semi
case 0x7C: expected_len = 2; handler = gp0_rect_16x16_opaque; break;     // NON-TEXTURED

// ❌ WRONG (Original Bug)
// case 0x78: expected_len = 2; handler = gp0_rect_16x16_opaque; break;
// case 0x7C: expected_len = 3; handler = gp0_rect_tex_16x16_opaque; break;
```

**Why**: Bit 26 indicates textured (1) vs non-textured (0). Must check correctly.

**Pattern**:
- `0x78 = 0b01111000`: Bit 26 = 1 → **Textured** (3 words: cmd, pos, uv_clut)
- `0x7C = 0b01111100`: Bit 26 = 0 → **Non-Textured** (2 words: cmd, pos)

**Migration**: Build handler table in new `gpu_commands.c::build_gp0_command_table()`

---

### Bug Fix #3: DMA Packet Boundary Detection

**Location**: `gpu_gp0_handle_word()` in `src/gpu.c:918-938`

```c
// ✅ CORRECT (Current Fixed Version)
uint8_t opcode_check = (uint8_t)(command >> 24);
uint8_t cmd_type = (opcode_check >> 5) & 0x7; // Top 3 bits
bool is_render_cmd = (cmd_type >= 1 && cmd_type <= 3); // 001=poly, 010=line, 011=rect
bool is_env_cmd = (cmd_type == 7 && opcode_check >= 0xE0); // 111=environment
bool looks_like_new_cmd = (gpu->gp0_words_remaining > 2) && (is_render_cmd || is_env_cmd);

if (gpu->gp0_words_remaining == 0 || looks_like_new_cmd) {
    if (looks_like_new_cmd) {
        LOG_GPU_WARN("GPU: Forcing new command 0x%02X while %d words remaining for 0x%02X (DMA packet boundary)\n", 
                    opcode_check, gpu->gp0_words_remaining, gpu->gp0_current_opcode);
    }
    // Reset state and start new command
    // ...
}
```

**Why**: DMA linked-list mode sends packets that can end mid-command. New command arriving while waiting for previous command data indicates packet boundary.

**Example Scenario**:
1. GP0(0x2C) textured quad expects 9 words
2. DMA packet ends after word 2
3. New packet starts with GP0(0x78)
4. Without fix: 0x78 treated as data word #3 for 0x2C → **command corruption**
5. With fix: Detect 0x78 as new command, force reset → **correct parsing**

**Migration**: Integrate into new `gpu_commands.c::gp0_dispatch_command()` state machine

---

## Part 3: Modular File Structure

### Target Directory Layout

```
ZonistationOne/
├── include/gpu/
│   ├── gpu_types.h          # ✅ DONE (248 lines)
│   ├── gpu_core.h           # 🔄 EXPAND (add public API)
│   ├── gpu_commands.h       # ⏳ NEW (command handler interface)
│   ├── gpu_rendering.h      # ⏳ NEW (drawing functions)
│   ├── gpu_vram.h           # ⏳ NEW (VRAM operations)
│   └── gpu_timing.h         # ⏳ NEW (CRTC/display timing)
├── src/gpu/
│   ├── gpu_core.c           # 🔄 EXPAND (state machine)
│   ├── gpu_commands.c       # ⏳ NEW (GP0/GP1 handlers)
│   ├── gpu_rendering.c      # ⏳ NEW (draw primitives)
│   ├── gpu_vram.c           # ⏳ NEW (VRAM read/write/copy)
│   └── gpu_timing.c         # ⏳ NEW (display mapping)
└── include/gpu.h            # ✅ DONE (legacy wrapper)
```

---

## Part 4: Module Specifications

### Module 1: `gpu_commands.c/.h` - Command Parsing Layer

**Purpose**: GP0/GP1 command dispatch, FIFO management, packet handling

**Responsibilities**:
- GP0 command table generation (256 entries)
- Command word buffering and validation
- DMA packet boundary detection ⭐
- GP1 register commands
- Blitter state transitions

**Public API**:
```c
// Command dispatch
void gp0_dispatch_command(GPU* gpu, uint32_t command);
void gp1_dispatch_command(GPU* gpu, uint8_t opcode, uint32_t value);

// FIFO management
void gp0_push_fifo(GPU* gpu, uint32_t word);
uint32_t gp0_pop_fifo(GPU* gpu);

// Command buffer
void gp0_clear_buffer(GPU* gpu);
void gp0_push_word(GPU* gpu, uint32_t word);

// Handler table
typedef bool (*GP0Handler)(GPU* gpu);
void build_gp0_command_table(GP0Handler table[256]);
```

**Key Functions** (Internal):
```c
// GP0 Rendering Commands (0x20-0x7F)
static bool gp0_polygon_handler(GPU* gpu);
static bool gp0_line_handler(GPU* gpu);
static bool gp0_rectangle_handler(GPU* gpu);

// GP0 VRAM Commands (0x80-0xDF)
static bool gp0_vram_copy_handler(GPU* gpu);
static bool gp0_vram_load_handler(GPU* gpu);
static bool gp0_vram_store_handler(GPU* gpu);

// GP0 Environment Commands (0xE0-0xFF)
static bool gp0_draw_mode_handler(GPU* gpu);
static bool gp0_texture_window_handler(GPU* gpu);
static bool gp0_drawing_area_handler(GPU* gpu);
static bool gp0_drawing_offset_handler(GPU* gpu);

// GP1 Commands
static void gp1_reset(GPU* gpu);
static void gp1_display_mode(GPU* gpu, uint32_t value);
static void gp1_dma_direction(GPU* gpu, uint32_t value);
```

**Lines from `gpu.c` to Migrate**:
- Lines 58-70: Handler forward declarations
- Lines 89-117: Buffer management functions
- Lines 119-251: GP1 handlers
- Lines 253-301: GP0 NOP/cache/fill commands
- Lines 918-1018: Command dispatch and packet detection ⭐
- Lines 1020-1065: `gpu_gp0()` entry point

**Estimated Size**: ~500 lines

---

### Module 2: `gpu_rendering.c/.h` - Drawing Primitives

**Purpose**: Render command execution, geometry generation, renderer backend calls

**Responsibilities**:
- Parse rendering commands (polygons, lines, rectangles)
- Generate vertex data with correct coordinates
- Apply drawing offset, texture coordinates, colors
- Call renderer backend (OpenGL) with prepared data
- Handle transparency, dithering, raw texture modes

**Public API**:
```c
// High-level rendering interface
void gpu_render_polygon(GPU* gpu, uint8_t opcode);
void gpu_render_line(GPU* gpu, uint8_t opcode);
void gpu_render_rectangle(GPU* gpu, uint8_t opcode);

// Internal drawing primitives (used by command handlers)
void draw_triangle_internal(GPU* gpu, const GPUVertex vertices[3], bool textured, bool shaded);
void draw_quad_internal(GPU* gpu, const GPUVertex vertices[4], bool textured, bool shaded);
void draw_line_internal(GPU* gpu, const GPUVertex vertices[2], bool shaded);
void draw_rectangle_internal(GPU* gpu, int16_t x, int16_t y, uint16_t w, uint16_t h, 
                             const RendererColor* color, bool textured, 
                             const RendererTexCoord* tex, uint16_t clut, uint16_t tpage);
```

**Key Data Structures**:
```c
typedef struct {
    int16_t x, y;            // Position
    uint8_t r, g, b;         // Color
    uint8_t u, v;            // Texture coordinates
} GPUVertex;
```

**Lines from `gpu.c` to Migrate**:
- Lines 303-512: `draw_rectangle()` with UV fix ⭐
- Lines 514-665: Specific rectangle handlers (1x1, 8x8, 16x16, variable, textured, non-textured)
- Lines 667-696: `gp0_rect_tex_16x16_opaque()` ⭐ (critical for font rendering)
- Lines 698-785: Other rectangle variants
- Lines 787-819: Polygon handlers (quad_mono, quad_texture_blend, quad_shaded, triangle_shaded)

**Critical Section** - `gp0_rect_tex_16x16_opaque()`:
```c
static void gp0_rect_tex_16x16_opaque(Gpu* gpu) {
    LOG_GPU_INFO(">>> gp0_rect_tex_16x16_opaque() CALLED! buffer count=%d\n", 
                 gpu->gp0_command_buffer.count);
    
    if (gpu->gp0_command_buffer.count < 3) {
        LOG_GPU_ERROR("Not enough words for 16x16 textured rect");
        return;
    }

    uint32_t cmd = gpu->gp0_command_buffer.buffer[0];
    uint32_t vtx = gpu->gp0_command_buffer.buffer[1];
    uint32_t uv_clut = gpu->gp0_command_buffer.buffer[2];

    // Extract color from cmd
    RendererColor col = {
        .r = (GLubyte)(cmd & 0xFF),
        .g = (GLubyte)((cmd >> 8) & 0xFF),
        .b = (GLubyte)((cmd >> 16) & 0xFF)
    };

    // Extract position
    int16_t x = (int16_t)(vtx & 0xFFFF);
    int16_t y = (int16_t)(vtx >> 16);

    // Extract UV coordinates
    uint8_t u = (uint8_t)(uv_clut & 0xFF);
    uint8_t v = (uint8_t)((uv_clut >> 8) & 0xFF);

    // Extract CLUT and Texture Page
    uint16_t clut = (uint16_t)((uv_clut >> 16) & 0xFFFF);
    uint16_t tpage = gpu->texture_page_raw;

    RendererTexCoord tex = { .u = u, .v = v };
    bool raw_texture = (cmd >> 25) & 1;

    draw_rectangle(gpu, x, y, 16, 16, col, true, raw_texture, &tex, clut, tpage);
}
```

**Estimated Size**: ~600 lines

---

### Module 3: `gpu_vram.c/.h` - VRAM Operations

**Purpose**: VRAM read/write/copy operations, CPU↔VRAM transfers, mask bit handling

**Responsibilities**:
- GP0(0xA0): Image Load (CPU → VRAM)
- GP0(0xC0): Image Store (VRAM → CPU)
- GP0(0x80): VRAM Copy (VRAM → VRAM)
- Masked writes (check/set mask bits)
- VRAM texture upload to OpenGL

**Public API**:
```c
// VRAM transfers
void vram_load_start(GPU* gpu, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void vram_load_word(GPU* gpu, uint32_t data); // Process one 32-bit word (2 pixels)
void vram_store_start(GPU* gpu, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
uint32_t vram_store_read(GPU* gpu);

// VRAM copy
void vram_copy_rect(GPU* gpu, uint16_t src_x, uint16_t src_y,
                   uint16_t dst_x, uint16_t dst_y,
                   uint16_t w, uint16_t h);

// Direct access
uint16_t vram_read16(const GPU* gpu, uint32_t offset);
void vram_write16(GPU* gpu, uint32_t offset, uint16_t value);
void vram_write_masked(GPU* gpu, uint32_t offset, uint16_t value);

// Renderer sync
void vram_upload_to_renderer(GPU* gpu);
```

**Lines from `gpu.c` to Migrate**:
- Lines 821-899: `gp0_image_load()` and data processing
- Lines 901-930: `vram_write_masked()` helper
- Lines 932-1000: Copy rectangle logic (likely in another handler)

**Estimated Size**: ~400 lines

---

### Module 4: `gpu_timing.c/.h` - CRTC and Display Timing

**Purpose**: Display area configuration, CRTC scanout, video mode management

**Responsibilities**:
- GP1(0x05-0x08): Display configuration
- Scanline/dot clock tracking
- Horizontal/vertical sync ranges
- Interlaced mode handling
- Display mapping updates

**Public API**:
```c
// Display configuration
void display_set_vram_start(GPU* gpu, uint16_t x, uint16_t y);
void display_set_horizontal_range(GPU* gpu, uint16_t start, uint16_t end);
void display_set_vertical_range(GPU* gpu, uint16_t start, uint16_t end);
void display_set_mode(GPU* gpu, uint32_t mode_bits);

// Display mapping calculation
void update_display_mapping(GPU* gpu);

// Video timing
uint32_t get_scanline_ticks(const GPU* gpu);
uint32_t get_total_scanlines(const GPU* gpu);
bool is_interlaced_enabled(const GPU* gpu);
```

**Lines from `gpu.c` to Migrate**:
- Lines 153-251: GP1 display commands
- Display mapping calculations (scattered, need to consolidate)
- CRTC register updates

**Estimated Size**: ~300 lines

---

### Module 5: `gpu_core.c/.h` - Core State Management

**Purpose**: GPU initialization, reset, GPUSTAT register, main API entry points

**Responsibilities**:
- `gpu_init_full()` - Full initialization
- `gpu_soft_reset()` - Soft reset (preserve VRAM)
- `gpu_gp0()` - Main GP0 entry point → calls `gp0_dispatch_command()`
- `gpu_gp1()` - Main GP1 entry point → calls `gp1_dispatch_command()`
- GPUSTAT register composition
- DMA direction management

**Current Status**: ✅ Partially complete (initialization moved)

**Additional Work Needed**:
- Move remaining state management from `gpu.c`
- Add GPUSTAT read logic
- Integrate command modules

**Estimated Size**: ~200 lines

---

## Part 5: Migration Strategy

### Phase 1: Create Command Module (PRIORITY)

**Why First**: Commands are entry point, isolate packet handling bug fix

**Steps**:
1. Create `include/gpu/gpu_commands.h`
   - Function prototypes for GP0/GP1 dispatch
   - Handler typedef: `typedef bool (*GP0Handler)(GPU* gpu);`
   
2. Create `src/gpu/gpu_commands.c`
   - Implement `build_gp0_command_table()` with correct 0x78 mapping ⭐
   - Implement `gp0_dispatch_command()` with packet boundary detection ⭐
   - Move GP1 handlers (reset, display, DMA)
   - Move GP0 environment commands (draw mode, texture window, etc.)

3. **Preserve Bug Fixes**:
   - ✅ Handler table entry: `table[0x78] = &gp0_rect_tex_16x16_handler;` (3 words)
   - ✅ Packet boundary logic in dispatch loop

4. Update `gpu_core.c`:
   - `gpu_gp0()` now calls `gp0_dispatch_command()`
   - `gpu_gp1()` now calls `gp1_dispatch_command()`

5. Update `Makefile`:
   ```makefile
   GPU_OBJS = src/gpu/gpu_core.o src/gpu/gpu_commands.o
   ```

**Test**: Build, run, verify logo still displays

---

### Phase 2: Create Rendering Module

**Steps**:
1. Create `include/gpu/gpu_rendering.h`
   - Drawing function prototypes
   
2. Create `src/gpu/gpu_rendering.c`
   - Move `draw_rectangle()` with UV fix ⭐
   - Move all rectangle handlers
   - Move polygon/line handlers
   - Move `gp0_rect_tex_16x16_opaque()` ⭐

3. **Preserve Bug Fixes**:
   - ✅ UV calculation: `tex->u + (w-1)`, `tex->v + (h-1)`

4. Update command module to call rendering functions

5. Update `Makefile`:
   ```makefile
   GPU_OBJS = src/gpu/gpu_core.o src/gpu/gpu_commands.o src/gpu/gpu_rendering.o
   ```

**Test**: Build, run, verify text rendering still works

---

### Phase 3: Create VRAM Module

**Steps**:
1. Create `include/gpu/gpu_vram.h`
2. Create `src/gpu/gpu_vram.c`
   - Move image load/store handlers
   - Move copy rectangle
   - Move masked write logic
3. Update Makefile
4. **Test**: Logo upload, VRAM copy operations

---

### Phase 4: Create Timing Module

**Steps**:
1. Create `include/gpu/gpu_timing.h`
2. Create `src/gpu/gpu_timing.c`
   - Move GP1 display commands
   - Move CRTC calculations
3. Update Makefile
4. **Test**: Display configuration changes

---

### Phase 5: Cleanup and Verification

**Steps**:
1. Remove `src/gpu.c` (now empty)
2. Expand `gpu_core.c` with remaining logic
3. Run full emulator test suite
4. Verify all three bug fixes still work ⭐
5. Check log output matches previous behavior
6. Performance test (no regressions)

---

## Part 6: DuckStation vs ZonistationOne Patterns

### Command Dispatch Comparison

**DuckStation** (Function pointer table):
```cpp
GP0CommandHandlerTable GPU::GenerateGP0CommandHandlerTable() {
    GP0CommandHandlerTable table = {};
    for (u32 i = 0; i < 256; i++)
        table[i] = &GPU::HandleUnknownGP0Command;
    
    table[0x00] = &GPU::HandleNOPCommand;
    table[0x02] = &GPU::HandleFillRectangleCommand;
    // ...
    for (u32 i = 0x20; i <= 0x7F; i++) {
        const GPURenderCommand rc{i << 24};
        switch (rc.primitive) {
            case GPUPrimitive::Polygon:
                table[i] = &GPU::HandleRenderPolygonCommand;
                break;
            case GPUPrimitive::Rectangle:
                table[i] = &GPU::HandleRenderRectangleCommand;
                break;
            // ...
        }
    }
    return table;
}
```

**ZonistationOne** (Current - giant switch):
```c
switch (opcode) {
    case 0x00: expected_len = 1; handler = gp0_nop; break;
    case 0x02: expected_len = 3; handler = gp0_fill_rectangle; break;
    case 0x28: expected_len = 5; handler = gp0_quad_mono_opaque; break;
    // ... 50+ cases ...
    case 0x78: expected_len = 3; handler = gp0_rect_tex_16x16_opaque; break;
    default:
        LOG_ERROR("Unhandled GP0 Opcode 0x%02x", opcode);
        expected_len = 1; handler = gp0_nop; break;
}
```

**Target** (DuckStation-style for ZonistationOne):
```c
void build_gp0_command_table(GP0Handler table[256]) {
    // Default all to unknown
    for (int i = 0; i < 256; i++) {
        table[i] = &gp0_unknown_command;
    }
    
    // Single commands
    table[0x00] = &gp0_nop;
    table[0x01] = &gp0_clear_cache;
    table[0x02] = &gp0_fill_rectangle;
    
    // Rendering commands (0x20-0x7F) - programmatic
    for (int i = 0x20; i <= 0x7F; i++) {
        uint8_t cmd_type = (i >> 5) & 0x7;
        if (cmd_type == 1) { // Polygon
            table[i] = &gp0_polygon_handler;
        } else if (cmd_type == 2) { // Line
            table[i] = &gp0_line_handler;
        } else if (cmd_type == 3) { // Rectangle
            table[i] = &gp0_rectangle_handler;
        }
    }
    
    // VRAM operations
    for (int i = 0x80; i <= 0x9F; i++) table[i] = &gp0_vram_copy;
    for (int i = 0xA0; i <= 0xBF; i++) table[i] = &gp0_vram_load;
    for (int i = 0xC0; i <= 0xDF; i++) table[i] = &gp0_vram_store;
    
    // Environment commands
    table[0xE1] = &gp0_draw_mode;
    table[0xE2] = &gp0_texture_window;
    table[0xE3] = &gp0_drawing_area_top_left;
    table[0xE4] = &gp0_drawing_area_bottom_right;
    table[0xE5] = &gp0_drawing_offset;
    table[0xE6] = &gp0_mask_bit_setting;
}
```

---

### FIFO Management Comparison

**DuckStation** (Circular queue):
```cpp
FIFOQueue<u64, MAX_FIFO_SIZE> m_fifo;

u32 FifoPeek(u32 index) const {
    return Truncate32(m_fifo.Peek(index));
}

u32 FifoPop() {
    return Truncate32(m_fifo.Pop());
}

void DMAWrite(u32 address, u32 value) {
    m_fifo.Push((ZeroExtend64(address) << 32) | ZeroExtend64(value));
}
```

**ZonistationOne** (Current - manual buffering):
```c
typedef struct {
    uint32_t buffer[MAX_GPU_COMMAND_WORDS]; // 16 words max
    uint32_t count;
} GP0CommandBuffer;

// Plus separate FIFO:
uint32_t gp0_fifo[16];
uint32_t gp0_fifo_head, gp0_fifo_tail, gp0_fifo_count;
```

**Target** (Keep current simple approach, but clarify roles):
- **FIFO**: Hardware-level buffering (16 entries)
- **Command buffer**: Multi-word command assembly
- DMA writes → FIFO → Command buffer → Handler

---

### Packet Boundary Handling

**DuckStation** (Implicitly handled by FIFO and command completion):
```cpp
void GPU::TryExecuteCommands() {
    while (m_pending_command_ticks <= m_max_run_ahead && !m_fifo.IsEmpty()) {
        // Each handler returns true when complete, false when needs more words
        const u32 command = FifoPeek(0) >> 24;
        if ((this->*s_GP0_command_handler_table[command])()) {
            continue; // Command complete, try next
        } else {
            return; // Command incomplete, wait for more words
        }
    }
}
```

**ZonistationOne** (Current - explicit detection):
```c
// DMA packet boundary detection (CRITICAL BUG FIX)
uint8_t cmd_type = (opcode_check >> 5) & 0x7;
bool is_render_cmd = (cmd_type >= 1 && cmd_type <= 3);
bool is_env_cmd = (cmd_type == 7 && opcode_check >= 0xE0);
bool looks_like_new_cmd = (gpu->gp0_words_remaining > 2) && (is_render_cmd || is_env_cmd);

if (gpu->gp0_words_remaining == 0 || looks_like_new_cmd) {
    // Start new command
}
```

**Why Different**: DuckStation's FIFO design naturally handles this because handlers explicitly check for enough words (`CHECK_COMMAND_SIZE(num_words)`). ZonistationOne uses `gp0_words_remaining` counter which can get out of sync with DMA packet boundaries.

**Target**: Keep explicit detection, but integrate into state machine properly

---

## Part 7: Testing Checklist

### Before Refactoring
- [ ] Build current code: `make clean && make`
- [ ] Run BIOS: Verify logo + menu background + **text visible**
- [ ] Capture baseline log: `./myps1_emu --log-single-file 2>&1 | tee baseline_log.txt`
- [ ] Check for 0x78 commands: `grep "gp0_rect_tex_16x16_opaque() CALLED" baseline_log.txt`

### After Each Module
- [ ] Build: No warnings/errors
- [ ] Run: Logo displays (tests textured polygons)
- [ ] Run: Background displays (tests flat polygons)
- [ ] Run: Text displays (tests 0x78 rectangles) ⭐
- [ ] Log: Packet boundary warnings present (tests DMA detection)
- [ ] Log: UV values match baseline (tests UV fix)

### Final Verification
- [ ] All three bug fixes confirmed working
- [ ] No visual regressions
- [ ] Log output matches baseline structure
- [ ] Code compiles with `-Wall -Wextra` no warnings
- [ ] Memory leak check: `valgrind ./myps1_emu`

---

## Part 8: Implementation Order

### Week 1: Commands Module
**Day 1-2**: Create header + stub implementations  
**Day 3-4**: Migrate GP0 command table + dispatch  
**Day 5**: Migrate GP1 handlers  
**Day 6**: Testing + bug fix verification  
**Day 7**: Documentation

### Week 2: Rendering Module
**Day 1-2**: Create header + migrate draw_rectangle  
**Day 3-4**: Migrate all rectangle handlers  
**Day 5**: Migrate polygon/line handlers  
**Day 6**: Testing (focus on text rendering)  
**Day 7**: Performance testing

### Week 3: VRAM + Timing Modules
**Day 1-2**: VRAM module  
**Day 3-4**: Timing module  
**Day 5-6**: Integration testing  
**Day 7**: Final cleanup

### Week 4: Polish and Documentation
**Day 1-2**: Remove old gpu.c  
**Day 3**: Full regression testing  
**Day 4**: Update all documentation  
**Day 5**: Code review + cleanup  
**Day 6-7**: Performance optimization

---

## Part 9: Risks and Mitigation

### Risk 1: Bug Fix Lost During Migration
**Likelihood**: High  
**Impact**: Critical (text breaks again)  
**Mitigation**:
- Document exact line numbers of all three fixes
- Create unit tests for UV calculation
- Verify handler table entries with script
- Test after EVERY phase

### Risk 2: Performance Regression
**Likelihood**: Medium  
**Impact**: Medium  
**Mitigation**:
- Profile before/after with `perf`
- Keep hot paths inline (UV calc, coordinate transform)
- Use `-O3` optimizations
- Avoid unnecessary function call overhead

### Risk 3: Integration Breakage
**Likelihood**: Medium  
**Impact**: High  
**Mitigation**:
- Update `Makefile` incrementally
- Keep `gpu.h` wrapper for backward compatibility
- Don't change external API (interconnect, renderer)
- Parallel compilation: Keep old gpu.c until all modules work

### Risk 4: DMA Packet Detection Too Aggressive
**Likelihood**: Low (already tuned)  
**Impact**: High (breaks logo rendering)  
**Mitigation**:
- Keep exact same detection logic
- Add test cases for GP0(0x2C) 9-word commands
- Monitor for false positives in logs

---

## Part 10: Code Quality Standards

### Naming Conventions
- **Public API**: `gpu_command_dispatch()`, `gpu_render_polygon()`
- **Internal static**: `gp0_rect_tex_16x16_handler()`, `parse_vertex_data()`
- **Constants**: `MAX_FIFO_SIZE`, `VRAM_WIDTH`
- **Enums**: `GPU_PRIMITIVE_POLYGON`, `GPU_DMA_OFF`

### Documentation
- Every public function: Doxygen comment
- Complex logic: Inline comments explaining "why"
- Bug fixes: Reference this document + original issue

### Error Handling
- LOG_ERROR for unhandled commands
- LOG_WARN for packet boundary forcing
- LOG_INFO for state transitions
- LOG_DEBUG for per-command details (gated by `LOG_GPU_DEBUG`)

### Testing
- Compile with `-Wall -Wextra -Werror`
- No static analyzer warnings (`clang-tidy`)
- Zero memory leaks (`valgrind --leak-check=full`)

---

## Appendix A: Complete Function Migration Map

### From `src/gpu.c` to New Modules

| Function Name | Source Lines | Target Module | Target File |
|---------------|--------------|---------------|-------------|
| `clear_gp0_command_buffer()` | 89-92 | Commands | `gpu_commands.c` |
| `push_gp0_command_word()` | 99-107 | Commands | `gpu_commands.c` |
| `gp1_reset()` | 119-124 | Commands | `gpu_commands.c` |
| `gp1_reset_command_buffer()` | 127-136 | Commands | `gpu_commands.c` |
| `gp1_acknowledge_irq()` | 139-143 | Commands | `gpu_commands.c` |
| `gp1_display_enable()` | 146-150 | Timing | `gpu_timing.c` |
| `gp1_dma_direction()` | 153-160 | Core | `gpu_core.c` |
| `gp1_display_vram_start()` | 163-169 | Timing | `gpu_timing.c` |
| `gp1_display_horizontal_range()` | 172-178 | Timing | `gpu_timing.c` |
| `gp1_display_vertical_range()` | 181-187 | Timing | `gpu_timing.c` |
| `gp1_display_mode()` | 190-226 | Timing | `gpu_timing.c` |
| `gp0_nop()` | 253-255 | Commands | `gpu_commands.c` |
| `gp0_clear_cache()` | 258-261 | Commands | `gpu_commands.c` |
| `gp0_fill_rectangle()` | 264-301 | Rendering | `gpu_rendering.c` |
| `draw_rectangle()` ⭐ | 303-512 | Rendering | `gpu_rendering.c` |
| `gp0_rect_variable_opaque()` | 514-534 | Rendering | `gpu_rendering.c` |
| `gp0_rect_variable_semi_trans()` | 536-556 | Rendering | `gpu_rendering.c` |
| `gp0_rect_tex_variable_opaque()` | 558-591 | Rendering | `gpu_rendering.c` |
| `gp0_rect_1x1_opaque()` | 593-610 | Rendering | `gpu_rendering.c` |
| `gp0_rect_8x8_opaque()` | 612-629 | Rendering | `gpu_rendering.c` |
| `gp0_rect_16x16_opaque()` | 631-648 | Rendering | `gpu_rendering.c` |
| `gp0_rect_tex_1x1_opaque()` | 650-665 | Rendering | `gpu_rendering.c` |
| `gp0_rect_tex_8x8_opaque()` | 667-696 | Rendering | `gpu_rendering.c` |
| `gp0_rect_tex_16x16_opaque()` ⭐ | 698-734 | Rendering | `gpu_rendering.c` |
| `gp0_quad_mono_opaque()` | 736-762 | Rendering | `gpu_rendering.c` |
| `gp0_quad_texture_blend_opaque()` | 764-787 | Rendering | `gpu_rendering.c` |
| `gp0_quad_shaded_opaque()` | 789-819 | Rendering | `gpu_rendering.c` |
| `gp0_triangle_shaded_opaque()` | 821-856 | Rendering | `gpu_rendering.c` |
| `gp0_image_load()` | 858-899 | VRAM | `gpu_vram.c` |
| `vram_write_masked()` | 901-916 | VRAM | `gpu_vram.c` |
| `gpu_gp0_handle_word()` ⭐ | 918-1018 | Commands | `gpu_commands.c` |
| `gpu_gp0()` | 1020-1065 | Core | `gpu_core.c` |
| `gp0_draw_mode()` | (scattered) | Commands | `gpu_commands.c` |
| `gp0_texture_window()` | (scattered) | Commands | `gpu_commands.c` |
| `gp0_drawing_area_top_left()` | (scattered) | Commands | `gpu_commands.c` |
| `gp0_drawing_area_bottom_right()` | (scattered) | Commands | `gpu_commands.c` |
| `gp0_drawing_offset()` | (scattered) | Commands | `gpu_commands.c` |
| `gp0_mask_bit_setting()` | (scattered) | Commands | `gpu_commands.c` |

⭐ = Contains critical bug fixes

---

## Appendix B: Critical Code Sections

### Section 1: UV Coordinate Fix (Rendering Module)

**Before** (WRONG):
```c
// In draw_rectangle() around line 470
t[1].u = tex->u + w;      // ❌ Wrong: Exceeds bounds
t[1].v = tex->v + h;      // ❌ Wrong: Exceeds bounds
```

**After** (CORRECT):
```c
// ✅ Correct: Inclusive coordinates
t[1].u = tex->u + (w - 1);
t[1].v = tex->v + (h - 1);

// Example: 16x16 rectangle starting at UV(0,0)
// u0=0, v0=0 (top-left corner)
// u1=0+(16-1)=15, v1=0+(16-1)=15 (bottom-right corner)
// Result: Samples texels [0..15] in both dimensions ✓
```

**Unit Test**:
```c
void test_uv_coordinates() {
    // Test 16x16 rectangle
    RendererTexCoord tex = {.u = 0, .v = 0};
    RendererTexCoord result = calculate_uv_bounds(&tex, 16, 16);
    assert(result.u == 15 && result.v == 15);
    
    // Test 8x8 rectangle
    tex.u = 10; tex.v = 5;
    result = calculate_uv_bounds(&tex, 8, 8);
    assert(result.u == 17 && result.v == 12);
}
```

---

### Section 2: Handler Mapping Fix (Commands Module)

**Handler Table Generation**:
```c
void build_gp0_command_table(GP0Handler table[256]) {
    // ...
    
    // Rectangle commands (0x60-0x7F)
    // Pattern: Bits [7:5] = 011 (rectangle primitive)
    //          Bit 6 = size bit 1
    //          Bit 5 = size bit 0
    //          Bit 2 = texture enable (CRITICAL!)
    //          Bit 1 = semi-transparency
    //          Bit 0 = raw texture
    
    for (int i = 0x60; i <= 0x7F; i++) {
        bool is_textured = (i & 0x04) != 0;  // Bit 2
        uint8_t size_code = (i >> 3) & 0x3;  // Bits [4:3]
        
        if (size_code == 0) { // Variable size
            if (is_textured) {
                table[i] = &gp0_rect_tex_variable_handler; // 4 words
            } else {
                table[i] = &gp0_rect_variable_handler;     // 3 words
            }
        } else if (size_code == 1) { // 1x1
            if (is_textured) {
                table[i] = &gp0_rect_tex_1x1_handler;      // 3 words
            } else {
                table[i] = &gp0_rect_1x1_handler;          // 2 words
            }
        } else if (size_code == 2) { // 8x8
            if (is_textured) {
                table[i] = &gp0_rect_tex_8x8_handler;      // 3 words
            } else {
                table[i] = &gp0_rect_8x8_handler;          // 2 words
            }
        } else if (size_code == 3) { // 16x16
            if (is_textured) {
                table[i] = &gp0_rect_tex_16x16_handler;    // 3 words ✅
            } else {
                table[i] = &gp0_rect_16x16_handler;        // 2 words ✅
            }
        }
    }
    
    // Explicit verification for critical commands
    assert(table[0x78] == &gp0_rect_tex_16x16_handler);    // ✅ Textured
    assert(table[0x7C] == &gp0_rect_16x16_handler);        // ✅ Non-textured
}
```

**Unit Test**:
```c
void test_command_handlers() {
    GP0Handler table[256];
    build_gp0_command_table(table);
    
    // Test 0x78 (01111000 = rect + 16x16 + textured + opaque)
    assert(table[0x78] == &gp0_rect_tex_16x16_handler);
    assert(get_expected_words(0x78) == 3);
    
    // Test 0x7C (01111100 = rect + 16x16 + non-textured + opaque)
    assert(table[0x7C] == &gp0_rect_16x16_handler);
    assert(get_expected_words(0x7C) == 2);
    
    // Test 0x7A (01111010 = rect + 16x16 + textured + semi-trans)
    assert(table[0x7A] == &gp0_rect_tex_16x16_handler);
    assert(get_expected_words(0x7A) == 3);
}
```

---

### Section 3: DMA Packet Boundary Detection (Commands Module)

**Full Logic**:
```c
bool gp0_dispatch_command(GPU* gpu, uint32_t command) {
    // --- PACKET BOUNDARY DETECTION (CRITICAL) ---
    //
    // Problem: DMA linked-list mode sends commands in packets.
    // Packets can end mid-command, causing next packet's first word
    // to be misinterpreted as data for previous command.
    //
    // Example:
    //   Packet 1: [GP0(0x2C), word1, word2] (expects 9 words, only 3 sent)
    //   Packet 2: [GP0(0x78), ...] (new command, but looks like word4 of 0x2C)
    //
    // Solution: Detect when incoming word looks like a NEW command
    // despite words_remaining > 0, indicating packet boundary.
    
    uint8_t opcode = (uint8_t)(command >> 24);
    uint8_t cmd_type = (opcode >> 5) & 0x7; // Top 3 bits of opcode
    
    // Command type classification:
    // 000 = reserved/special
    // 001 = polygon
    // 010 = line
    // 011 = rectangle
    // 100-110 = reserved
    // 111 = environment/control
    
    bool is_render_cmd = (cmd_type >= 1 && cmd_type <= 3);
    bool is_env_cmd = (cmd_type == 7 && opcode >= 0xE0);
    
    // Force new command if:
    // 1. We're waiting for more words (words_remaining > 0)
    // 2. This word looks like a command opcode
    // 3. We're past the first couple words (words_remaining > 2)
    //    (Allows legitimate data that happens to look like opcodes)
    bool looks_like_new_cmd = (gpu->gp0_words_remaining > 2) && 
                              (is_render_cmd || is_env_cmd);
    
    if (gpu->gp0_words_remaining == 0 || looks_like_new_cmd) {
        if (looks_like_new_cmd) {
            LOG_GPU_WARN(
                "GPU: Forcing new command 0x%02X while %d words remaining "
                "for 0x%02X (DMA packet boundary detected)",
                opcode, gpu->gp0_words_remaining, gpu->gp0_current_opcode
            );
        }
        
        // Reset state for new command
        gpu->gp0_current_opcode = opcode;
        clear_gp0_command_buffer(gpu);
        gpu->gp0_words_remaining = 0;
        
        // Get handler and expected word count
        GP0Handler handler = s_gp0_handler_table[opcode];
        uint32_t expected_words = s_gp0_expected_words[opcode];
        
        // Push first word and update state
        push_gp0_command_word(gpu, command);
        gpu->gp0_words_remaining = expected_words - 1;
        
        // If command complete (single-word), execute immediately
        if (gpu->gp0_words_remaining == 0) {
            (*handler)(gpu);
            return true; // Command complete
        }
        
        return false; // Need more words
    }
    
    // --- NORMAL DATA WORD PROCESSING ---
    // This is a data word for the current command
    push_gp0_command_word(gpu, command);
    gpu->gp0_words_remaining--;
    
    if (gpu->gp0_words_remaining == 0) {
        // Command complete, execute handler
        GP0Handler handler = s_gp0_handler_table[gpu->gp0_current_opcode];
        (*handler)(gpu);
        return true;
    }
    
    return false; // Still need more words
}
```

**Unit Test** (Difficult - requires DMA simulation):
```c
void test_packet_boundary_detection() {
    GPU gpu;
    gpu_init(&gpu);
    
    // Simulate packet 1: Incomplete GP0(0x2C) textured quad
    gp0_dispatch_command(&gpu, 0x2C000000); // Cmd (expects 9 words)
    gp0_dispatch_command(&gpu, 0x12345678); // Word 2
    gp0_dispatch_command(&gpu, 0x9ABCDEF0); // Word 3
    // Packet ends here, words_remaining = 6
    
    // Simulate packet 2: GP0(0x78) 16x16 textured rect
    bool forced = gp0_dispatch_command(&gpu, 0x78140000);
    
    // Verify forced new command
    assert(gpu.gp0_current_opcode == 0x78);
    assert(gpu.gp0_words_remaining == 2); // Expects 3 total
    assert(forced == false); // Incomplete command
    
    // Complete the 0x78 command
    gp0_dispatch_command(&gpu, 0x0167016C); // Position
    bool complete = gp0_dispatch_command(&gpu, 0x000F0017); // UV+CLUT
    
    assert(complete == true);
    assert(gpu.gp0_current_opcode == 0x78);
}
```

---

## Appendix C: Makefile Changes

### Current Structure
```makefile
GPU_OBJS = src/gpu.o

$(GPU_OBJS): src/gpu.c include/gpu.h
	$(CC) $(CFLAGS) -c $< -o $@
```

### Target Structure
```makefile
GPU_CORE_OBJS = src/gpu/gpu_core.o
GPU_COMMANDS_OBJS = src/gpu/gpu_commands.o
GPU_RENDERING_OBJS = src/gpu/gpu_rendering.o
GPU_VRAM_OBJS = src/gpu/gpu_vram.o
GPU_TIMING_OBJS = src/gpu/gpu_timing.o

GPU_OBJS = $(GPU_CORE_OBJS) $(GPU_COMMANDS_OBJS) $(GPU_RENDERING_OBJS) $(GPU_VRAM_OBJS) $(GPU_TIMING_OBJS)

# Core module
$(GPU_CORE_OBJS): src/gpu/gpu_core.c include/gpu/gpu_core.h include/gpu/gpu_types.h
	$(CC) $(CFLAGS) -c $< -o $@

# Commands module
$(GPU_COMMANDS_OBJS): src/gpu/gpu_commands.c include/gpu/gpu_commands.h include/gpu/gpu_types.h include/gpu/gpu_core.h
	$(CC) $(CFLAGS) -c $< -o $@

# Rendering module
$(GPU_RENDERING_OBJS): src/gpu/gpu_rendering.c include/gpu/gpu_rendering.h include/gpu/gpu_types.h include/gpu/gpu_core.h
	$(CC) $(CFLAGS) -c $< -o $@

# VRAM module
$(GPU_VRAM_OBJS): src/gpu/gpu_vram.c include/gpu/gpu_vram.h include/gpu/gpu_types.h include/gpu/gpu_core.h
	$(CC) $(CFLAGS) -c $< -o $@

# Timing module
$(GPU_TIMING_OBJS): src/gpu/gpu_timing.c include/gpu/gpu_timing.h include/gpu/gpu_types.h include/gpu/gpu_core.h
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target
clean:
	rm -f $(GPU_OBJS)
	rm -f src/gpu/*.o
```

---

## Summary

This refactoring will transform the monolithic 1239-line `gpu.c` into 5 focused modules (~2000 lines total with documentation), following DuckStation's proven architecture. **All three critical bug fixes will be preserved** through careful migration and extensive testing.

**Total Effort Estimate**: 3-4 weeks  
**Risk Level**: Medium (mitigated by phased approach)  
**Benefit**: Maintainable, testable, DuckStation-comparable GPU code

**Next Steps**:
1. ✅ Review this plan
2. ⏳ Start Phase 1: Commands Module
3. ⏳ Test after Phase 1
4. ⏳ Continue Phases 2-5

**Critical Success Factors**:
- Test after EVERY phase
- Verify all three bug fixes at each step
- Keep old `gpu.c` until all modules proven
- No "big bang" integration - incremental only
