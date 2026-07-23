/**
 * gpu.c
 * PlayStation GPU state management: init/reset, GP1 command handlers,
 * GPUSTAT register, GPUREAD data port, and display mapping updates.
 *
 * GP0 command handling is in gpu_commands.c (modular split).
 */

#include "gpu.h"
#include "renderer.h"
#include "vram.h"
#include "log.h"
#include "interconnect.h"
#include "timers.h"
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal helpers shared with gpu_commands.c  (implementations are in
// gpu_commands.c; we just use them via the non-static declarations in gpu.h)
// ---------------------------------------------------------------------------
// gpu_clear_cmd_buf()  — in gpu_commands.c
// gpu_push_cmd_word()  — in gpu_commands.c

// ---------------------------------------------------------------------------
// Helper: update renderer screen scale and drawing offset
// Uses the CRTC-derived display area (set by GP1(05/06/07/08)).
// ---------------------------------------------------------------------------
void gpu_update_display_mapping(Gpu* gpu) {
    // Determine display dimensions from GP1 register state.
    // Width: use display_width_hint computed by gp1_display_mode from resolution index.
    uint16_t disp_w = gpu->display_width_hint ? gpu->display_width_hint : 320;

    // Height: 480i uses full 480-line single buffer; 240p uses GP1(07) line range.
    uint16_t disp_h = 240;
    if (gpu->interlaced && gpu->vres == Y480Lines) {
        disp_h = 480;
    } else if (gpu->display_line_end > gpu->display_line_start) {
        uint16_t h = gpu->display_line_end - gpu->display_line_start;
        if (h > 0 && h <= VRAM_HEIGHT)
            disp_h = h;
    }

    // Store computed display area into CRTC state for the VBlank blit path.
    gpu->crtc.display_vram_x = gpu->display_vram_x_start;
    gpu->crtc.display_vram_y = gpu->display_vram_y_start;
    gpu->crtc.display_width  = disp_w;
    gpu->crtc.display_height = disp_h;

    // Keep full VRAM dimensions so polygon coordinates (0..1023 × 0..511) are
    // visible — full VRAM view, VRAM objects remain visible as the user prefers.
    renderer_set_screen_scale(&gpu->renderer, VRAM_WIDTH, VRAM_HEIGHT);
    renderer_set_draw_offset(&gpu->renderer, gpu->drawing_x_offset, gpu->drawing_y_offset);
    renderer_set_display_region(&gpu->renderer,
        gpu->crtc.display_vram_x, gpu->crtc.display_vram_y,
        gpu->crtc.display_width,  gpu->crtc.display_height);
}

// ---------------------------------------------------------------------------
// CRTC tick — advance scanline counter and STAT[31] once per VBlank period.
// Call from evq_handle_vblank() with the number of CPU cycles since last VBlank.
// ---------------------------------------------------------------------------
#define CPU_CYCLES_PER_SCANLINE 2146u

void gpu_crtc_tick(Gpu* gpu, uint32_t cpu_cycles_elapsed) {
    // Update vertical total from current video mode
    gpu->crtc.vertical_total = (gpu->vmode == Pal) ? 314 : 263;

    // Advance scanline counter (at least 1 scanline per call)
    uint32_t scanlines = cpu_cycles_elapsed / CPU_CYCLES_PER_SCANLINE;
    if (scanlines == 0) scanlines = 1;
    gpu->crtc.current_scanline = (uint16_t)(
        ((uint32_t)gpu->crtc.current_scanline + scanlines) % gpu->crtc.vertical_total);

    // Update in_vblank from vertical display range (set by GP1(07))
    if (gpu->display_line_start < gpu->display_line_end) {
        gpu->crtc.in_vblank = (gpu->crtc.current_scanline < gpu->display_line_start ||
                               gpu->crtc.current_scanline >= gpu->display_line_end);
    } else {
        gpu->crtc.in_vblank = false;
    }

    // Toggle STAT[31] (odd/even line) each VBlank period
    gpu->crtc.active_line_lsb ^= 1u;

    // In interlaced 480i mode: also alternate the field tag
    if (gpu->interlaced && gpu->vres == Y480Lines) {
        gpu->crtc.interlaced_field ^= 1u;
        gpu->field = gpu->crtc.interlaced_field ? Top : Bottom;
    }

    LOG_GPU_DEBUG("[GPU] tick: scanline=%u vblank=%d lsb=%u",
                  gpu->crtc.current_scanline, (int)gpu->crtc.in_vblank,
                  gpu->crtc.active_line_lsb);

    // Timer1's external gate is VBlank (PSX-SPX; DuckStation's HBLANK_TIMER_INDEX
    // despite the name is gated by vblank, not hblank). Timer0's gate is hblank,
    // which this CRTC model doesn't track at per-scanline granularity yet, so it
    // isn't wired up here.
    if (gpu->inter)
        timers_set_gate(&gpu->inter->timers_state, 1, gpu->crtc.in_vblank);
}

// ---------------------------------------------------------------------------
// GP1 handlers
// ---------------------------------------------------------------------------

static void gp1_reset(Gpu* gpu, uint32_t value) {
    (void)value;
    LOG_GPU_DEBUG("[GPU] GPU: Soft Reset (GP1 0x00)");
    gpu_soft_reset(gpu);
}

static void gp1_reset_command_buffer(Gpu* gpu, uint32_t value) {
    (void)value;
    LOG_GPU_DEBUG("[GPU] GPU: Reset Command Buffer (GP1 0x01)");
    gpu_clear_cmd_buf(gpu);
    gpu->gp0_words_remaining = 0;
    gpu->gp0_mode = GP0_MODE_COMMAND;
    gpu->gp0_current_opcode = 0xFF;
    gpu->gp0_command_method = NULL;
    gpu->gp0_fifo_head = 0; gpu->gp0_fifo_tail = 0; gpu->gp0_fifo_count = 0;
}

static void gp1_acknowledge_irq(Gpu* gpu, uint32_t value) {
    (void)value;
    gpu->interrupt = false;
    /* Deassert the GPU IRQ line so the edge detector can latch the next
     * GP0(0x1F) — pairs with the raise in gp0_interrupt_request. Matches
     * DuckStation's GP1(02) handler (SetLineState(IRQ::GPU, false)). */
    if (gpu->inter)
        interconnect_set_irq_line(gpu->inter, IRQ_GPU, false);
    LOG_GPU_DEBUG("[GPU] GPU: Acknowledge IRQ (GP1 0x02)");
}

static void gp1_display_enable(Gpu* gpu, uint32_t value) {
    gpu->display_disabled = (value & 1) != 0;
    LOG_GPU_DEBUG("[GPU] GPU: Display %s (GP1 0x03)", gpu->display_disabled ? "Disabled" : "Enabled");
}

static void gp1_dma_direction(Gpu* gpu, uint32_t value) {
    switch (value & 3) {
        case 0: gpu->dma_setting = GPU_DMA_Off;       break;
        case 1: gpu->dma_setting = GPU_DMA_Fifo;      break;
        case 2: gpu->dma_setting = GPU_DMA_CpuToGp0;  break;
        case 3: gpu->dma_setting = GPU_DMA_VRamToCpu; break;
    }
    LOG_GPU_DEBUG("[GPU] GPU: DMA Direction = %d (GP1 0x04)", gpu->dma_setting);
}

static void gp1_display_vram_start(Gpu* gpu, uint32_t value) {
    gpu->display_vram_x_start = (uint16_t)(value & 0x3FE);
    gpu->display_vram_y_start = (uint16_t)((value >> 10) & 0x1FF);
    LOG_GPU_DEBUG("[GPU] Display address start <- 0x%08x", value & 0x000FFFFF);
    gpu_update_display_mapping(gpu);
}

static void gp1_display_horizontal_range(Gpu* gpu, uint32_t value) {
    gpu->display_horiz_start = (uint16_t)(value & 0xFFF);
    gpu->display_horiz_end   = (uint16_t)((value >> 12) & 0xFFF);
    LOG_GPU_DEBUG("[GPU] Horizontal display range <- 0x%08x", value & 0x00FFFFFF);
    gpu_update_display_mapping(gpu);
}

static void gp1_display_vertical_range(Gpu* gpu, uint32_t value) {
    gpu->display_line_start = (uint16_t)(value & 0x3FF);
    gpu->display_line_end   = (uint16_t)((value >> 10) & 0x3FF);
    LOG_GPU_DEBUG("[GPU] Vertical display range <- 0x%08x", value & 0x000FFFFF);
    gpu_update_display_mapping(gpu);
}

static void gp1_display_mode(Gpu* gpu, uint32_t value) {
    gpu->hres_raw.hr1   = (uint8_t)(value & 3);
    gpu->hres_raw.hr2   = (uint8_t)((value >> 6) & 1);
    gpu->vres           = ((value >> 2) & 1) ? Y480Lines : Y240Lines;
    gpu->vmode          = ((value >> 3) & 1) ? Pal : Ntsc;
    gpu->display_depth  = ((value >> 4) & 1) ? D24Bits : D15Bits;
    gpu->interlaced     = ((value >> 5) & 1) != 0;
    if ((value >> 7) & 1)
        LOG_GPU_WARN("[GPU] GP1(0x08): Reverseflag bit set (unsupported)");

    uint16_t width = 256;
    switch (gpu->hres_raw.hr1 | (gpu->hres_raw.hr2 << 2)) {
        case 0: width = 256; break;
        case 1: width = 320; break;
        case 2: width = 512; break;
        case 3: width = 640; break;
        case 4: width = 368; break;
        default: width = 256; break;
    }
    uint16_t height = (gpu->interlaced || gpu->vres == Y480Lines) ? 480 : 240;
    gpu->display_width_hint  = width;
    gpu->display_height_hint = height;
    gpu_update_display_mapping(gpu);
    LOG_GPU_DEBUG("[GPU] Set display mode <- 0x%08x", value & 0xFF);
}

// ---------------------------------------------------------------------------
// GP1(0x10–0x1F): GetGPUInfo — stores result into gpu_info_latch for GPUREAD
// ---------------------------------------------------------------------------
static void gp1_get_gpu_info(Gpu* gpu, uint32_t value) {
    uint32_t subfn = value & 0xF;
    uint32_t result = 0;
    switch (subfn) {
        case 0x02:
            // Texture page info (same format as GP0(E1))
            result  = (uint32_t)gpu->page_base_x;
            result |= (uint32_t)gpu->page_base_y << 4;
            result |= (uint32_t)gpu->semi_transparency << 5;
            result |= (uint32_t)gpu->texture_depth << 7;
            result |= (uint32_t)gpu->dithering << 9;
            result |= (uint32_t)gpu->draw_to_display << 10;
            result |= (uint32_t)gpu->texture_disable << 11;
            result |= (uint32_t)gpu->rectangle_texture_x_flip << 12;
            result |= (uint32_t)gpu->rectangle_texture_y_flip << 13;
            break;
        case 0x03:
            // Texture window (same format as GP0(E2))
            result  = (uint32_t)gpu->texture_window_x_mask;
            result |= (uint32_t)gpu->texture_window_y_mask   << 5;
            result |= (uint32_t)gpu->texture_window_x_offset << 10;
            result |= (uint32_t)gpu->texture_window_y_offset << 15;
            break;
        case 0x04:
            // Drawing area top-left (same format as GP0(E3))
            result  = (uint32_t)gpu->drawing_area_left;
            result |= (uint32_t)gpu->drawing_area_top << 10;
            break;
        case 0x05:
            // Drawing area bottom-right (same format as GP0(E4))
            result  = (uint32_t)gpu->drawing_area_right;
            result |= (uint32_t)gpu->drawing_area_bottom << 10;
            break;
        case 0x06:
            // Drawing offset (same format as GP0(E5))
            result  = (uint32_t)(gpu->drawing_x_offset & 0x7FF);
            result |= (uint32_t)((gpu->drawing_y_offset & 0x7FF) << 11);
            break;
        case 0x07:
            result = 0x00000002; // GPU type: version 2
            break;
        default:
            result = 0;
            break;
    }
    gpu->gpu_info_latch = result;
    LOG_GPU_DEBUG("[GPU] GP1(0x10): GetGPUInfo subfn=%u -> 0x%08x", subfn, result);
}

// ---------------------------------------------------------------------------
// Public GP1 entry point
// ---------------------------------------------------------------------------
void gpu_gp1(Gpu* gpu, uint32_t command) {
    uint32_t opcode = (command >> 24) & 0xFF;
    switch (opcode) {
        case 0x00: gp1_reset(gpu, command); break;
        case 0x01: gp1_reset_command_buffer(gpu, command); break;
        case 0x02: gp1_acknowledge_irq(gpu, command); break;
        case 0x03: gp1_display_enable(gpu, command); break;
        case 0x04: gp1_dma_direction(gpu, command); break;
        case 0x05: gp1_display_vram_start(gpu, command); break;
        case 0x06: gp1_display_horizontal_range(gpu, command); break;
        case 0x07: gp1_display_vertical_range(gpu, command); break;
        case 0x08: gp1_display_mode(gpu, command); break;
        case 0x09:
            // GP1(0x09): Allow texture disable (stored in texture_disable controlled via GP0(E1))
            LOG_GPU_DEBUG("[GPU] GP1(0x09): Allow texture disable (no-op in LLE)");
            break;
        case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15:
        case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B:
        case 0x1C: case 0x1D: case 0x1E: case 0x1F:
            gp1_get_gpu_info(gpu, command);
            break;
        default:
            LOG_GPU_WARN("[GPU] Unhandled GP1 opcode 0x%02x (cmd 0x%08x) @ 0x%08x", opcode, command);
            break;
    }
}

// ---------------------------------------------------------------------------
// GPUSTAT register read
// ---------------------------------------------------------------------------
uint32_t gpu_read_status(Gpu* gpu) {
    uint32_t r = 0;
    r |= (uint32_t)gpu->page_base_x << 0;
    r |= (uint32_t)gpu->page_base_y << 4;
    r |= (uint32_t)gpu->semi_transparency << 5;
    r |= (uint32_t)gpu->texture_depth << 7;
    r |= (uint32_t)gpu->dithering << 9;
    r |= (uint32_t)gpu->draw_to_display << 10;
    r |= (uint32_t)gpu->force_set_mask_bit << 11;
    r |= (uint32_t)gpu->preserve_masked_pixels << 12;
    r |= (uint32_t)gpu->field << 13;
    r |= (uint32_t)gpu->texture_disable << 15;
    uint32_t hres = ((uint32_t)gpu->hres_raw.hr2 << 2) | (uint32_t)gpu->hres_raw.hr1;
    r |= ((hres >> 0) & 1) << 16;
    r |= ((hres >> 1) & 1) << 17;
    r |= ((hres >> 2) & 1) << 18;
    r |= ((uint32_t)(gpu->vres == Y480Lines ? 1 : 0)) << 19;
    r |= ((uint32_t)gpu->vmode) << 20;
    r |= ((uint32_t)gpu->display_depth) << 21;
    r |= ((uint32_t)gpu->interlaced) << 22;
    r |= ((uint32_t)gpu->display_disabled) << 23;
    // Bit 24: GPU IRQ flag — use actual state (cleared by GP1(02))
    r |= ((uint32_t)gpu->interrupt) << 24;
    // DMA request (bit 25)
    if (gpu->dma_setting != GPU_DMA_Off) r |= (1 << 25);
    // GPU-1 FIX: Correct ready bits and DMA request per GPUSTAT spec.

    // Bit 26: Ready to receive Cmd Word — always 1 (synchronous emulator, never busy).
    bool ready_to_recv_cmd = true;
    if (ready_to_recv_cmd) r |= (1u << 26);

    // Bit 27: Ready to send VRAM to CPU — only when IMAGE_STORE transfer is pending.
    bool vram_send_ready = (gpu->gp0_mode == GP0_MODE_IMAGE_STORE && gpu->gp0_words_remaining > 0);
    if (vram_send_ready) r |= (1u << 27);

    // Bit 28: Ready to receive DMA Block — always 1 (synchronous emulator).
    bool dma_recv_ready = true;
    if (dma_recv_ready) r |= (1u << 28);

    // Bit 25: DMA / Data Request — meaning depends on GP1(04h) DMA Direction:
    //   Off       → Always 0
    //   FIFO(1)   → FIFO state: 0=Full, 1=Not Full  (same as bit28)
    //   CpuToGP0  → Same as GPUSTAT.28
    //   VRamToCPU → Same as GPUSTAT.27
    {
        bool dma_request;
        switch (gpu->dma_setting) {
            case GPU_DMA_Off:       dma_request = false;           break;
            case GPU_DMA_Fifo:      dma_request = dma_recv_ready;  break;
            case GPU_DMA_CpuToGp0:  dma_request = dma_recv_ready;  break;
            case GPU_DMA_VRamToCpu: dma_request = vram_send_ready; break;
            default:                dma_request = false;           break;
        }
        if (dma_request) r |= (1u << 25);
    }

    // DMA direction (bits 30:29)
    r |= ((uint32_t)gpu->dma_setting) << 29;
    // Bit 31: odd/even line — from CRTC scanline counter
    r |= ((uint32_t)gpu->crtc.active_line_lsb) << 31;
    return r;
}

// ---------------------------------------------------------------------------
// GPUREAD data port
// ---------------------------------------------------------------------------
uint32_t gpu_read_data(Gpu* gpu) {
    // IMAGE_STORE: pack two 16-bit VRAM pixels per 32-bit word
    if (gpu->gp0_mode == GP0_MODE_IMAGE_STORE) {
        if (gpu->gp0_words_remaining == 0) {
            LOG_GPU_WARN("[GPU] GPUREAD: No words remaining in Image Store transfer");
            return 0xFFFFFFFF;
        }
        uint32_t idx = gpu->vram_load_count;
        uint16_t pixel1 = 0, pixel2 = 0;

        uint16_t x1 = (gpu->vram_load_x + (uint16_t)(idx % gpu->vram_load_w)) & 0x3FF;
        uint16_t y1 = (gpu->vram_load_y + (uint16_t)(idx / gpu->vram_load_w)) & 0x1FF;
        pixel1 = vram_load16(&gpu->vram,
            (uint32_t)y1 * VRAM_WIDTH * VRAM_BPP + (uint32_t)x1 * VRAM_BPP);
        gpu->vram_load_count++;

        uint32_t total = (uint32_t)gpu->vram_load_w * gpu->vram_load_h;
        if (gpu->vram_load_count < total) {
            idx = gpu->vram_load_count;
            uint16_t x2 = (gpu->vram_load_x + (uint16_t)(idx % gpu->vram_load_w)) & 0x3FF;
            uint16_t y2 = (gpu->vram_load_y + (uint16_t)(idx / gpu->vram_load_w)) & 0x1FF;
            pixel2 = vram_load16(&gpu->vram,
                (uint32_t)y2 * VRAM_WIDTH * VRAM_BPP + (uint32_t)x2 * VRAM_BPP);
            gpu->vram_load_count++;
        }

        gpu->gp0_words_remaining--;
        if (gpu->gp0_words_remaining == 0) {
            gpu->gp0_mode = GP0_MODE_COMMAND;
            LOG_GPU_INFO("[GPU] GP0(0xC0): VRAM→CPU transfer COMPLETE");
        }
        return (uint32_t)pixel1 | ((uint32_t)pixel2 << 16);
    }

    // GP1(0x10) info latch
    if (gpu->gpu_info_latch != 0) {
        uint32_t v = gpu->gpu_info_latch;
        gpu->gpu_info_latch = 0;
        return v;
    }

    // Fallback
    static uint32_t dummy = 0;
    return dummy++;
}

// ---------------------------------------------------------------------------
// VBlank hook
// ---------------------------------------------------------------------------
void gpu_trigger_vblank_irq(Gpu* gpu) {
    (void)gpu;
    // VBlank IRQ0 is handled by the event scheduler — no direct action here.
    LOG_GPU_DEBUG("[GPU] VBlank event (IRQ0 via event scheduler)");
}

// ---------------------------------------------------------------------------
// Full initialization (power-on / hard reset — clears VRAM)
// ---------------------------------------------------------------------------
static void gpu_reset_state(Gpu* gpu) {
    gpu->interrupt = false;
    gpu->page_base_x = 0; gpu->page_base_y = 0;
    gpu->semi_transparency = 0;
    gpu->texture_depth = T4Bit;
    gpu->texture_window_x_mask = 0; gpu->texture_window_y_mask = 0;
    gpu->texture_window_x_offset = 0; gpu->texture_window_y_offset = 0;
    gpu->dithering = false; gpu->draw_to_display = false;
    gpu->texture_disable = false;
    gpu->rectangle_texture_x_flip = false;
    gpu->rectangle_texture_y_flip = false;
    // Initialize drawing area to full VRAM bounds (unrestricted)
    gpu->drawing_area_left = 0; gpu->drawing_area_top = 0;
    gpu->drawing_area_right = 1023; gpu->drawing_area_bottom = 511;
    gpu->drawing_x_offset = 0; gpu->drawing_y_offset = 0;
    gpu->force_set_mask_bit = false;
    gpu->preserve_masked_pixels = false;
    gpu->dma_setting = GPU_DMA_Off;
    gpu->display_disabled = true;
    gpu->display_vram_x_start = 0; gpu->display_vram_y_start = 0;
    gpu->hres_raw.hr1 = 0; gpu->hres_raw.hr2 = 0;
    gpu->vres = Y240Lines; gpu->vmode = Ntsc;
    gpu->interlaced = true; gpu->display_depth = D15Bits;
    gpu->display_horiz_start = 0x200; gpu->display_horiz_end = 0xC00;
    gpu->display_line_start = 0x10; gpu->display_line_end = 0x100;
    gpu->field = Top;
    gpu->display_width_hint = 320; gpu->display_height_hint = 240;
    gpu->vram_dirty = false;
    gpu->gpu_info_latch = 0;
    gpu->polyline_count = 0; gpu->polyline_shaded = false; gpu->polyline_semi_trans = false;
    gpu_clear_cmd_buf(gpu);
    gpu->gp0_words_remaining = 0;
    gpu->gp0_mode = GP0_MODE_COMMAND;
    gpu->gp0_command_method = NULL;
    gpu->gp0_current_opcode = 0xFF;
    gpu->gp0_fifo_head = 0; gpu->gp0_fifo_tail = 0; gpu->gp0_fifo_count = 0;
    gpu->vram_load_x = 0; gpu->vram_load_y = 0;
    gpu->vram_load_w = 0; gpu->vram_load_h = 0;
    gpu->vram_load_count = 0;
    // Reset CRTC state
    gpu->crtc.current_scanline  = 0;
    gpu->crtc.vertical_total    = 263; // NTSC default
    gpu->crtc.in_vblank         = false;
    gpu->crtc.active_line_lsb   = 0;
    gpu->crtc.interlaced_field  = 0;
    gpu->crtc.display_vram_x    = 0;
    gpu->crtc.display_vram_y    = 0;
    gpu->crtc.display_width     = 320;
    gpu->crtc.display_height    = 240;
    renderer_set_texture_window(&gpu->renderer, 0, 0, 0, 0);
    // Set initial drawing area to full VRAM bounds
    renderer_set_drawing_area(&gpu->renderer,
        gpu->drawing_area_left, gpu->drawing_area_top,
        gpu->drawing_area_right, gpu->drawing_area_bottom);
    // Set default display region (320x240 at VRAM origin) for correct blit before GP1(05-08)
    renderer_set_display_region(&gpu->renderer, 0, 0, 320, 240);
}

void gpu_init_full(Gpu* gpu, Interconnect* inter) {
    LOG_GPU_DEBUG("[GPU] GPU full initialization (with VRAM)");
    vram_init(&gpu->vram);
    gpu->inter = inter;
    gpu_reset_state(gpu);
    LOG_GPU_DEBUG("[GPU] GPU Initialized.");
}

void gpu_soft_reset(Gpu* gpu) {
    LOG_GPU_DEBUG("[GPU] GPU soft reset (VRAM preserved)");
    gpu_reset_state(gpu);
    LOG_GPU_DEBUG("[GPU] GPU Soft Reset complete.");
}

// gpu_init (legacy alias used by some callers)
void gpu_init(Gpu* gpu, Interconnect* inter) {
    gpu_init_full(gpu, inter);
}
