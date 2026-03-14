#include "event_scheduler.h"
#include "interconnect.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "log.h"
#include "dma.h"           // For DMA structures and helpers
#include "gpu.h"           // For GPU DMA transfer
#include "timers.h"        // Add this include for timer event handler prototypes

// ===============================
// Event Scheduler Implementation
// ===============================
// This file implements the central event/timing system for the emulator.
// All hardware events (timers, VBlank, DMA, etc.) are scheduled and dispatched here.
// Naming and structure are original and distinct from PCSX ReARMed.

#ifndef LOG_EVENT_DEBUG
#define LOG_EVENT_DEBUG(...) log_component("event_scheduler", LOG_LEVEL_DEBUG, __VA_ARGS__)
#endif

// --- Event Handler Table ---
// Forward declarations for event handler functions (to be implemented elsewhere)
typedef void (*EventQueueHandler)(struct Interconnect*);

// Event handler stubs for all timer events
static void evq_handle_vblank(struct Interconnect* sys); // VBlank event
static void evq_handle_timer0(struct Interconnect* sys); // Timer0 event
static void evq_handle_timer1(struct Interconnect* sys); // Timer1 event
static void evq_handle_timer2(struct Interconnect* sys); // Timer2 event
static void evq_handle_dma_gpu(struct Interconnect* sys);   // GPU DMA event
static void evq_handle_dma_cdrom(struct Interconnect* sys); // CDROM DMA event
static void evq_handle_cdrom(struct Interconnect* sys); // CDROM event
// ... add more as needed for other event types

// Table of event handlers, indexed by EventQueueType
typedef EventQueueHandler EventHandlerTable[EVQ_EVENT_COUNT];
static EventHandlerTable evq_handlers = {
    evq_handle_vblank,   // EVQ_VBLANK
    evq_handle_timer0,   // EVQ_TIMER0
    evq_handle_timer1,   // EVQ_TIMER1
    evq_handle_timer2,   // EVQ_TIMER2
    evq_handle_dma_gpu,  // EVQ_DMA_GPU
    evq_handle_dma_cdrom,// EVQ_DMA_CDROM
    NULL,                // EVQ_DMA_SPU
    NULL,                // EVQ_DMA_OTC
    NULL,                // EVQ_SIO
    evq_handle_cdrom,    // EVQ_CDROM (non-DMA)
    NULL,                // EVQ_GPU (non-DMA)
    NULL,                // EVQ_MDEC
    NULL                 // EVQ_SPU
};

// --- Event Scheduling ---
/**
 * @brief Schedule an event to occur after a given number of cycles from now.
 */
void eventq_schedule(struct Interconnect* sys, EventQueueType event, uint32_t cycles_from_now) {
    sys->evq_pending |= (1u << event);
    sys->evq_target_cycle[event] = sys->cpu_cycle_counter + cycles_from_now;
    
    // Update the next event cycle if this event is sooner
    if (sys->evq_next_cycle > sys->evq_target_cycle[event] || sys->evq_next_cycle <= sys->cpu_cycle_counter) {
        sys->evq_next_cycle = sys->evq_target_cycle[event];
    }
}

// --- Event Dispatch ---
/**
 * @brief Check and dispatch any events that are due at the current cycle.
 */
void eventq_dispatch_due(struct Interconnect* sys) {
    uint32_t now = sys->cpu_cycle_counter;
    
    // Keep dispatching as long as any event is due
    while (1) {
        uint32_t pending = sys->evq_pending;
        int any_fired = 0;
        
        for (EventQueueType event = 0; event < EVQ_EVENT_COUNT; ++event) {
            if ((pending & (1u << event)) && (int32_t)(now - sys->evq_target_cycle[event]) >= 0) {
                sys->evq_pending &= ~(1u << event);
                if (evq_handlers[event]) {
                    evq_handlers[event](sys);
                }
                any_fired = 1;
            }
        }
        if (!any_fired) break;
        // After firing, update now in case event handler advanced cycles
        now = sys->cpu_cycle_counter;
    }
    // Recalculate the next event cycle
    uint32_t soonest = UINT32_MAX;
    for (EventQueueType event = 0; event < EVQ_EVENT_COUNT; ++event) {
        if (sys->evq_pending & (1u << event)) {
            uint32_t target = sys->evq_target_cycle[event];
            if (target > now && target < soonest) {
                soonest = target;
            }
        }
    }
    sys->evq_next_cycle = soonest;
}

// --- Next Event Cycle Query ---
/**
 * @brief Get the cycle of the next scheduled event (for main loop efficiency).
 */
uint32_t eventq_next_cycle(const struct Interconnect* sys) {
    return sys->evq_next_cycle;
}

uint32_t eventq_cycles_until_next(const struct Interconnect* sys) {
    const uint32_t now = sys->cpu_cycle_counter;
    const uint32_t next = sys->evq_next_cycle;

    if (next == UINT32_MAX) {
        // No pending scheduled events: execute at least one cycle.
        return 1;
    }

    if ((int32_t)(now - next) >= 0) {
        return 0;
    }

    return next - now;
}

// --- Example Event Handlers (Stubs) ---
// These should be implemented to set IRQs, reschedule themselves, etc.

#define VBLANK_CYCLES 564480 // NTSC: 33868800 / 60
#define TIMER0_CYCLES 1000   // Placeholder, tune as needed

static void evq_handle_vblank(struct Interconnect* sys) {
    static uint32_t vblank_count = 0;
    vblank_count++;

    // Advance CRTC scanline counter and update STAT[31]
    gpu_crtc_tick(&sys->gpu, VBLANK_CYCLES);

    // Log VBlank with SYSTEM category to bypass timer rate limiting
    if (vblank_count <= 5 || vblank_count % 60 == 0) {
        LOG_DEBUG("[VBlank] Frame #%u at cycle %u, I_STAT=0x%04x, I_MASK=0x%04x",
                 vblank_count, sys->cpu_cycle_counter,
                 sys->irq_status, sys->irq_mask);
    }
    
    // Always reschedule the next VBlank
    eventq_schedule(sys, EVQ_VBLANK, VBLANK_CYCLES);
    
    // Verify it was scheduled
    if (!(sys->evq_pending & (1u << EVQ_VBLANK))) {
        LOG_ERROR("[VBlank] CRITICAL: VBlank not rescheduled! pending=0x%X", sys->evq_pending);
    }
    
    // Trigger VBlank interrupt (IRQ0 per PSX-SPX)
    interconnect_request_irq(sys, 0, "VBlank");
    
    timers_on_vblank(&sys->timers_state);

    // --- Diagnostic VRAM + GPU register dump at VBlank ---
    // Dump a few frames early and every 60th frame to help diagnose display issues.
    if (vblank_count <= 3 || (vblank_count % 60) == 0) {
        Gpu* gpu = &sys->gpu;
        // Log key GPU register state
        LOG_GPU_INFO("[VBlank Dump] Frame=%u page_base=(%u,%u) tex_depth=%d draw_display=%d display_start=(%u,%u) disp_hint=(%u,%u) draw_area=(%u,%u,%u,%u)",
                     vblank_count,
                     gpu->page_base_x, gpu->page_base_y, (int)gpu->texture_depth,
                     (int)gpu->draw_to_display,
                     gpu->display_vram_x_start, gpu->display_vram_y_start,
                     gpu->display_width_hint, gpu->display_height_hint,
                     gpu->drawing_area_left, gpu->drawing_area_top, gpu->drawing_area_right, gpu->drawing_area_bottom);

        // Determine dump region from CRTC-derived display area (updated by gpu_update_display_mapping)
        uint32_t dump_x = gpu->crtc.display_vram_x;
        uint32_t dump_y = gpu->crtc.display_vram_y;
        uint32_t dump_w = gpu->crtc.display_width  ? gpu->crtc.display_width  : 320;
        uint32_t dump_h = gpu->crtc.display_height ? gpu->crtc.display_height : 240;
        // Clamp to VRAM dimensions
        if (dump_x + dump_w > VRAM_WIDTH) dump_w = VRAM_WIDTH - dump_x;
        if (dump_y + dump_h > VRAM_HEIGHT) dump_h = VRAM_HEIGHT - dump_y;

        // Create filename and write PPM (binary) in the current working directory
        char fname[256];
        snprintf(fname, sizeof(fname), "vram_vblank_%05u.ppm", vblank_count);
        FILE* f = fopen(fname, "wb");
        if (f) {
            // PPM header
            fprintf(f, "P6\n%u %u\n255\n", dump_w, dump_h);
            // For each pixel convert 15-bit BGR -> 24-bit RGB
            for (uint32_t yy = 0; yy < dump_h; ++yy) {
                for (uint32_t xx = 0; xx < dump_w; ++xx) {
                    uint32_t vx = dump_x + xx;
                    uint32_t vy = dump_y + yy;
                    uint32_t off = (vy * VRAM_WIDTH + vx) * 2; // bytes
                    uint8_t lo = gpu->vram.data[off];
                    uint8_t hi = gpu->vram.data[off + 1];
                    uint16_t raw = (uint16_t)(lo | (hi << 8));
                    // PSX VRAM is 15-bit BGR (bits: 0-4 B, 5-9 G, 10-14 R)
                    uint8_t b5 = raw & 0x1F;
                    uint8_t g5 = (raw >> 5) & 0x1F;
                    uint8_t r5 = (raw >> 10) & 0x1F;
                    // Expand 5-bit to 8-bit
                    uint8_t r8 = (r5 << 3) | (r5 >> 2);
                    uint8_t g8 = (g5 << 3) | (g5 >> 2);
                    uint8_t b8 = (b5 << 3) | (b5 >> 2);
                    fputc(r8, f);
                    fputc(g8, f);
                    fputc(b8, f);
                }
            }
            fclose(f);
            LOG_GPU_INFO("[VBlank Dump] Wrote %s (%ux%u) from VRAM (%u,%u)", fname, dump_w, dump_h, dump_x, dump_y);
        } else {
            LOG_GPU_INFO("[VBlank Dump] Failed to open %s for writing", fname);
        }
        // Force the renderer to blit the display-area from VRAM to the screen
        if (gpu->renderer.initialized && gpu->crtc.display_width > 0 && gpu->crtc.display_height > 0) {
            LOG_GPU_INFO("[VBlank Dump] Forcing renderer blit of display-area (%u,%u %ux%u)", dump_x, dump_y, dump_w, dump_h);
            renderer_blit_vram(&gpu->renderer, (uint16_t)gpu->crtc.display_vram_x, (uint16_t)gpu->crtc.display_vram_y, (uint16_t)dump_w, (uint16_t)dump_h);
        }
        // Additional diagnostic: sample specific VRAM locations useful for BIOS menu
        // Sample display origin area
        for (int ry = 0; ry < 4; ++ry) {
            char line[256]; int pos = 0;
            pos += snprintf(line + pos, sizeof(line) - pos, "VRAM-SAMP DisplayRow %d:", ry + (int)gpu->display_vram_y_start);
            for (int rx = 0; rx < 16; ++rx) {
                uint32_t vx = (gpu->display_vram_x_start + rx) & 0x3FF;
                uint32_t vy = (gpu->display_vram_y_start + ry) & 0x1FF;
                uint32_t off = (vy * VRAM_WIDTH + vx) * 2;
                uint16_t val = (uint16_t)(gpu->vram.data[off] | (gpu->vram.data[off + 1] << 8));
                pos += snprintf(line + pos, sizeof(line) - pos, " %04x", val);
                if (pos > (int)sizeof(line) - 8) break;
            }
            LOG_GPU_INFO("%s", line);
        }
        // Sample menu/font CLUT area around y=480 and a few texture pages
        for (int sample = 0; sample < 3; ++sample) {
            int base_x = sample * 64; int base_y = 480;
            char line2[256]; int p = 0;
            p += snprintf(line2 + p, sizeof(line2) - p, "VRAM-SAMP Page %d (%d,%d):", sample, base_x, base_y);
            for (int i = 0; i < 8; ++i) {
                uint32_t vx = (base_x + i) & 0x3FF;
                uint32_t vy = base_y & 0x1FF;
                uint32_t off = (vy * VRAM_WIDTH + vx) * 2;
                uint16_t val = (uint16_t)(gpu->vram.data[off] | (gpu->vram.data[off + 1] << 8));
                p += snprintf(line2 + p, sizeof(line2) - p, " %04x", val);
                if (p > (int)sizeof(line2) - 8) break;
            }
            LOG_GPU_INFO("%s", line2);
        }
    }
}

static void evq_handle_timer0(struct Interconnect* sys) {
    static int timer0_dispatch_count = 0;
    if (timer0_dispatch_count < 5) {
        LOG_EVENT_DEBUG("[EventQ] DISPATCH Timer0 event handler called (count=%d)", ++timer0_dispatch_count);
    }
    timer0_event_handler(sys);
}
static void evq_handle_timer1(struct Interconnect* sys) { timer1_event_handler(sys); }
static void evq_handle_timer2(struct Interconnect* sys) { timer2_event_handler(sys); }

static void evq_handle_dma_gpu(struct Interconnect* sys) {
    static uint32_t gpu_dma_count = 0;
    gpu_dma_count++;
    
    Dma* dma = &sys->dma;
    DmaChannel* ch = &dma->channels[2]; // Channel 2: GPU
    if (!ch->enable) {
        // Only log first few times
        if (gpu_dma_count <= 3) {
            LOG_EVENT_DEBUG("[DMA] GPU DMA event but channel not enabled");
        }
        return;
    }
    extern void perform_gpu_dma_transfer(struct Interconnect* sys, DmaChannel* ch);
    perform_gpu_dma_transfer(sys, ch);
    dma_channel_done(ch);
    // --- DMA IRQ logic (PCSX ReARMed style) ---
    // Set channel IRQ flag for channel 2
    dma->channel_irq_flags |= (1 << 2);
    LOG_EVENT_DEBUG("[DMA] Channel 2 IRQ flag set: channel_irq_flags=0x%02x", dma->channel_irq_flags);
    // If channel IRQ enable and master IRQ enable are set, set master IRQ flag
    if ((dma->channel_irq_enable & (1 << 2)) && dma->master_irq_enable) {
        dma->master_irq_flag = true;
        LOG_EVENT_DEBUG("[DMA] Master IRQ flag set: master_irq_flag=%d", dma->master_irq_flag);
    }
    // If master IRQ flag is set, set IRQ3 (DMA IRQ) in irq_status
    if (dma->master_irq_flag) {
        LOG_EVENT_DEBUG("[DMA] GPU DMA IRQ3 triggered (master IRQ flag set)");
        interconnect_request_irq(sys, 3, "DMA_GPU"); // Use edge-triggered API
    }
    LOG_EVENT_DEBUG("[DMA] Handler exit: channel_irq_enable=0x%02x, master_irq_enable=%d, master_irq_flag=%d, irq_status=0x%04x", dma->channel_irq_enable, dma->master_irq_enable, dma->master_irq_flag, sys->irq_status);
}

static void evq_handle_dma_cdrom(struct Interconnect* sys) {
    LOG_EVENT_DEBUG("[DMA] CDROM DMA event handler called (stub)");
    (void)sys;
    // TODO: Complete the CDROM DMA transfer and set IRQ when ready
}

static void evq_handle_cdrom(struct Interconnect* sys) {
    // CDROM events are now handled via interconnect_check_cdrom_events()
    // This handler is kept for legacy event system compatibility
    (void)sys;
} 