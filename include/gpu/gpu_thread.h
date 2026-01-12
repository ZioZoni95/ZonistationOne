#ifndef GPU_THREAD_H
#define GPU_THREAD_H

/**
 * gpu_thread.h
 * 
 * GPU Thread System for ZonistationOne Emulator
 * Inspired by DuckStation's GPU threading architecture but adapted for C
 * 
 * Architecture:
 * - CPU thread generates GPU commands and pushes them to a command FIFO
 * - GPU thread consumes commands from FIFO and executes them
 * - Lock-free ring buffer for command queue
 * - Semaphore-based thread synchronization with spin-wait optimization
 * 
 * Benefits:
 * - GPU rendering doesn't block CPU emulation
 * - Better multi-core utilization
 * - Smoother frame pacing
 */

#include <stdint.h>
#include <stdbool.h>
#include "threading.h"

// Forward declarations
struct Gpu;
struct Interconnect;

// ============================================================================
// GPU Command Types
// ============================================================================

typedef enum {
    GPU_CMD_DRAW_POLYGON,
    GPU_CMD_DRAW_LINE,
    GPU_CMD_DRAW_RECTANGLE,
    GPU_CMD_FILL_VRAM,
    GPU_CMD_COPY_VRAM_TO_VRAM,
    GPU_CMD_COPY_CPU_TO_VRAM,
    GPU_CMD_COPY_VRAM_TO_CPU,
    GPU_CMD_SET_DRAW_MODE,
    GPU_CMD_SET_TEXTURE_WINDOW,
    GPU_CMD_SET_DRAWING_AREA_TOP_LEFT,
    GPU_CMD_SET_DRAWING_AREA_BOTTOM_RIGHT,
    GPU_CMD_SET_DRAWING_OFFSET,
    GPU_CMD_SET_MASK_BIT,
    GPU_CMD_CLEAR_CACHE,
    GPU_CMD_DISPLAY_MODE,
    GPU_CMD_DISPLAY_ENABLE,
    GPU_CMD_DISPLAY_START,
    GPU_CMD_VBLANK,
    GPU_CMD_GP0_COMMAND,    // Raw GP0 command (for direct passthrough)
    GPU_CMD_GP1_COMMAND,    // Raw GP1 command (for direct passthrough)
    GPU_CMD_SYNC,           // Synchronization point
    GPU_CMD_SHUTDOWN,       // Thread shutdown signal
    GPU_CMD_WRAPAROUND,     // FIFO wraparound marker (internal use)
    GPU_CMD_COUNT
} GpuCommandType;

// ============================================================================
// GPU Command Structure
// ============================================================================

// Base command header (8 bytes)
typedef struct GpuCommand {
    GpuCommandType type;
    uint32_t size;  // Total size of this command in bytes (including header)
} GpuCommand;

// Draw polygon command
typedef struct GpuDrawPolygonCommand {
    GpuCommand header;
    uint32_t vertices[12];  // Up to 4 vertices (x, y, color/texture)
    uint8_t vertex_count;
    bool is_textured;
    bool is_blended;
    bool is_quad;
    uint8_t padding;
} GpuDrawPolygonCommand;

// Fill VRAM command
typedef struct GpuFillVramCommand {
    GpuCommand header;
    uint16_t x, y;
    uint16_t width, height;
    uint32_t color;
} GpuFillVramCommand;

// VRAM copy command
typedef struct GpuCopyVramCommand {
    GpuCommand header;
    uint16_t src_x, src_y;
    uint16_t dst_x, dst_y;
    uint16_t width, height;
} GpuCopyVramCommand;

// CPU to VRAM transfer command (includes data)
typedef struct GpuCpuToVramCommand {
    GpuCommand header;
    uint16_t x, y;
    uint16_t width, height;
    uint32_t data[];  // Variable-length data array
} GpuCpuToVramCommand;

// VRAM to CPU transfer command
typedef struct GpuVramToCpuCommand {
    GpuCommand header;
    uint16_t x, y;
    uint16_t width, height;
    uint32_t* dst_buffer;  // Pointer to CPU buffer to fill (on CPU side)
} GpuVramToCpuCommand;

// Display mode command
typedef struct GpuDisplayModeCommand {
    GpuCommand header;
    uint16_t display_x, display_y;
    uint16_t display_width, display_height;
    bool is_24bit;
    bool is_pal;
    bool is_interlaced;
} GpuDisplayModeCommand;

// Sync command (blocks CPU thread until GPU catches up)
typedef struct GpuSyncCommand {
    GpuCommand header;
    volatile bool* sync_flag;  // CPU sets to true when GPU completes
} GpuSyncCommand;

// Raw GP0 command (for passthrough to gpu_gp0)
typedef struct GpuGP0Command {
    GpuCommand header;
    uint32_t command_word;
} GpuGP0Command;

// Raw GP1 command (for passthrough to gpu_gp1)
typedef struct GpuGP1Command {
    GpuCommand header;
    uint32_t command_word;
} GpuGP1Command;

// ============================================================================
// Command FIFO Configuration
// ============================================================================

#define GPU_COMMAND_FIFO_SIZE (16 * 1024 * 1024)  // 16MB command buffer
#define GPU_WAKE_THRESHOLD (64 * 1024)             // Wake GPU after 64KB of commands

// ============================================================================
// GPU Thread State
// ============================================================================

typedef struct GpuThreadState {
    // Thread control
    ThreadHandle gpu_thread;
    volatile bool is_running;
    volatile bool shutdown_requested;
    bool use_threading;  // Can disable threading for debugging
    
    // Command FIFO (lock-free ring buffer)
    uint8_t* command_fifo;
    AtomicUInt32 write_ptr;  // CPU writes here
    AtomicUInt32 read_ptr;   // GPU reads here
    uint32_t fifo_size;
    
    // Synchronization
    Semaphore wake_semaphore;    // CPU signals GPU to wake up
    Semaphore sync_semaphore;    // GPU signals CPU that sync is complete
    AtomicInt32 pending_commands; // Number of pending commands (for CPU to check)
    
    // Spin-wait optimization
    uint64_t spin_time_ns;       // How long to spin before sleeping
    
    // Statistics
    uint64_t commands_processed;
    uint64_t sync_count;
    uint64_t wake_count;
    
    // GPU context (owned by GPU thread)
    struct GPU* gpu;
    
    // OpenGL context for GPU thread
    void* sdl_window;       // SDL_Window* (void* to avoid SDL include here)
    void* gl_context;       // SDL_GLContext (void* to avoid SDL include here)
    
} GpuThreadState;

// ============================================================================
// GPU Thread API
// ============================================================================

/**
 * @brief Initialize the GPU thread system
 * @param state GPU thread state to initialize
 * @param gpu Pointer to GPU context
 * @param use_threading Enable/disable threading (false for single-threaded mode)
 * @return true on success
 */
bool gpu_thread_init(GpuThreadState* state, struct GPU* gpu, bool use_threading);

/**
 * @brief Shutdown the GPU thread
 * @param state GPU thread state
 */
void gpu_thread_shutdown(GpuThreadState* state);

/**
 * @brief Start the GPU thread (begin running)
 * @param state GPU thread state
 * @return true on success
 */
bool gpu_thread_start(GpuThreadState* state);

/**
 * @brief Stop the GPU thread (graceful shutdown)
 * @param state GPU thread state
 */
void gpu_thread_stop(GpuThreadState* state);

// ============================================================================
// Command Submission (CPU Thread)
// ============================================================================

/**
 * @brief Allocate space in the command FIFO for a new command
 * @param state GPU thread state
 * @param size Size of command in bytes (including header)
 * @return Pointer to command buffer, or NULL if FIFO full
 */
void* gpu_thread_alloc_command(GpuThreadState* state, uint32_t size);

/**
 * @brief Submit a command to the GPU thread
 * @param state GPU thread state
 * @param cmd Command pointer (from gpu_thread_alloc_command)
 */
void gpu_thread_submit_command(GpuThreadState* state, GpuCommand* cmd);

/**
 * @brief Submit a command and wake the GPU thread if needed
 * @param state GPU thread state
 * @param cmd Command pointer
 */
void gpu_thread_submit_and_wake(GpuThreadState* state, GpuCommand* cmd);

/**
 * @brief Synchronize with GPU thread (wait for all commands to complete)
 * @param state GPU thread state
 * @param spin Whether to use spin-wait optimization
 */
void gpu_thread_sync(GpuThreadState* state, bool spin);

/**
 * @brief Check if command FIFO is empty
 * @param state GPU thread state
 * @return true if FIFO is empty
 */
bool gpu_thread_is_idle(const GpuThreadState* state);

/**
 * @brief Get number of bytes available in FIFO
 * @param state GPU thread state
 * @return Available bytes
 */
uint32_t gpu_thread_get_fifo_space(const GpuThreadState* state);

/**
 * @brief Set OpenGL context for GPU thread
 * @param state GPU thread state
 * @param window SDL_Window pointer
 * @param gl_context SDL_GLContext
 * Note: Must be called before gpu_thread_start()
 */
void gpu_thread_set_gl_context(GpuThreadState* state, void* window, void* gl_context);

// ============================================================================
// Helper Functions and Macros for Command Submission
// ============================================================================

/**
 * @brief Allocate and initialize a command (internal helper)
 */
static inline void* gpu_alloc_and_init_command(GpuThreadState* state, uint32_t size, GpuCommandType type) {
    void* cmd = gpu_thread_alloc_command(state, size);
    if (cmd) {
        ((GpuCommand*)cmd)->type = type;
        ((GpuCommand*)cmd)->size = size;
    }
    return cmd;
}

/**
 * Begin a GPU command (allocates space and returns typed pointer)
 * Usage:
 *   GpuFillVramCommand* cmd = GPU_BEGIN_COMMAND(&state, GpuFillVramCommand, GPU_CMD_FILL_VRAM);
 *   if (cmd) {
 *       cmd->x = 0;
 *       cmd->y = 0;
 *       // ... fill in command
 *       GPU_SUBMIT_COMMAND(&state, cmd);
 *   }
 */
#define GPU_BEGIN_COMMAND(state_ptr, cmd_struct_type, cmd_enum_type) \
    ((cmd_struct_type*)gpu_alloc_and_init_command(state_ptr, sizeof(cmd_struct_type), cmd_enum_type))

/**
 * Submit a GPU command (with automatic waking if needed)
 */
#define GPU_SUBMIT_COMMAND(state, cmd) \
    gpu_thread_submit_and_wake(state, (GpuCommand*)(cmd))

#endif // GPU_THREAD_H
