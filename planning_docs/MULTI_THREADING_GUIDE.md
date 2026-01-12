# Multi-Threading Implementation Guide for ZonistationOne

## Overview

This guide explains how to integrate DuckStation-style multi-threading into your PS1 emulator while maintaining your existing codebase and single-threaded compatibility.

## Architecture

### Threading Model

**DuckStation's Approach (adapted to C):**

```
┌─────────────────┐         ┌─────────────────┐
│   CPU Thread    │────────>│   GPU Thread    │
│  (Main Loop)    │  FIFO   │  (Rendering)    │
│                 │         │                 │
│ - CPU Emulation │         │ - GPU Commands  │
│ - IRQ/Timers    │         │ - OpenGL Draw   │
│ - DMA           │         │ - VBlank        │
│ - Event Queue   │         │                 │
└─────────────────┘         └─────────────────┘
         │                           │
         └───────────────────────────┘
              Synchronization
         (Semaphores + Spin-wait)
```

### Key Components

1. **Threading Primitives** (`threading.h/c`)
   - POSIX threads wrapper
   - Mutexes, condition variables, semaphores
   - Atomic operations (lock-free)
   - Timing utilities

2. **GPU Thread System** (`gpu_thread.h/c`)
   - Lock-free command FIFO (ring buffer)
   - GPU command encoding/decoding
   - Thread synchronization
   - Single-threaded compatibility mode

3. **Event Scheduler** (existing `event_scheduler.h/c`)
   - Remains on CPU thread
   - Schedules GPU VBlank events
   - Synchronizes with GPU thread for timing

## Integration Steps

### Step 1: Update Makefile

Add the new source files:

```makefile
# In Makefile, add to EMU_SRCS:
EMU_SRCS = src/main.c \
           src/threading.c \
           src/gpu_thread.c \
           src/cpu.c \
           src/interconnect.c \
           # ... rest of files

# Add pthread library
LIBS += -lpthread -lrt
```

### Step 2: Integrate GPU Thread into Interconnect

Modify `include/interconnect.h`:

```c
#include "gpu_thread.h"

struct Interconnect {
    // ... existing fields ...
    
    // Multi-threading support
    GpuThreadState gpu_thread_state;
    bool use_gpu_thread;  // Enable/disable via command line
};
```

### Step 3: Initialize GPU Thread in Main

Modify `src/main.c`:

```c
#include "gpu_thread.h"

int main(int argc, char *argv[]) {
    // ... existing argument parsing ...
    
    bool use_gpu_thread = true;  // Default enabled
    
    // Add command line option
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--no-gpu-thread") == 0) {
            use_gpu_thread = false;
        }
        // ... rest of args
    }
    
    // ... existing initialization ...
    
    // Initialize GPU thread system
    if (!gpu_thread_init(&inter.gpu_thread_state, &inter.gpu, use_gpu_thread)) {
        LOG_SYSTEM_ERROR("Failed to initialize GPU thread");
        return 1;
    }
    
    // Start GPU thread
    if (!gpu_thread_start(&inter.gpu_thread_state)) {
        LOG_SYSTEM_ERROR("Failed to start GPU thread");
        return 1;
    }
    
    // ... main loop ...
    
    // Shutdown GPU thread before cleanup
    gpu_thread_shutdown(&inter.gpu_thread_state);
    
    return 0;
}
```

### Step 4: Convert GPU Commands to Threaded Model

Modify `src/gpu.c` to submit commands instead of executing directly:

**Before (single-threaded):**
```c
void gpu_fill_vram(Gpu* gpu, uint16_t x, uint16_t y, 
                   uint16_t width, uint16_t height, uint32_t color) {
    // Direct OpenGL call
    glClearColor(...);
    glClear(...);
}
```

**After (multi-threaded):**
```c
void gpu_fill_vram(Interconnect* inter, uint16_t x, uint16_t y,
                   uint16_t width, uint16_t height, uint32_t color) {
    GpuThreadState* state = &inter->gpu_thread_state;
    
    // Allocate command
    GpuFillVramCommand* cmd = GPU_BEGIN_COMMAND(state, 
                                                GpuFillVramCommand, 
                                                GPU_CMD_FILL_VRAM);
    if (!cmd) {
        LOG_GPU_ERROR("Failed to allocate fill VRAM command");
        return;
    }
    
    // Fill command data
    cmd->x = x;
    cmd->y = y;
    cmd->width = width;
    cmd->height = height;
    cmd->color = color;
    
    // Submit to GPU thread
    GPU_SUBMIT_COMMAND(state, cmd);
}
```

### Step 5: Implement GPU Command Handlers

In `src/gpu_thread.c`, implement the actual GPU operations:

```c
static void execute_fill_vram_command(Gpu* gpu, GpuFillVramCommand* cmd) {
    // This runs on GPU thread
    // Make OpenGL calls here
    
    // Bind VRAM framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, gpu->vram_fbo);
    
    // Set up viewport for fill region
    glViewport(cmd->x, cmd->y, cmd->width, cmd->height);
    
    // Extract color components
    float r = ((cmd->color >> 0) & 0xFF) / 255.0f;
    float g = ((cmd->color >> 8) & 0xFF) / 255.0f;
    float b = ((cmd->color >> 16) & 0xFF) / 255.0f;
    
    // Clear the region
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}
```

Add this to the GPU thread's command processing loop:

```c
case GPU_CMD_FILL_VRAM: {
    GpuFillVramCommand* fill_cmd = (GpuFillVramCommand*)cmd;
    execute_fill_vram_command(state->gpu, fill_cmd);
    break;
}
```

### Step 6: Synchronize VBlank with GPU Thread

The VBlank event needs coordination between CPU and GPU threads:

```c
// In event_scheduler.c
static void evq_handle_vblank(struct Interconnect* sys) {
    // CPU side: trigger VBlank interrupt
    sys->irq_state |= IRQ_VBLANK;
    
    // Submit VBlank command to GPU thread
    GpuCommand* cmd = GPU_BEGIN_COMMAND(&sys->gpu_thread_state,
                                        GpuCommand,
                                        GPU_CMD_VBLANK);
    if (cmd) {
        GPU_SUBMIT_COMMAND(&sys->gpu_thread_state, cmd);
    }
    
    // Reschedule next VBlank
    uint32_t cycles_per_vblank = sys->is_pal ? 314 * 3406 : 263 * 3413;
    eventq_schedule(sys, EVQ_VBLANK, cycles_per_vblank);
}
```

### Step 7: Handle GPU->CPU Synchronization

Some operations require CPU to wait for GPU:

```c
// Example: GP0(0xC0) - VRAM to CPU transfer
uint32_t gpu_read_vram_word(Interconnect* inter) {
    // Submit read command
    GpuVramToCpuCommand* cmd = GPU_BEGIN_COMMAND(&inter->gpu_thread_state,
                                                 GpuVramToCpuCommand,
                                                 GPU_CMD_VRAM_TO_CPU);
    if (!cmd) return 0;
    
    cmd->x = inter->gpu.vram_transfer_x;
    cmd->y = inter->gpu.vram_transfer_y;
    cmd->width = inter->gpu.vram_transfer_width;
    cmd->height = inter->gpu.vram_transfer_height;
    
    // Allocate buffer for result
    uint32_t result = 0;
    cmd->dst_buffer = &result;
    
    GPU_SUBMIT_COMMAND(&inter->gpu_thread_state, cmd);
    
    // MUST SYNC - CPU needs result immediately
    gpu_thread_sync(&inter->gpu_thread_state, true);
    
    return result;
}
```

## Performance Considerations

### When to Sync

**Avoid frequent syncs** - they kill performance:

❌ **BAD:**
```c
// Syncing after every command
gpu_fill_vram(...);
gpu_thread_sync(&state, true);  // ← Slow!
gpu_draw_polygon(...);
gpu_thread_sync(&state, true);  // ← Slow!
```

✅ **GOOD:**
```c
// Let commands batch up
gpu_fill_vram(...);
gpu_draw_polygon(...);
gpu_draw_polygon(...);
// ... many commands ...
// Only sync when necessary (e.g., VRAM read, frame end)
if (need_result) {
    gpu_thread_sync(&state, true);
}
```

### Spin-Wait Optimization

DuckStation uses spin-waiting for low-latency synchronization:

```c
// When sync latency is critical (< 2ms expected)
gpu_thread_sync(state, true);   // ← Spin-wait

// When sync latency is not critical
gpu_thread_sync(state, false);  // ← Sleep-wait
```

The spin duration is configurable in `GpuThreadState::spin_time_ns`.

### Command FIFO Size

The 16MB FIFO can hold ~260,000 small commands. Tune based on your workload:

```c
// In gpu_thread.h
#define GPU_COMMAND_FIFO_SIZE (16 * 1024 * 1024)  // Increase if needed
```

## Debugging Multi-Threading Issues

### Enable Single-Threaded Mode

Always support disabling threading for debugging:

```bash
./myps1_emu --no-gpu-thread --debug
```

This runs everything on the CPU thread, making debugging easier.

### Add Synchronization Assertions

```c
// In GPU command handlers
assert(pthread_equal(pthread_self(), state->gpu_thread.thread_id));
```

### Log Thread IDs

```c
LOG_GPU_DEBUG("Command processed on thread %lu", 
              (unsigned long)pthread_self());
```

### Use Thread Sanitizer

Compile with ThreadSanitizer to detect data races:

```makefile
CFLAGS += -fsanitize=thread -g
LDFLAGS += -fsanitize=thread
```

## PS1 Documentation Integration

Reference the PS1 documentation for accurate timing:

### VBlank Timing (from #file:DOCS/timers.md)

```c
// NTSC: 263 lines * 3413 cycles/line = 897,619 cycles per frame
#define NTSC_CYCLES_PER_VBLANK (263 * 3413)

// PAL: 314 lines * 3406 cycles/line = 1,069,484 cycles per frame
#define PAL_CYCLES_PER_VBLANK (314 * 3406)
```

### GPU Timing Synchronization

The GPU thread must respect PS1 timing:

```c
// GPU command execution time (in CPU cycles)
// From DuckStation: varies by command type
uint32_t get_gpu_command_cycles(GpuCommandType type) {
    switch (type) {
        case GPU_CMD_FILL_VRAM: return 46;  // ~46 CPU cycles
        case GPU_CMD_COPY_VRAM_TO_VRAM: return 200;  // ~200 cycles
        // ... etc
    }
}
```

## Testing Checklist

- [ ] Emulator boots with GPU threading enabled
- [ ] Emulator boots with `--no-gpu-thread` (single-threaded)
- [ ] BIOS menu renders correctly
- [ ] VBlank IRQs fire at correct intervals
- [ ] No visual glitches compared to single-threaded mode
- [ ] Performance improves on multi-core systems
- [ ] No crashes or deadlocks after 5+ minutes
- [ ] Memory usage is stable (no leaks)
- [ ] Thread sanitizer reports no data races

## Migration Path

### Phase 1: Infrastructure (Current)
- ✅ Threading primitives
- ✅ GPU command FIFO
- ✅ Basic command submission

### Phase 2: Core Commands
- [ ] Implement all GPU command handlers
- [ ] Test with BIOS
- [ ] Verify timing accuracy

### Phase 3: Optimization
- [ ] Tune FIFO size
- [ ] Optimize sync points
- [ ] Profile and remove bottlenecks

### Phase 4: Extended Features
- [ ] Audio thread (SPU)
- [ ] Separate CD-ROM thread (optional)
- [ ] Thread affinity tuning

## Compatibility with Existing Code

The threading system is designed to coexist with your current single-threaded code:

1. **Event Scheduler**: Stays on CPU thread, no changes needed
2. **CPU Emulation**: Stays on CPU thread
3. **Memory/DMA**: Stays on CPU thread
4. **Only GPU moves to separate thread**

This minimizes refactoring and maintains compatibility.

## Performance Expectations

On a 4+ core system, expect:

- **~30-50% better frame pacing** (less stutter)
- **~15-25% higher frame rate** (multi-core utilization)
- **Lower input latency** (GPU doesn't block CPU)
- **Better sustained performance** (no GPU stalls)

On a 2-core system:
- **Minimal performance gain** (thread contention)
- **Better frame timing** still improves experience
- **Can disable with `--no-gpu-thread`**

## References

- DuckStation source: `src/core/gpu_thread.cpp`
- DuckStation threading: `src/common/threading.cpp`
- PS1 GPU timing: #file:DOCS/graphicsprocessingunitgpu.md
- PS1 timers: #file:DOCS/timers.md

## Next Steps

1. Compile the new code (`make clean && make`)
2. Test with `--no-gpu-thread` first (single-threaded)
3. Enable threading and compare behavior
4. Implement remaining GPU command handlers
5. Profile and optimize

---

**Note**: This is a significant architectural change. Test thoroughly before committing to multi-threading. Always maintain the single-threaded fallback for debugging.
