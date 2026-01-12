# Multi-Threading Quick Reference

## Files Added

```
include/
├── threading.h         ← Threading primitives (mutex, semaphore, etc.)
└── gpu_thread.h        ← GPU thread system and command FIFO

src/
├── threading.c         ← POSIX pthread implementation
└── gpu_thread.c        ← GPU thread logic and command processing

Docs/
├── MULTI_THREADING_GUIDE.md              ← Complete integration guide
└── DUCKSTATION_THREADING_COMPARISON.md   ← C vs C++ comparison
```

## Quick Start

### 1. Compile with Threading Support

```bash
make clean
make
```

The Makefile now includes:
- `-std=c11` (for C11 atomics)
- `-pthread` (POSIX threads)
- `-lrt` (real-time extensions)
- `src/threading.c`
- `src/gpu_thread.c`

### 2. Enable/Disable GPU Threading

```bash
# Run with GPU threading (default)
./myps1_emu roms/SCPH1001.BIN

# Run single-threaded (for debugging)
./myps1_emu --no-gpu-thread roms/SCPH1001.BIN

# Run with debug logs
./myps1_emu --debug --no-gpu-thread roms/SCPH1001.BIN
```

### 3. Basic Integration Pattern

```c
// In interconnect.h
#include "gpu_thread.h"

struct Interconnect {
    GpuThreadState gpu_thread_state;
    // ... existing fields
};

// In main.c initialization
gpu_thread_init(&inter.gpu_thread_state, &inter.gpu, true);
gpu_thread_start(&inter.gpu_thread_state);

// Convert GPU calls from direct to threaded
// BEFORE:
void gpu_fill_vram(Gpu* gpu, uint16_t x, uint16_t y, ...) {
    glClearColor(...);  // Direct OpenGL call
}

// AFTER:
void gpu_fill_vram(Interconnect* inter, uint16_t x, uint16_t y, ...) {
    GpuFillVramCommand* cmd = GPU_BEGIN_COMMAND(
        &inter->gpu_thread_state, 
        GpuFillVramCommand, 
        GPU_CMD_FILL_VRAM
    );
    cmd->x = x;
    cmd->y = y;
    // ... fill command
    GPU_SUBMIT_COMMAND(&inter->gpu_thread_state, cmd);
}

// In main.c shutdown
gpu_thread_shutdown(&inter.gpu_thread_state);
```

## Threading API Quick Reference

### Thread Management

```c
ThreadHandle handle;
thread_create(&handle, "MyThread", entry_func, user_data);
thread_join(&handle);                    // Wait for completion
thread_set_name("Thread Name");          // For debugging
thread_sleep_us(1000);                   // Sleep 1ms
```

### Mutex (Mutual Exclusion)

```c
Mutex mutex;
mutex_init(&mutex);
mutex_lock(&mutex);
// ... critical section ...
mutex_unlock(&mutex);
mutex_destroy(&mutex);
```

### Semaphore (Signaling)

```c
Semaphore sem;
semaphore_init(&sem, 0);      // Initial value 0
semaphore_wait(&sem);         // Wait (decrement)
semaphore_post(&sem);         // Signal (increment)
semaphore_destroy(&sem);
```

### Atomic Operations

```c
AtomicUInt32 counter = {0};
uint32_t val = atomic_load_u32(&counter);
atomic_store_u32(&counter, 42);
atomic_fetch_add_u32(&counter, 1);     // Returns old value
```

### Timing

```c
uint64_t now = time_get_nanos();       // Nanoseconds
uint64_t now = time_get_micros();      // Microseconds
time_sleep_until(target_time, true);   // Sleep with spin-wait
```

## GPU Thread API Quick Reference

### Initialization

```c
GpuThreadState state;
gpu_thread_init(&state, gpu, use_threading);
gpu_thread_start(&state);
gpu_thread_stop(&state);
gpu_thread_shutdown(&state);
```

### Command Submission

```c
// Method 1: Manual allocation
GpuFillVramCommand* cmd = (GpuFillVramCommand*)
    gpu_thread_alloc_command(&state, sizeof(GpuFillVramCommand));
cmd->header.type = GPU_CMD_FILL_VRAM;
cmd->header.size = sizeof(GpuFillVramCommand);
cmd->x = 0;
cmd->y = 0;
// ... fill fields ...
gpu_thread_submit_and_wake(&state, &cmd->header);

// Method 2: Using helper macros (recommended)
GpuFillVramCommand* cmd = GPU_BEGIN_COMMAND(
    &state, GpuFillVramCommand, GPU_CMD_FILL_VRAM
);
cmd->x = 0;
cmd->y = 0;
// ... fill fields ...
GPU_SUBMIT_COMMAND(&state, cmd);
```

### Synchronization

```c
// Check if GPU is idle
if (gpu_thread_is_idle(&state)) {
    // No commands pending
}

// Wait for GPU to finish all commands
gpu_thread_sync(&state, true);   // With spin-wait (low latency)
gpu_thread_sync(&state, false);  // Sleep-wait (less CPU)

// Check available FIFO space
uint32_t space = gpu_thread_get_fifo_space(&state);
```

## Command Types

```c
GPU_CMD_DRAW_POLYGON              // Draw 3-4 point polygon
GPU_CMD_DRAW_LINE                 // Draw line
GPU_CMD_DRAW_RECTANGLE            // Draw rectangle
GPU_CMD_FILL_VRAM                 // Fill VRAM region
GPU_CMD_COPY_VRAM_TO_VRAM         // Copy within VRAM
GPU_CMD_COPY_CPU_TO_VRAM          // CPU -> VRAM transfer
GPU_CMD_COPY_VRAM_TO_CPU          // VRAM -> CPU transfer (must sync!)
GPU_CMD_SET_DRAW_MODE             // Set drawing parameters
GPU_CMD_SET_TEXTURE_WINDOW        // Set texture window
GPU_CMD_SET_DRAWING_AREA_TOP_LEFT // Set draw area
GPU_CMD_SET_DRAWING_AREA_BOTTOM_RIGHT
GPU_CMD_SET_DRAWING_OFFSET        // Set draw offset
GPU_CMD_SET_MASK_BIT              // Set mask bit setting
GPU_CMD_CLEAR_CACHE               // Clear texture cache
GPU_CMD_DISPLAY_MODE              // Set display mode
GPU_CMD_DISPLAY_ENABLE            // Enable/disable display
GPU_CMD_DISPLAY_START             // Set display start
GPU_CMD_VBLANK                    // VBlank event
GPU_CMD_SYNC                      // Synchronization point
```

## Performance Tips

### DO ✅

```c
// Batch commands together
for (int i = 0; i < 100; i++) {
    gpu_draw_triangle(...);  // Submits command
}
// Sync once at end if needed
gpu_thread_sync(&state, true);

// Use spin-wait for time-critical syncs
gpu_thread_sync(&state, true);

// Check idle before syncing
if (!gpu_thread_is_idle(&state)) {
    gpu_thread_sync(&state, true);
}
```

### DON'T ❌

```c
// Don't sync after every command (slow!)
gpu_draw_triangle(...);
gpu_thread_sync(&state, true);  // ← Bad!

// Don't allocate huge commands
uint8_t huge_data[1024*1024];  // ← Too big for FIFO

// Don't forget to sync before reading VRAM
gpu_submit_vram_read(...);
// Missing: gpu_thread_sync(&state, true);
uint32_t data = read_result();  // ← Wrong result!
```

## Debugging

### Enable Single-Threaded Mode

```bash
./myps1_emu --no-gpu-thread --debug
```

This runs everything on the CPU thread for easier debugging.

### Add Thread Logging

```c
LOG_GPU_DEBUG("Command %d at offset %u (thread %lu)", 
              cmd->type, offset, pthread_self());
```

### Compile with Thread Sanitizer

```makefile
CFLAGS += -fsanitize=thread -g
LDFLAGS += -fsanitize=thread
```

Detects data races and threading bugs.

### Check Thread State

```c
printf("Write ptr: %u\n", atomic_load_u32(&state->write_ptr));
printf("Read ptr: %u\n", atomic_load_u32(&state->read_ptr));
printf("Commands processed: %lu\n", state->commands_processed);
printf("Syncs: %lu, Wakes: %lu\n", state->sync_count, state->wake_count);
```

## PS1 Timing Integration

### VBlank Event (from event_scheduler.c)

```c
static void evq_handle_vblank(struct Interconnect* sys) {
    // CPU side: trigger IRQ
    sys->irq_state |= IRQ_VBLANK;
    
    // Submit VBlank to GPU thread
    GpuCommand* cmd = GPU_BEGIN_COMMAND(
        &sys->gpu_thread_state, GpuCommand, GPU_CMD_VBLANK
    );
    GPU_SUBMIT_COMMAND(&sys->gpu_thread_state, cmd);
    
    // Schedule next VBlank
    uint32_t cycles = sys->is_pal ? PAL_CYCLES_PER_VBLANK 
                                   : NTSC_CYCLES_PER_VBLANK;
    eventq_schedule(sys, EVQ_VBLANK, cycles);
}
```

### GPU Command Timing

```c
// GPU commands consume CPU cycles (from PS1 docs)
#define GPU_FILL_VRAM_CYCLES 46
#define GPU_COPY_VRAM_CYCLES 200

// Account for timing in CPU thread
sys->cpu_cycle_counter += GPU_FILL_VRAM_CYCLES;
```

## Common Patterns

### Pattern 1: Fire-and-Forget Commands

```c
// Submit command and continue (no wait)
GpuFillVramCommand* cmd = GPU_BEGIN_COMMAND(...);
cmd->x = 0;
cmd->y = 0;
GPU_SUBMIT_COMMAND(&state, cmd);
// Continue CPU work immediately
```

### Pattern 2: Submit and Wait

```c
// Submit command and wait for completion
GpuVramToCpuCommand* cmd = GPU_BEGIN_COMMAND(...);
cmd->dst_buffer = &result;
GPU_SUBMIT_COMMAND(&state, cmd);
gpu_thread_sync(&state, true);  // ← Must wait for result
```

### Pattern 3: Batch and Sync

```c
// Submit many commands, sync once
for (int i = 0; i < poly_count; i++) {
    GpuDrawPolygonCommand* cmd = GPU_BEGIN_COMMAND(...);
    // Fill polygon data
    GPU_SUBMIT_COMMAND(&state, cmd);
}
// Sync at frame boundary
gpu_thread_sync(&state, false);
```

## Architecture Diagram

```
┌──────────────────────────────────────────────────────────┐
│                      CPU Thread                          │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐     │
│  │ CPU Emu     │  │ Event Queue │  │ DMA/Timers  │     │
│  └─────────────┘  └─────────────┘  └─────────────┘     │
│                          │                               │
│                          ▼                               │
│  ┌────────────────────────────────────────────────┐     │
│  │        GPU Command Submission                  │     │
│  │  GPU_BEGIN_COMMAND() → GPU_SUBMIT_COMMAND()   │     │
│  └────────────────────────────────────────────────┘     │
└──────────────────────┬───────────────────────────────────┘
                       │
                       ▼ (Lock-free FIFO)
┌──────────────────────────────────────────────────────────┐
│                      GPU Thread                          │
│  ┌─────────────────────────────────────────────────┐    │
│  │  Command FIFO (16MB Ring Buffer)                │    │
│  │  ┌────┬────┬────┬────┬────┬────┬────┬────┐     │    │
│  │  │CMD │CMD │CMD │CMD │    │    │    │    │     │    │
│  │  └────┴────┴────┴────┴────┴────┴────┴────┘     │    │
│  │   read_ptr ↑         write_ptr ↑               │    │
│  └─────────────────────────────────────────────────┘    │
│                          │                               │
│                          ▼                               │
│  ┌────────────────────────────────────────────────┐     │
│  │   Command Processing & OpenGL Rendering        │     │
│  └────────────────────────────────────────────────┘     │
└──────────────────────────────────────────────────────────┘
```

## Next Steps

1. ✅ **Compiled successfully** - Check with `make`
2. ✅ **Single-threaded works** - Test with `--no-gpu-thread`
3. ⏳ **Implement GPU command handlers** - Fill in the `switch` statement in `gpu_thread.c`
4. ⏳ **Test with threading enabled** - Remove `--no-gpu-thread`
5. ⏳ **Profile performance** - Compare single vs multi-threaded
6. ⏳ **Optimize sync points** - Minimize unnecessary syncs
7. ⏳ **Add statistics** - Track performance metrics

## Resources

- Full Guide: `MULTI_THREADING_GUIDE.md`
- Comparison: `DUCKSTATION_THREADING_COMPARISON.md`
- DuckStation Source: `duckstation/src/core/gpu_thread.cpp`
- PS1 Docs: `DOCS/graphicsprocessingunitgpu.md`
- PS1 Timers: `DOCS/timers.md`

## Contact / Issues

If you encounter issues:

1. Try single-threaded mode first: `--no-gpu-thread`
2. Enable debug logs: `--debug`
3. Check thread sanitizer: `make CFLAGS+="-fsanitize=thread"`
4. Review the full integration guide
5. Compare against DuckStation's implementation

---

**Status**: Threading infrastructure complete ✅  
**Next**: Implement GPU command handlers in `gpu_thread.c`
