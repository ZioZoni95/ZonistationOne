# ZonistationOne GPU File Structure
## Matching DuckStation Architecture (Adapted for C)

**Date**: January 8, 2026  
**Reference**: DuckStation has 26 GPU files, we'll create 12 for C (no C++, no SW renderer, no threading)

---

## DuckStation Structure Analysis (26 Files)

### Core Layer (3 files)
- `gpu.cpp/h` (2363 lines) - Main state machine, FIFO, status register
- `gpu_types.h` (556 lines) - Enums, structs, constants

### Command Layer (4 files)
- `gpu_commands.cpp` (1129 lines) - GP0/GP1 parsing
- `gpu_thread.cpp/h` (1516 lines) - Thread synchronization
- `gpu_thread_commands.h` (354 lines) - Command queue

### Backend Abstraction (2 files)
- `gpu_backend.cpp/h` (968 lines) - Abstract renderer interface

### Hardware Renderer (6 files)
- `gpu_hw.cpp/h` (4351 lines) - OpenGL/Vulkan/D3D renderer
- `gpu_hw_shadergen.cpp/h` (2432 lines) - Dynamic shader generation
- `gpu_hw_texture_cache.cpp/h` (3870 lines) - Texture management

### Software Renderer (5 files)
- `gpu_sw.cpp/h` (446 lines) - Software rasterizer entry
- `gpu_sw_rasterizer.cpp/h` (129 lines) - Pure CPU rasterization
- `gpu_sw_rasterizer_avx2.cpp` (12 lines) - SIMD optimization

### Utilities (6 files)
- `gpu_shadergen.cpp/h` (324 lines) - Base shader utilities
- `gpu_presenter.cpp/h` (1727 lines) - Display output
- `gpu_dump.cpp/h` (544 lines) - Debug recording/playback

**Total**: 26 files, ~20,000 lines

---

## ZonistationOne Target Structure (12 Core Files)

### Category 1: GPU Core (3 files)

**`include/gpu/gpu_types.h`** ✅ EXISTS (248 lines)
- Enums: GPUTextureMode, GPUDMADirection, GPUPrimitive, etc.
- Structs: GPUSTAT, GPUDrawingArea, GPUDrawingOffset
- Constants: VRAM_WIDTH, VRAM_HEIGHT, MAX_PRIMITIVE_WIDTH

**`include/gpu/gpu_core.h`** + **`src/gpu/gpu_core.c`** ✅ EXISTS (partial)
- GPU state structure
- Initialization: `gpu_init_full()`, `gpu_soft_reset()`
- Main entry: `gpu_gp0()`, `gpu_gp1()`
- GPUSTAT register management
- DMA handling

**Current**: 200 lines (init only)  
**Target**: ~500 lines (full core state machine)  
**Matches**: DuckStation's `gpu.cpp/h` (core only, no threading)

---

### Category 2: Command Parsing (1 file)

**`include/gpu/gpu_commands.h`** + **`src/gpu/gpu_commands.c`** ⏳ CREATE
- GP0 command dispatch table (256 entries)
- GP0 handlers: Rendering, VRAM, environment commands
- GP1 handlers: Display configuration, reset, DMA
- FIFO management
- **DMA packet boundary detection** ⭐ (critical bug fix)
- Command buffering

**Target**: ~800 lines  
**Matches**: DuckStation's `gpu_commands.cpp`

**Key Functions**:
```c
// Command dispatch
void gp0_dispatch_command(GPU* gpu, uint32_t command);
void gp1_dispatch_command(GPU* gpu, uint8_t cmd, uint32_t value);

// Handler table
typedef void (*GP0Handler)(GPU* gpu);
void build_gp0_handler_table(GP0Handler table[256]);

// FIFO
void gp0_fifo_push(GPU* gpu, uint32_t word);
uint32_t gp0_fifo_pop(GPU* gpu);
```

---

### Category 3: GPU Rendering (2 files)

**`include/gpu/gpu_rendering.h`** + **`src/gpu/gpu_rendering.c`** ⏳ CREATE
- Parse rendering commands (polygons, lines, rectangles)
- Generate vertex data
- **UV coordinate calculation** ⭐ (bug fix: w-1, h-1)
- Apply drawing offset, colors, textures
- Call renderer backend

**Target**: ~700 lines  
**Matches**: DuckStation's `gpu_hw.cpp` (drawing logic only, no backend)

**Key Functions**:
```c
// Rendering dispatch
void gpu_render_polygon(GPU* gpu, uint8_t opcode);
void gpu_render_line(GPU* gpu, uint8_t opcode);
void gpu_render_rectangle(GPU* gpu, uint8_t opcode);

// Internal primitives
void draw_triangle_internal(GPU* gpu, const GPUVertex v[3], bool textured, bool shaded);
void draw_quad_internal(GPU* gpu, const GPUVertex v[4], bool textured, bool shaded);
void draw_rectangle_internal(GPU* gpu, int16_t x, int16_t y, uint16_t w, uint16_t h,
                             const RendererColor* color, bool textured,
                             const RendererTexCoord* tex, uint16_t clut, uint16_t tpage);
```

---

### Category 4: GPU VRAM Operations (1 file)

**`include/gpu/gpu_vram.h`** + **`src/gpu/gpu_vram.c`** ⏳ CREATE
- GP0(0xA0): CPU→VRAM image load
- GP0(0xC0): VRAM→CPU image store
- GP0(0x80): VRAM→VRAM copy
- Mask bit handling
- Direct VRAM access

**Target**: ~400 lines  
**Matches**: Part of DuckStation's `gpu_hw.cpp` (VRAM transfer logic)

**Key Functions**:
```c
// Image transfers
void vram_load_start(GPU* gpu, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void vram_load_word(GPU* gpu, uint32_t data); // 2 pixels per word
void vram_store_start(GPU* gpu, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
uint32_t vram_store_read(GPU* gpu);

// Copy
void vram_copy_rect(GPU* gpu, uint16_t sx, uint16_t sy, uint16_t dx, uint16_t dy, uint16_t w, uint16_t h);

// Direct access
uint16_t vram_read16(const GPU* gpu, uint32_t offset);
void vram_write16(GPU* gpu, uint32_t offset, uint16_t value);
void vram_write_masked(GPU* gpu, uint32_t offset, uint16_t value);
```

---

### Category 5: GPU Display/Timing (1 file)

**`include/gpu/gpu_display.h`** + **`src/gpu/gpu_display.c`** ⏳ CREATE
- GP1(0x05-0x08): Display configuration
- CRTC scanout settings
- Horizontal/vertical sync ranges
- Video mode (NTSC/PAL)
- Interlaced mode
- Display area mapping

**Target**: ~300 lines  
**Matches**: DuckStation's `gpu_presenter.cpp` (configuration only, no actual presentation)

**Key Functions**:
```c
// Display configuration
void display_set_vram_start(GPU* gpu, uint16_t x, uint16_t y);
void display_set_horizontal_range(GPU* gpu, uint16_t start, uint16_t end);
void display_set_vertical_range(GPU* gpu, uint16_t start, uint16_t end);
void display_set_mode(GPU* gpu, uint32_t mode_bits);
void display_update_mapping(GPU* gpu);

// Video timing
uint32_t display_get_scanline_ticks(const GPU* gpu);
uint32_t display_get_total_scanlines(const GPU* gpu);
bool display_is_interlaced(const GPU* gpu);
```

---

### Category 6: Renderer Backend (4 files)

**`include/renderer/renderer_core.h`** + **`src/renderer/renderer_core.c`** ⏳ SPLIT from renderer.c
- Abstract renderer interface
- Initialization and state management
- Vertex buffer management
- Flush/draw operations

**Target**: ~300 lines  
**Matches**: DuckStation's `gpu_backend.cpp/h`

**Key Functions**:
```c
// Initialization
bool renderer_init(Renderer* renderer);
void renderer_shutdown(Renderer* renderer);
void renderer_reset(Renderer* renderer);

// Drawing interface
void renderer_push_quad(Renderer* renderer, RendererPosition pos[4], RendererColor col[4],
                       RendererTexCoord tex[4], uint16_t clut, uint16_t tpage);
void renderer_flush(Renderer* renderer);

// State
void renderer_set_drawing_offset(Renderer* renderer, int16_t x, int16_t y);
void renderer_set_drawing_area(Renderer* renderer, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
```

---

**`include/renderer/renderer_gl.h`** + **`src/renderer/renderer_gl.c`** ⏳ SPLIT from renderer.c
- OpenGL-specific implementation
- VAO/VBO management
- Shader program setup
- Texture upload
- Draw calls

**Target**: ~300 lines  
**Matches**: DuckStation's `gpu_hw.cpp` (OpenGL backend code)

**Key Functions**:
```c
// OpenGL setup
bool renderer_gl_init(Renderer* renderer);
void renderer_gl_shutdown(Renderer* renderer);

// OpenGL state
void renderer_gl_bind_vao(Renderer* renderer);
void renderer_gl_bind_shader(Renderer* renderer);
void renderer_gl_set_uniforms(Renderer* renderer);

// Drawing
void renderer_gl_draw_batch(Renderer* renderer);
```

---

**`include/renderer/renderer_shaders.h`** + **`src/renderer/renderer_shaders.c`** ⏳ SPLIT from renderer.c
- Shader source code (vertex + fragment)
- Shader compilation
- Program linking
- Uniform location caching

**Target**: ~250 lines  
**Matches**: DuckStation's `gpu_hw_shadergen.cpp` (but hardcoded, not generated)

**Key Functions**:
```c
// Shader compilation
GLuint shader_compile(const char* source, GLenum type);
GLuint shader_link_program(GLuint vertex, GLuint fragment);

// Shader sources (from current renderer.c lines 38-170)
extern const char* vertex_shader_source;
extern const char* fragment_shader_source;

// Uniform management
void shader_cache_uniform_locations(Renderer* renderer);
```

---

**`include/renderer/renderer_texture.h`** + **`src/renderer/renderer_texture.c`** ⏳ SPLIT from renderer.c
- VRAM texture creation (1024x512, R16UI)
- Texture upload from CPU VRAM
- Texture window management
- CLUT handling (implicit, in shader)

**Target**: ~200 lines  
**Matches**: DuckStation's `gpu_hw_texture_cache.cpp` (simplified, no caching)

**Key Functions**:
```c
// Texture management
bool renderer_texture_init(Renderer* renderer);
void renderer_texture_shutdown(Renderer* renderer);

// VRAM upload
void renderer_upload_vram(Renderer* renderer, const uint16_t* vram_data);
void renderer_update_vram_region(Renderer* renderer, uint16_t x, uint16_t y,
                                 uint16_t w, uint16_t h, const uint16_t* data);

// Texture window
void renderer_set_texture_window(Renderer* renderer, uint32_t window_bits);
```

---

### Category 7: Debug Utilities (1 file - OPTIONAL)

**`include/gpu/gpu_debug.h`** + **`src/gpu/gpu_debug.c`** ⏳ CREATE (optional)
- VRAM region dumping
- Command logging
- Statistics tracking
- Error reporting

**Target**: ~200 lines  
**Matches**: DuckStation's `gpu_dump.cpp/h`

**Key Functions**:
```c
// Debug dumping
void gpu_debug_dump_vram_region(const GPU* gpu, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* path);
void gpu_debug_log_command(const GPU* gpu, uint32_t command);
void gpu_debug_print_stats(const GPU* gpu);
```

---

## Complete File Mapping

### Current Structure
```
ZonistationOne/
├── include/
│   ├── gpu.h (legacy wrapper)
│   └── gpu/
│       ├── gpu_types.h ✅ (248 lines)
│       ├── gpu_core.h ✅ (partial)
│       └── gpu_commands.h ⏳ (stub)
├── src/
│   ├── gpu.c 🔴 (1239 lines - MONOLITHIC, to be removed)
│   ├── gpu/
│   │   └── gpu_core.c ✅ (partial init only)
│   └── renderer.c 🔴 (863 lines - MONOLITHIC, to be split)
```

### Target Structure (12 Core Files)
```
ZonistationOne/
├── include/
│   ├── gpu.h ✅ (legacy wrapper, keep)
│   ├── gpu/
│   │   ├── gpu_types.h ✅ (248 lines) - Keep as-is
│   │   ├── gpu_core.h 🔄 (expand to ~150 lines)
│   │   ├── gpu_commands.h ⏳ (create ~100 lines)
│   │   ├── gpu_rendering.h ⏳ (create ~80 lines)
│   │   ├── gpu_vram.h ⏳ (create ~60 lines)
│   │   ├── gpu_display.h ⏳ (create ~50 lines)
│   │   └── gpu_debug.h ⏳ (optional ~30 lines)
│   └── renderer/
│       ├── renderer_core.h ⏳ (create ~80 lines)
│       ├── renderer_gl.h ⏳ (create ~50 lines)
│       ├── renderer_shaders.h ⏳ (create ~40 lines)
│       └── renderer_texture.h ⏳ (create ~40 lines)
│
├── src/
│   ├── gpu/
│   │   ├── gpu_core.c 🔄 (expand from 200 → ~500 lines)
│   │   ├── gpu_commands.c ⏳ (create ~800 lines)
│   │   ├── gpu_rendering.c ⏳ (create ~700 lines)
│   │   ├── gpu_vram.c ⏳ (create ~400 lines)
│   │   ├── gpu_display.c ⏳ (create ~300 lines)
│   │   └── gpu_debug.c ⏳ (optional ~200 lines)
│   └── renderer/
│       ├── renderer_core.c ⏳ (split from renderer.c ~300 lines)
│       ├── renderer_gl.c ⏳ (split from renderer.c ~300 lines)
│       ├── renderer_shaders.c ⏳ (split from renderer.c ~250 lines)
│       └── renderer_texture.c ⏳ (split from renderer.c ~200 lines)
```

**Files to Remove**:
- `src/gpu.c` (1239 lines) → Split into gpu_commands.c, gpu_rendering.c, gpu_vram.c, gpu_display.c
- `src/renderer.c` (863 lines) → Split into renderer_core.c, renderer_gl.c, renderer_shaders.c, renderer_texture.c

---

## Line Count Comparison

### DuckStation (C++, full production)
- Core: ~3,522 lines (gpu.cpp/h, gpu_types.h)
- Commands: ~2,353 lines (gpu_commands.cpp, gpu_thread.cpp/h, gpu_thread_commands.h)
- Hardware Renderer: ~11,067 lines (gpu_hw.cpp/h, gpu_hw_shadergen.cpp/h, gpu_hw_texture_cache.cpp/h)
- Software Renderer: ~687 lines (gpu_sw.cpp/h, gpu_sw_rasterizer.cpp/h, avx2)
- Utilities: ~2,933 lines (gpu_backend.cpp/h, gpu_shadergen.cpp/h, gpu_presenter.cpp/h, gpu_dump.cpp/h)
- **Total**: ~20,562 lines (26 files)

### ZonistationOne (C, simplified)
- Core: ~748 lines (gpu_core.c/h, gpu_types.h)
- Commands: ~900 lines (gpu_commands.c/h)
- Rendering: ~780 lines (gpu_rendering.c/h)
- VRAM: ~460 lines (gpu_vram.c/h)
- Display: ~350 lines (gpu_display.c/h)
- Renderer Backend: ~1,050 lines (renderer_core/gl/shaders/texture .c/h)
- Debug (optional): ~230 lines (gpu_debug.c/h)
- **Total**: ~4,518 lines (12 files)

**Ratio**: DuckStation is 4.5x larger (includes threading, multi-backend, software renderer, SIMD, complex texture caching)

---

## Migration Priority Order

### Phase 1: GPU Command Separation ⭐ HIGHEST PRIORITY
1. Create `gpu_commands.c/h`
2. Move command dispatch from `gpu.c`
3. Build handler table with **0x78 fix** ⭐
4. Implement **DMA packet boundary detection** ⭐
5. Test: Logo + menu background

### Phase 2: GPU Rendering Separation
1. Create `gpu_rendering.c/h`
2. Move all drawing functions from `gpu.c`
3. Preserve **UV coordinate fix (w-1, h-1)** ⭐
4. Move `gp0_rect_tex_16x16_opaque()` ⭐
5. Test: Text rendering (0x78 commands)

### Phase 3: GPU VRAM Separation
1. Create `gpu_vram.c/h`
2. Move image load/store/copy from `gpu.c`
3. Test: VRAM uploads, copy operations

### Phase 4: GPU Display Separation
1. Create `gpu_display.c/h`
2. Move GP1 display commands from `gpu.c`
3. Test: Display configuration changes

### Phase 5: Expand GPU Core
1. Update `gpu_core.c/h`
2. Add remaining state management from `gpu.c`
3. Remove old `gpu.c` file
4. Test: Full emulator functionality

### Phase 6: Renderer Backend Separation (Lower Priority)
1. Create `renderer/` subdirectories
2. Split `renderer.c` → 4 files
3. Test: All rendering still works

### Phase 7: Debug Utilities (Optional)
1. Create `gpu_debug.c/h`
2. Add VRAM dump tools
3. Add command logging

---

## Critical Bug Fixes to Preserve

All migration must preserve these fixes:

### 1. UV Coordinate Fix (gpu_rendering.c)
```c
// ✅ CORRECT
t[1].u = tex->u + (w - 1);
t[1].v = tex->v + (h - 1);
```

### 2. Handler Mapping Fix (gpu_commands.c)
```c
// ✅ CORRECT
table[0x78] = &gp0_rect_tex_16x16_handler; // 3 words, textured
table[0x7C] = &gp0_rect_16x16_handler;     // 2 words, non-textured
```

### 3. DMA Packet Boundary Detection (gpu_commands.c)
```c
// ✅ CORRECT
uint8_t cmd_type = (opcode >> 5) & 0x7;
bool is_render_cmd = (cmd_type >= 1 && cmd_type <= 3);
bool looks_like_new_cmd = (gp0_words_remaining > 2) && is_render_cmd;
```

---

## Makefile Structure

### Current
```makefile
GPU_OBJS = src/gpu.o
RENDERER_OBJS = src/renderer.o
```

### Target
```makefile
# GPU Core Objects
GPU_CORE_OBJS = src/gpu/gpu_core.o
GPU_COMMANDS_OBJS = src/gpu/gpu_commands.o
GPU_RENDERING_OBJS = src/gpu/gpu_rendering.o
GPU_VRAM_OBJS = src/gpu/gpu_vram.o
GPU_DISPLAY_OBJS = src/gpu/gpu_display.o
GPU_DEBUG_OBJS = src/gpu/gpu_debug.o

GPU_OBJS = $(GPU_CORE_OBJS) $(GPU_COMMANDS_OBJS) $(GPU_RENDERING_OBJS) \
           $(GPU_VRAM_OBJS) $(GPU_DISPLAY_OBJS) $(GPU_DEBUG_OBJS)

# Renderer Objects
RENDERER_CORE_OBJS = src/renderer/renderer_core.o
RENDERER_GL_OBJS = src/renderer/renderer_gl.o
RENDERER_SHADERS_OBJS = src/renderer/renderer_shaders.o
RENDERER_TEXTURE_OBJS = src/renderer/renderer_texture.o

RENDERER_OBJS = $(RENDERER_CORE_OBJS) $(RENDERER_GL_OBJS) \
                $(RENDERER_SHADERS_OBJS) $(RENDERER_TEXTURE_OBJS)

# Build rules
$(GPU_COMMANDS_OBJS): src/gpu/gpu_commands.c include/gpu/gpu_commands.h include/gpu/gpu_types.h
	$(CC) $(CFLAGS) -c $< -o $@

# ... (similar for each module)
```

---

## Summary

**DuckStation**: 26 files, 20,562 lines (C++, multi-backend, threading, SW renderer)  
**ZonistationOne Target**: 12 files, 4,518 lines (C, OpenGL only, single-threaded, simplified)

**Key Differences**:
- ❌ No C++ templates/classes (using C structs + function pointers)
- ❌ No threading (gpu_thread.cpp/h not needed)
- ❌ No software renderer (gpu_sw.cpp/h not needed)
- ❌ No multi-backend (gpu_backend.cpp/h simplified)
- ❌ No dynamic shader generation (hardcoded shaders in renderer_shaders.c)
- ✅ Same separation of concerns (commands, rendering, VRAM, display)
- ✅ Similar abstraction layers (GPU core, renderer backend)

**Next Steps**: Start Phase 1 - Create `gpu_commands.c/h`
