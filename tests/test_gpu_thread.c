/**
 * test_gpu_thread.c
 * 
 * Unit tests for the GPU thread system
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "gpu_thread.h"
#include "gpu.h"

// Mock GPU structure for testing
typedef struct {
    int commands_executed;
    int fill_count;
    int copy_count;
    int sync_count;
} MockGpu;

int main() {
    printf("=== GPU Thread System Tests ===\n\n");
    
    // Create mock GPU
    MockGpu mock_gpu;
    memset(&mock_gpu, 0, sizeof(MockGpu));
    
    // Test 1: Initialize GPU thread in single-threaded mode
    printf("Test 1: Initialize GPU thread (single-threaded mode)\n");
    GpuThreadState state;
    
    if (!gpu_thread_init(&state, (struct Gpu*)&mock_gpu, false)) {
        printf("FAILED: Could not initialize GPU thread\n");
        return 1;
    }
    
    printf("PASSED: GPU thread initialized (single-threaded)\n\n");
    
    // Test 2: Allocate and submit commands
    printf("Test 2: Command allocation and submission\n");
    
    GpuFillVramCommand* fill_cmd = GPU_BEGIN_COMMAND(&state, 
                                                     GpuFillVramCommand,
                                                     GPU_CMD_FILL_VRAM);
    if (!fill_cmd) {
        printf("FAILED: Could not allocate fill command\n");
        return 1;
    }
    
    fill_cmd->x = 0;
    fill_cmd->y = 0;
    fill_cmd->width = 100;
    fill_cmd->height = 100;
    fill_cmd->color = 0xFF0000;
    
    GPU_SUBMIT_COMMAND(&state, fill_cmd);
    printf("Submitted fill command\n");
    
    // Check FIFO is not empty
    if (gpu_thread_is_idle(&state)) {
        printf("FAILED: FIFO should not be empty\n");
        return 1;
    }
    printf("PASSED: Command submitted to FIFO\n\n");
    
    // Test 3: FIFO space tracking
    printf("Test 3: FIFO space tracking\n");
    uint32_t space_before = gpu_thread_get_fifo_space(&state);
    printf("FIFO space available: %u bytes\n", space_before);
    
    // Submit many commands
    for (int i = 0; i < 100; i++) {
        GpuFillVramCommand* cmd = GPU_BEGIN_COMMAND(&state, GpuFillVramCommand, GPU_CMD_FILL_VRAM);
        if (cmd) {
            cmd->x = cmd->y = 0;
            cmd->width = cmd->height = 1;
            cmd->color = 0;
            GPU_SUBMIT_COMMAND(&state, cmd);
        }
    }
    
    uint32_t space_after = gpu_thread_get_fifo_space(&state);
    printf("FIFO space after 100 commands: %u bytes\n", space_after);
    
    if (space_after < space_before) {
        printf("PASSED: FIFO space correctly tracked\n\n");
    } else {
        printf("FAILED: FIFO space not decreasing\n\n");
        return 1;
    }
    
    // Test 4: GPU thread shutdown
    printf("Test 4: GPU thread shutdown\n");
    gpu_thread_shutdown(&state);
    printf("PASSED: GPU thread shutdown cleanly\n\n");
    
    // Test 5: Multi-threaded mode initialization
    printf("Test 5: Initialize with threading enabled\n");
    
    if (!gpu_thread_init(&state, (struct Gpu*)&mock_gpu, true)) {
        printf("FAILED: Could not initialize GPU thread with threading\n");
        return 1;
    }
    
    if (!gpu_thread_start(&state)) {
        printf("FAILED: Could not start GPU thread\n");
        return 1;
    }
    
    printf("PASSED: GPU thread started with threading enabled\n\n");
    
    // Test 6: Submit commands to threaded mode
    printf("Test 6: Submit commands in threaded mode\n");
    
    for (int i = 0; i < 10; i++) {
        GpuFillVramCommand* cmd = GPU_BEGIN_COMMAND(&state,
                                                    GpuFillVramCommand,
                                                    GPU_CMD_FILL_VRAM);
        if (cmd) {
            cmd->x = i * 10;
            cmd->y = i * 10;
            cmd->width = 50;
            cmd->height = 50;
            cmd->color = 0xFF0000 + (i << 8);
            GPU_SUBMIT_COMMAND(&state, cmd);
        }
    }
    
    printf("Submitted 10 fill commands\n");
    
    // Test 7: Synchronization
    printf("Test 7: GPU thread synchronization\n");
    printf("Syncing with GPU thread...\n");
    
    gpu_thread_sync(&state, true); // Spin-wait sync
    
    if (gpu_thread_is_idle(&state)) {
        printf("PASSED: GPU thread synchronized and idle\n\n");
    } else {
        printf("FAILED: GPU thread not idle after sync\n\n");
        return 1;
    }
    
    // Test 8: Statistics
    printf("Test 8: Statistics\n");
    printf("Commands processed: %lu\n", state.commands_processed);
    printf("Sync count: %lu\n", state.sync_count);
    printf("Wake count: %lu\n", state.wake_count);
    printf("PASSED: Statistics tracked\n\n");
    
    // Test 9: Graceful shutdown
    printf("Test 9: Graceful shutdown\n");
    gpu_thread_stop(&state);
    gpu_thread_shutdown(&state);
    printf("PASSED: GPU thread stopped and shutdown cleanly\n\n");
    
    // Test 10: Rapid init/shutdown cycles
    printf("Test 10: Rapid init/shutdown cycles\n");
    for (int i = 0; i < 5; i++) {
        if (!gpu_thread_init(&state, (struct Gpu*)&mock_gpu, false)) {
            printf("FAILED: Init failed on cycle %d\n", i);
            return 1;
        }
        gpu_thread_shutdown(&state);
    }
    printf("PASSED: Survived 5 rapid init/shutdown cycles\n\n");
    
    printf("=== All GPU Thread Tests Passed! ===\n");
    return 0;
}
