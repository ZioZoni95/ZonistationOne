/**
 * gpu_thread.c
 * 
 * GPU Thread System Implementation for ZonistationOne Emulator
 * Based on DuckStation's lock-free FIFO command queue architecture
 */

#include "gpu/gpu_thread.h"
#include "gpu/gpu_core.h"
#include "gpu/gpu_commands.h"
#include "gpu.h"
#include "log.h"
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>

// Forward declarations for direct command processing
void gp0_dispatch_command(GPU* gpu, uint32_t command);
void gp1_dispatch_command(GPU* gpu, uint32_t command);

// ============================================================================
// Internal Helper Functions
// ============================================================================

static inline uint32_t wrap_fifo_ptr(uint32_t ptr, uint32_t size) {
    return ptr >= size ? (ptr - size) : ptr;
}

static inline uint32_t get_fifo_used_space(const GpuThreadState* state) {
    uint32_t write = atomic_load_u32(&state->write_ptr);
    uint32_t read = atomic_load_u32(&state->read_ptr);
    
    if (write >= read) {
        return write - read;
    } else {
        return state->fifo_size - read + write;
    }
}

static inline uint32_t get_fifo_free_space(const GpuThreadState* state) {
    // Leave one byte unused to distinguish full from empty
    return state->fifo_size - get_fifo_used_space(state) - 1;
}

// ============================================================================
// GPU Thread Entry Point
// ============================================================================

static void* gpu_thread_entry(void* user_data) {
    GpuThreadState* state = (GpuThreadState*)user_data;
    
    thread_set_name("GPU Thread");
    
    // Make OpenGL context current on this thread
    if (state->sdl_window && state->gl_context) {
        if (SDL_GL_MakeCurrent((SDL_Window*)state->sdl_window, (SDL_GLContext)state->gl_context) != 0) {
            LOG_GPU_ERROR("Failed to make GL context current on GPU thread: %s", SDL_GetError());
            return NULL;
        }
        LOG_GPU_INFO("GPU thread started with OpenGL context");
    } else {
        LOG_GPU_INFO("GPU thread started (no GL context)");
    }
    
    uint64_t commands_processed_local = 0;
    
    while (!state->shutdown_requested) {
        uint32_t write_ptr = atomic_load_u32(&state->write_ptr);
        uint32_t read_ptr = atomic_load_u32(&state->read_ptr);
        
        // Check if FIFO is empty
        if (read_ptr == write_ptr) {
            // No commands to process - wait for wake signal
            semaphore_wait(&state->wake_semaphore);
            
            // Recheck after wake (might be shutdown signal)
            if (state->shutdown_requested) {
                break;
            }
            
            continue;
        }
        
        // Process commands in FIFO
        while (read_ptr != write_ptr) {
            // Read command header
            GpuCommand* cmd = (GpuCommand*)(state->command_fifo + read_ptr);
            uint32_t cmd_size = cmd->size;
            
            // Sanity check
            if (cmd_size == 0 || cmd_size > state->fifo_size) {
                LOG_GPU_ERROR("GPU thread: corrupt command size %u at offset %u", 
                             cmd_size, read_ptr);
                // Skip this corrupted command by advancing minimum size
                read_ptr = wrap_fifo_ptr(read_ptr + sizeof(GpuCommand), state->fifo_size);
                atomic_store_u32(&state->read_ptr, read_ptr);
                
                // Wait a bit before retrying
                usleep(1000);
                break;
            }
            
            // Process command based on type
            switch (cmd->type) {
                case GPU_CMD_WRAPAROUND: {
                    // FIFO wraparound marker - skip and reset read pointer to 0
                    LOG_GPU_DEBUG("[GPU_THREAD] Wraparound marker at %u, size %u", read_ptr, cmd_size);
                    atomic_store_u32(&state->read_ptr, 0);
                    read_ptr = 0;
                    write_ptr = atomic_load_u32(&state->write_ptr);
                    continue; // Skip normal read_ptr advance
                }
                
                case GPU_CMD_GP0_COMMAND: {
                    // Process GP0 command on GPU thread
                    // Temporarily disable threading check to avoid recursion
                    GpuGP0Command* gp0_cmd = (GpuGP0Command*)cmd;
                    bool old_threading = state->gpu->thread_state->use_threading;
                    state->gpu->thread_state->use_threading = false;  // Prevent re-queuing
                    gpu_gp0(state->gpu, gp0_cmd->command_word);
                    state->gpu->thread_state->use_threading = old_threading;  // Restore
                    break;
                }
                
                case GPU_CMD_GP1_COMMAND: {
                    // Process GP1 command on GPU thread
                    GpuGP1Command* gp1_cmd = (GpuGP1Command*)cmd;
                    bool old_threading = state->gpu->thread_state->use_threading;
                    state->gpu->thread_state->use_threading = false;
                    gpu_gp1(state->gpu, gp1_cmd->command_word);
                    state->gpu->thread_state->use_threading = old_threading;
                    break;
                }
                
                case GPU_CMD_FILL_VRAM: {
                    // GpuFillVramCommand* fill_cmd = (GpuFillVramCommand*)cmd;
                    // TODO: Call GPU fill function
                    // gpu_fill_vram(state->gpu, fill_cmd->x, fill_cmd->y, ...);
                    break;
                }
                
                case GPU_CMD_COPY_VRAM_TO_VRAM: {
                    // GpuCopyVramCommand* copy_cmd = (GpuCopyVramCommand*)cmd;
                    // TODO: Call GPU VRAM copy function
                    break;
                }
                
                case GPU_CMD_COPY_CPU_TO_VRAM: {
                    // GpuCpuToVramCommand* transfer_cmd = (GpuCpuToVramCommand*)cmd;
                    // TODO: Call GPU CPU->VRAM transfer function
                    break;
                }
                
                case GPU_CMD_VBLANK: {
                    // TODO: Handle VBlank processing
                    break;
                }
                
                case GPU_CMD_SYNC: {
                    // Synchronization point - flush renderer and signal CPU thread
                    GpuSyncCommand* sync_cmd = (GpuSyncCommand*)cmd;
                    
                    // Flush any buffered rendering commands
                    renderer_draw(&state->gpu->renderer);
                    
                    if (sync_cmd->sync_flag) {
                        *sync_cmd->sync_flag = true;
                    }
                    // Signal the semaphore to wake waiting CPU thread
                    semaphore_post(&state->sync_semaphore);
                    break;
                }
                
                case GPU_CMD_SHUTDOWN: {
                    // Shutdown command - exit thread
                    LOG_GPU_INFO("GPU thread received shutdown command");
                    state->shutdown_requested = true;
                    break;
                }
                
                default:
                    LOG_GPU_WARN("GPU thread: unknown command type %d", cmd->type);
                    break;
            }
            
            // Advance read pointer
            read_ptr = wrap_fifo_ptr(read_ptr + cmd_size, state->fifo_size);
            atomic_store_u32(&state->read_ptr, read_ptr);
            
            commands_processed_local++;
            
            // Log threading status every 1000 commands
            if (commands_processed_local % 1000 == 0) {
                pthread_t tid_check = pthread_self();
                pid_t tid = syscall(SYS_gettid);
                LOG_GPU_DEBUG("[THREADING] GPU thread processed %llu commands on pthread=0x%lx tid=%d", 
                             (unsigned long long)commands_processed_local, (unsigned long)tid_check, tid);
            }
            
            // Check for more commands
            write_ptr = atomic_load_u32(&state->write_ptr);
        }
        
        // Update statistics
        state->commands_processed = commands_processed_local;
    }
    
    LOG_GPU_INFO("GPU thread exiting (processed %lu commands)", commands_processed_local);
    return NULL;
}

// ============================================================================
// GPU Thread Management
// ============================================================================

bool gpu_thread_init(GpuThreadState* state, struct GPU* gpu, bool use_threading) {
    memset(state, 0, sizeof(GpuThreadState));
    
    state->gpu = gpu;
    state->use_threading = use_threading;
    state->fifo_size = GPU_COMMAND_FIFO_SIZE;
    state->spin_time_ns = 50000; // 50 microseconds spin time
    state->sdl_window = NULL;  // Will be set via gpu_thread_set_gl_context()
    state->gl_context = NULL;
    
    // Allocate command FIFO (zero-initialized to prevent reading garbage)
    state->command_fifo = (uint8_t*)calloc(1, state->fifo_size);
    if (!state->command_fifo) {
        LOG_GPU_ERROR("Failed to allocate GPU command FIFO");
        return false;
    }
    
    // Initialize atomic pointers
    atomic_store_u32(&state->write_ptr, 0);
    atomic_store_u32(&state->read_ptr, 0);
    atomic_store_i32(&state->pending_commands, 0);
    
    // Initialize synchronization primitives
    semaphore_init(&state->wake_semaphore, 0);
    semaphore_init(&state->sync_semaphore, 0);
    
    LOG_GPU_INFO("GPU thread system initialized (threading %s)", 
                use_threading ? "enabled" : "disabled");
    return true;
}

void gpu_thread_shutdown(GpuThreadState* state) {
    if (!state) return;
    
    // Stop thread if running
    if (state->is_running) {
        gpu_thread_stop(state);
    }
    
    // Free resources
    if (state->command_fifo) {
        free(state->command_fifo);
        state->command_fifo = NULL;
    }
    
    semaphore_destroy(&state->wake_semaphore);
    semaphore_destroy(&state->sync_semaphore);
    
    LOG_GPU_INFO("GPU thread system shutdown");
}

bool gpu_thread_start(GpuThreadState* state) {
    if (!state->use_threading) {
        LOG_GPU_INFO("GPU threading disabled - running in single-threaded mode");
        state->is_running = true;
        return true;
    }
    
    if (state->is_running) {
        LOG_GPU_WARN("GPU thread already running");
        return true;
    }
    
    state->shutdown_requested = false;
    
    if (!thread_create(&state->gpu_thread, "GPU Thread", gpu_thread_entry, state)) {
        LOG_GPU_ERROR("Failed to create GPU thread");
        return false;
    }
    
    state->is_running = true;
    LOG_GPU_INFO("GPU thread started");
    
    return true;
}

void gpu_thread_stop(GpuThreadState* state) {
    if (!state->is_running) {
        return;
    }
    
    if (state->use_threading) {
        // Set shutdown flag first
        state->shutdown_requested = true;
        
        // Send shutdown command
        GpuCommand* cmd = (GpuCommand*)gpu_thread_alloc_command(state, sizeof(GpuCommand));
        if (cmd) {
            cmd->type = GPU_CMD_SHUTDOWN;
            cmd->size = sizeof(GpuCommand);
            gpu_thread_submit_and_wake(state, cmd);
        }
        
        // Wake the thread multiple times to ensure it sees the shutdown
        for (int i = 0; i < 3; i++) {
            semaphore_post(&state->wake_semaphore);
        }
        
        // Wait for thread to exit
        LOG_GPU_INFO("Waiting for GPU thread to exit...");
        thread_join(&state->gpu_thread);
    }
    
    state->is_running = false;
    LOG_GPU_INFO("GPU thread stopped");
}

// ============================================================================
// Command Submission (CPU Thread)
// ============================================================================

void* gpu_thread_alloc_command(GpuThreadState* state, uint32_t size) {
    // Align size to 8 bytes for better cache performance
    size = (size + 7) & ~7;
    
    LOG_GPU_DEBUG("[ALLOC] Requesting %u bytes (aligned)", size);
    
    // DuckStation pattern: Allocate space but DON'T advance write_ptr yet
    // write_ptr is advanced during submit via fetch_add
    
    for (;;) {
        uint32_t read_ptr = atomic_load_u32(&state->read_ptr);
        uint32_t write_ptr = atomic_load_u32(&state->write_ptr);
        
        LOG_GPU_DEBUG("[ALLOC] Current state: read=%u write=%u", read_ptr, write_ptr);
        
        if (read_ptr > write_ptr) {
            // Wrapped around - space is between write_ptr and read_ptr
            uint32_t available = read_ptr - write_ptr;
            while (available < (size + sizeof(GpuCommand))) {
                // Wait for GPU thread to free space
                LOG_GPU_DEBUG("[ALLOC] Waiting for space (wrapped): available=%u needed=%u", 
                             available, size + (uint32_t)sizeof(GpuCommand));
                read_ptr = atomic_load_u32(&state->read_ptr);
                available = (read_ptr > write_ptr) ? (read_ptr - write_ptr) : (state->fifo_size - write_ptr);
            }
        } else {
            // Normal case - space from write_ptr to end of buffer
            uint32_t available = state->fifo_size - write_ptr;
            
            if ((size + sizeof(GpuCommand)) > available) {
                // Not enough space at end - need to wrap
                LOG_GPU_DEBUG("[ALLOC] Wrapping: available=%u needed=%u", available, size);
                
                if (read_ptr == 0) {
                    // GPU hasn't processed anything yet, wait
                    LOG_GPU_DEBUG("[ALLOC] Waiting for GPU to start processing");
                    do {
                        read_ptr = atomic_load_u32(&state->read_ptr);
                    } while (read_ptr == 0);
                }
                
                // Insert wraparound marker
                GpuCommand* wrap_cmd = (GpuCommand*)(state->command_fifo + write_ptr);
                wrap_cmd->type = GPU_CMD_WRAPAROUND;
                wrap_cmd->size = available;
                
                LOG_GPU_DEBUG("[ALLOC] Inserted wraparound marker at %u, size %u", write_ptr, available);
                
                // Reset write pointer to beginning
                atomic_store_u32(&state->write_ptr, 0);
                continue; // Retry allocation from beginning
            }
        }
        
        // Return pointer at current write_ptr WITHOUT advancing it
        // write_ptr will be advanced during submit
        GpuCommand* cmd = (GpuCommand*)(state->command_fifo + write_ptr);
        cmd->size = size; // Store size for submit
        
        LOG_GPU_DEBUG("[ALLOC] Allocated at offset %u, size %u", write_ptr, size);
        return cmd;
    }
}

void gpu_thread_submit_command(GpuThreadState* state, GpuCommand* cmd) {
    // Command data is already written by caller
    // Now atomically advance write_ptr to make command visible to GPU thread
    // This is the DuckStation pattern: fetch_add advances write_ptr atomically
    
    // Ensure all command data is visible before advancing write_ptr
    __sync_synchronize();
    
    // Atomically advance write_ptr by command size
    // GPU thread sees command as soon as write_ptr advances past it
    uint32_t old_write_ptr = atomic_fetch_add_u32(&state->write_ptr, cmd->size);
    uint32_t new_write_ptr = old_write_ptr + cmd->size;
    
    LOG_GPU_DEBUG("[SUBMIT] Command type %d, size %u: write_ptr %u -> %u", 
                 cmd->type, cmd->size, old_write_ptr, new_write_ptr);
    
    // Sanity check
    if (new_write_ptr > state->fifo_size) {
        LOG_GPU_ERROR("FIFO write pointer overflow: %u > %u", new_write_ptr, state->fifo_size);
    }
    
    atomic_fetch_add_i32(&state->pending_commands, 1);
}

void gpu_thread_submit_and_wake(GpuThreadState* state, GpuCommand* cmd) {
    gpu_thread_submit_command(state, cmd);
    
    // Wake GPU thread if it's sleeping
    uint32_t used_space = get_fifo_used_space(state);
    if (used_space >= GPU_WAKE_THRESHOLD || cmd->type == GPU_CMD_SYNC) {
        semaphore_post(&state->wake_semaphore);
        state->wake_count++;
    }
}

void gpu_thread_sync(GpuThreadState* state, bool spin) {
    if (!state->use_threading) {
        // In single-threaded mode, just process all commands immediately
        // TODO: Process commands directly
        return;
    }
    
    // Check if already idle
    if (gpu_thread_is_idle(state)) {
        return;
    }
    
    // Submit sync command
    volatile bool sync_complete = false;
    GpuSyncCommand* sync_cmd = (GpuSyncCommand*)gpu_thread_alloc_command(
        state, sizeof(GpuSyncCommand));
    
    if (!sync_cmd) {
        LOG_GPU_ERROR("Failed to allocate sync command");
        return;
    }
    
    sync_cmd->header.type = GPU_CMD_SYNC;
    sync_cmd->header.size = sizeof(GpuSyncCommand);
    sync_cmd->sync_flag = &sync_complete;
    
    gpu_thread_submit_and_wake(state, &sync_cmd->header);
    
    state->sync_count++;
    
    // Wait for sync to complete
    if (spin) {
        // Spin-wait for low latency
        uint64_t start_time = time_get_nanos();
        while (!sync_complete) {
            if (time_get_nanos() - start_time > state->spin_time_ns) {
                // Timeout - fall back to semaphore wait
                semaphore_wait(&state->sync_semaphore);
                break;
            }
            __builtin_ia32_pause();
        }
    } else {
        // Just wait on semaphore
        semaphore_wait(&state->sync_semaphore);
    }
}

bool gpu_thread_is_idle(const GpuThreadState* state) {
    uint32_t write_ptr = atomic_load_u32(&state->write_ptr);
    uint32_t read_ptr = atomic_load_u32(&state->read_ptr);
    return write_ptr == read_ptr;
}

uint32_t gpu_thread_get_fifo_space(const GpuThreadState* state) {
    return get_fifo_free_space(state);
}

void gpu_thread_set_gl_context(GpuThreadState* state, void* window, void* gl_context) {
    state->sdl_window = window;
    state->gl_context = gl_context;
    LOG_GPU_INFO("GPU thread GL context set (window=%p, context=%p)", window, gl_context);
}
