#include "../include/emulator.h"
#include "../include/log.h"
#include "../include/cpu.h"
#include "../include/interconnect.h"
#include "../include/dma.h"
#include "../include/event_scheduler.h"
#include <stdlib.h>

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
    uint32_t instruction = nc_interconnect_read32(ctx->interconnect, cpu->current_pc);
    NC_LOGI("[CPU] PC=0x%08x, instruction=0x%08x", cpu->current_pc, instruction);

    // Advance PC for next cycle (sequential, will be updated by branches)
    cpu->pc = cpu->next_pc;
    cpu->next_pc = cpu->pc + 4;

    // Call real decode/execute
    nc_decode_and_execute(cpu, instruction);
}

// Stub for event scheduler step (to be implemented/ported)
void nc_eventq_dispatch_due(EmulatorContext* ctx) {
    NC_LOGI("Event scheduler step (stub)");
    // TODO: Dispatch due events using ctx->interconnect and other subsystems
}

int emulator_run(EmulatorContext* ctx) {
    NC_LOGI("Emulator core starting main loop");
    // Allocate and initialize interconnect
    ctx->interconnect = malloc(sizeof(struct NcInterconnect));
    if (!ctx->interconnect) {
        NC_LOGE("Failed to allocate interconnect");
        return 1;
    }
    // Initialize DMA and event queue
    nc_dma_init(&ctx->dma);
    nc_eventq_init(&ctx->eventq);
    // Schedule a test event
    nc_eventq_schedule(&ctx->eventq, NC_EVENT_TIMER0, 5, NULL, NULL);
    int running = 1;
    int step_count = 0;
    while (running) {
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
        if (step_count > 10) running = 0;
    }
    NC_LOGI("Emulator core main loop exited");
    free(ctx->interconnect);
    return 0;
} 