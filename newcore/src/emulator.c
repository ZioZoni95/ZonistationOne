#include "../include/emulator.h"
#include "../include/log.h"
#include "../include/cpu.h"
#include "../include/interconnect.h"
#include "../include/dma.h"
#include "../include/event_scheduler.h"
#include "../include/bios.h"
#include "../include/gpu.h"
#include <stdlib.h>
#include <string.h>

// Event handlers
static void vblank_handler(void* ctx) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    NC_LOGD("VBlank event triggered - GPU can update display");
    // TODO: Trigger GPU VBlank interrupt, update display
    // For testing: don't reschedule indefinitely
    // nc_eventq_schedule(&ectx->eventq, NC_EVENT_VBLANK, 16667, vblank_handler, ctx);
}

static void dma_handler(void* ctx) {
    EmulatorContext* ectx = (EmulatorContext*)ctx;
    NC_LOGD("DMA event triggered - process DMA transfers");
    // Simulate DMA completion: set DMA interrupt bit (bit 3) in I_STAT
    ectx->irq_status |= (1 << 3); // DMA interrupt
    NC_LOGI("[DMA] DMA interrupt set (I_STAT |= 0x8)");
    // Optionally, reschedule DMA event for continuous operation
    nc_eventq_schedule(&ectx->eventq, NC_EVENT_DMA, ectx->cycle_count + 1000, dma_handler, ctx);
}

// Real CPU step: fetch, decode, execute
void nc_cpu_step(EmulatorContext* ctx) {
    NcCpu* cpu = &ctx->cpu;
    cpu->exception_pending = false;
    cpu->current_pc = cpu->pc;

    // Check PC alignment
    if (cpu->current_pc % 4 != 0) {
        NC_LOGE("[CPU] PC Alignment Error: PC=0x%08x", cpu->current_pc);
        // TODO: Trigger exception
        return;
    }

    // Fetch instruction from memory (via interconnect)
    uint32_t instruction = nc_interconnect_read32(&ctx->interconnect, cpu->current_pc);
    NC_LOGT("[CPU] PC=0x%08x, instruction=0x%08x", cpu->current_pc, instruction);

    // Advance PC for next cycle (sequential, will be updated by branches)
    cpu->pc = cpu->next_pc;
    cpu->next_pc = cpu->pc + 4;

    // Call real decode/execute
    nc_decode_and_execute(cpu, instruction);
    
    // Update cycle counter for timing simulation
    ctx->cycle_count++;
}

void emulator_init(EmulatorContext* ctx) {
    // Initialize all fields to zero
    memset(ctx, 0, sizeof(EmulatorContext));
    
    // Load BIOS
    if (!nc_bios_load(&ctx->bios, "roms/SCPH1001.BIN")) {
        NC_LOGE("Failed to load BIOS. Exiting.");
        return;
    }
    
    // Initialize CPU with interconnect
    nc_cpu_init(&ctx->cpu, &ctx->interconnect);
    
    // Initialize DMA and event queue
    nc_dma_init(&ctx->dma);
    nc_eventq_init(&ctx->eventq);
    nc_gpu_init(&ctx->gpu); // Initialize GPU and VRAM
    
    // Initialize scratchpad (1KB data cache RAM)
    memset(&ctx->scratchpad, 0, sizeof(NcScratchpad));
    
    // Initialize interrupt controller state
    ctx->irq_status = 0; // No pending interrupts
    ctx->irq_mask = 0;   // All interrupts masked initially
    
    // Initialize cycle counter
    ctx->cycle_count = 0;
    
    // Schedule VBlank events (60Hz = ~16667 cycles)
    nc_eventq_schedule(&ctx->eventq, NC_EVENT_VBLANK, 16667, vblank_handler, ctx);
    // Schedule DMA events (periodic)
    nc_eventq_schedule(&ctx->eventq, NC_EVENT_DMA, 1000, dma_handler, ctx);
    
    NC_LOGI("Emulator initialized successfully");
}

int emulator_run(EmulatorContext* ctx) {
    NC_LOGI("Emulator core starting main loop");
    
    // Initialize emulator if not already done
    if (ctx->cycle_count == 0) {
        emulator_init(ctx);
    }
    
    int running = 1;
    int step_count = 0;
    while (running) {
        NC_LOGT("[TRACE] Main loop iteration %d", step_count);
        // 1. Step CPU (fetch, decode, execute)
        nc_cpu_step(ctx);
        // 2. Step DMA
        nc_dma_step(&ctx->dma);
        // 3. Step event scheduler (timers, VBlank, DMA, etc.)
        ctx->eventq.current_cycle = step_count;
        nc_eventq_dispatch_due(&ctx->eventq);
        // 4. Render frame if needed (e.g., on VBlank)
        // TODO: renderer->render_frame(ctx->vram.data, 1024, 512);
        // 5. Handle input, audio, etc. (future)
        // 6. Check for exit condition (stub: break after a few steps for now)
        step_count++;
        if (step_count > 2000) {
            NC_LOGI("Emulator reached step limit, exiting");
            running = 0;
        }
    }
    NC_LOGI("Emulator core main loop exited");
    return 0;
} 