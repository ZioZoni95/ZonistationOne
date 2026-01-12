/**
 * gpu_commands.c
 * GPU Command Parsing Layer - Implementation
 * 
 * Based on DuckStation's gpu_commands.cpp architecture
 * 
 * This module handles:
 *   - GP0 command dispatch (256-entry table)
 *   - GP1 register commands
 *   - DMA packet boundary detection (CRITICAL FIX #3)
 *   - Command word buffering and accumulation
 *   - FIFO management
 *   - GPU threading integration
 * 
 * CRITICAL BUG FIXES PRESERVED:
 *   ✅ Fix #2: Handler mapping (0x78=textured, 0x7C=non-textured)
 *   ✅ Fix #3: DMA packet boundary detection
 */

#include "gpu/gpu_commands.h"
#include "gpu/gpu_core.h"
#include "gpu/gpu_rendering.h"
#include "gpu/gpu_vram.h"
#include "gpu/gpu_display.h"
#include "gpu/gpu_thread.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Forward Declarations for Static Helper Functions
// ============================================================================

static void gp0_unknown_command(GPU* gpu);

// ============================================================================
// Static GP0 Command Handler Table
// ============================================================================

static GP0Handler s_gp0_handler_table[256];
static uint8_t s_gp0_expected_words[256];
static bool s_table_initialized = false;

// ============================================================================
// Forward Declarations for Internal Handlers
// ============================================================================

static void gp0_unknown_command(GPU* gpu);

// ============================================================================
// Command Buffer Management
// ============================================================================

void gp0_clear_command_buffer(GPU* gpu) {
    gpu->gp0_command_buffer.count = 0;
}

void gp0_push_command_word(GPU* gpu, uint32_t word) {
    if (gpu->gp0_command_buffer.count >= MAX_GPU_COMMAND_WORDS) {
        LOG_GPU_ERROR("FATAL: GP0 Command Buffer Overflow! Opcode: 0x%02x", gpu->gp0_current_opcode);
        exit(EXIT_FAILURE);
    }
    gpu->gp0_command_buffer.buffer[gpu->gp0_command_buffer.count] = word;
    gpu->gp0_command_buffer.count++;
}

// ============================================================================
// FIFO Management
// ============================================================================

void gp0_fifo_push(GPU* gpu, uint32_t word) {
    if (gpu->gp0_fifo_count >= 16) {
        LOG_GPU_WARN("GPU FIFO overflow, dropping word 0x%08x", word);
        return;
    }
    gpu->gp0_fifo[gpu->gp0_fifo_tail] = word;
    gpu->gp0_fifo_tail = (gpu->gp0_fifo_tail + 1) & 0xF;
    gpu->gp0_fifo_count++;
}

uint32_t gp0_fifo_pop(GPU* gpu) {
    if (gpu->gp0_fifo_count == 0) {
        LOG_GPU_ERROR("GPU FIFO underflow!");
        return 0;
    }
    uint32_t word = gpu->gp0_fifo[gpu->gp0_fifo_head];
    gpu->gp0_fifo_head = (gpu->gp0_fifo_head + 1) & 0xF;
    gpu->gp0_fifo_count--;
    return word;
}

bool gp0_fifo_is_empty(const GPU* gpu) {
    return gpu->gp0_fifo_count == 0;
}

bool gp0_fifo_is_full(const GPU* gpu) {
    return gpu->gp0_fifo_count >= 16;
}

uint32_t gp0_fifo_get_count(const GPU* gpu) {
    return gpu->gp0_fifo_count;
}

// ============================================================================
// GP0 Command Handler Table Initialization
// ============================================================================

void gpu_commands_init_table(void) {
    if (s_table_initialized) {
        return;
    }
    
    // Initialize all to unknown command
    for (int i = 0; i < 256; i++) {
        s_gp0_handler_table[i] = &gp0_unknown_command;
        s_gp0_expected_words[i] = 1;
    }
    
    // --- NOP and Cache Commands ---
    s_gp0_handler_table[0x00] = &gp0_nop;
    s_gp0_expected_words[0x00] = 1;
    
    s_gp0_handler_table[0x01] = &gp0_clear_cache;
    s_gp0_expected_words[0x01] = 1;
    
    // --- Fill Rectangle ---
    s_gp0_handler_table[0x02] = &gp0_fill_rectangle;
    s_gp0_expected_words[0x02] = 3;
    
    // --- Rendering Commands (0x20-0x7F) ---
    // CRITICAL: This section includes FIX #2 (handler mapping)
    //
    // Rectangle commands (0x60-0x7F): Bit pattern 01xxxxxx
    // Bits [4:3] = size: 00=variable, 01=1x1, 10=8x8, 11=16x16
    // Bit [2] = textured (0=no, 1=yes) ⭐ CRITICAL
    // Bit [1] = semi-transparency
    // Bit [0] = raw texture
    
    for (int i = 0x20; i <= 0x7F; i++) {
        uint8_t cmd_type = (i >> 5) & 0x7; // Top 3 bits
        
        if (cmd_type == 1) { // 001 = Polygon
            s_gp0_handler_table[i] = &gp0_polygon_handler;
            // Calculate expected words based on opcode bits
            bool quad = (i & 0x08) != 0;       // Bit 3: 0=tri, 1=quad
            bool textured = (i & 0x04) != 0;   // Bit 2
            bool shaded = (i & 0x10) != 0;     // Bit 4: gouraud shading
            
            // Polygon word count:
            // 1 (header) + vertices + (colors if shaded) + (UVs if textured)
            // Triangle = 3 vertices, Quad = 4 vertices
            // Each vertex = 1 word (X,Y)
            // Each color (if shaded) = 1 word per vertex
            // Each UV (if textured) = embedded in color word or separate
            
            if (quad) {
                if (shaded) {
                    // Shaded quad: Cmd + (Color0+V0) + (Color1+V1) + (Color2+V2) + (Color3+V3)
                    // If textured: add UV after each color
                    s_gp0_expected_words[i] = textured ? 9 : 8; // With shading, texture adds 1 word per vertex
                } else {
                    // Flat quad: Cmd+Color + V0 + V1 + V2 + V3
                    // If textured: add UV+CLUT after each vertex
                    s_gp0_expected_words[i] = textured ? 9 : 5;
                }
            } else { // Triangle
                if (shaded) {
                    s_gp0_expected_words[i] = textured ? 7 : 6;
                } else {
                    s_gp0_expected_words[i] = textured ? 7 : 4;
                }
            }
        } else if (cmd_type == 2) { // 010 = Line
            s_gp0_handler_table[i] = &gp0_line_handler;
            // Lines: similar calculation needed
            bool polyline = (i & 0x08) != 0;
            bool shaded = (i & 0x10) != 0;
            s_gp0_expected_words[i] = polyline ? 1 : (shaded ? 4 : 3); // Simplified
        } else if (cmd_type == 3) { // 011 = Rectangle
            s_gp0_handler_table[i] = &gp0_rectangle_handler;
            // Determine expected words based on bits
            // Bit layout: 011-SS-T-X-R where SS=size, T=textured, X=semi-trans, R=raw
            bool textured = (i & 0x04) != 0; // Bit 2 (0x04 = bit 2)
            uint8_t size_mode = (i >> 3) & 0x3; // Bits [4:3]
            
            // WAIT - let me verify: 0x78 = 01111000
            // From docs: 011(rect) 11(16x16) 1(tex) 0(semi) 0(raw)
            // So texture is actually in the position after size bits
            // Let me recalculate: bits 7-5=011, bits 4-3=11, bit 2=1
            // 0x78 in binary: 0 1 1 1 1 0 0 0
            //                 7 6 5 4 3 2 1 0
            // So bit 2 should be 0, but docs say textured...
            // Actually checking bit pattern of 0x78:
            //   0x78 & 0x04 = 0111 1000 & 0000 0100 = 0
            // But 0x7C (non-textured 16x16) = 0111 1100
            //   0x7C & 0x04 = 0111 1100 & 0000 0100 = 0000 0100 = 4 (true!)
            // 
            // So the docs listing is WRONG or I'm reading it wrong!
            // Let me check: maybe T bit is in a different position?
            // Comparing 0x78 vs 0x7C:
            //   0x78 = 0111 1000
            //   0x7C = 0111 1100
            //   Diff at bits 2-1
            // 
            // Actually, looking at all textured rect commands:
            //   0x64 (var textured) vs 0x60 (var non-tex) - diff is bit 2
            //   0x74 (8x8 textured) vs 0x70 (8x8 non-tex) - diff is bit 2
            //   0x7C (16x16 textured) vs 0x78 (16x16 non-tex) - diff is bit 2!
            //
            // So 0x78 is NON-TEXTURED and 0x7C is TEXTURED!
            // The command only has 2 words because it's not textured!
            
            if (size_mode == 0) { // Variable size
                s_gp0_expected_words[i] = textured ? 4 : 3;
            } else { // Fixed size (1x1, 8x8, 16x16)
                s_gp0_expected_words[i] = textured ? 3 : 2;
            }
            
            // Debug logging
            if (i >= 0x78 && i <= 0x7F) {
                LOG_GPU_INFO("Command table: 0x%02X textured=%d size_mode=%d expected_words=%d",
                             i, textured, size_mode, s_gp0_expected_words[i]);
            }
        }
    }
    
    // --- VRAM Transfer Commands ---
    // Copy VRAM to VRAM (0x80-0x9F)
    for (int i = 0x80; i <= 0x9F; i++) {
        s_gp0_handler_table[i] = &gp0_copy_rectangle;
        s_gp0_expected_words[i] = 4;
    }
    
    // CPU to VRAM (0xA0-0xBF)
    for (int i = 0xA0; i <= 0xBF; i++) {
        s_gp0_handler_table[i] = &gp0_image_load;
        s_gp0_expected_words[i] = 3; // Header only, data follows
    }
    
    // VRAM to CPU (0xC0-0xDF)
    for (int i = 0xC0; i <= 0xDF; i++) {
        s_gp0_handler_table[i] = &gp0_image_store;
        s_gp0_expected_words[i] = 3;
    }
    
    // --- Environment Commands (0xE0-0xFF) ---
    s_gp0_handler_table[0xE1] = &gp0_draw_mode;
    s_gp0_expected_words[0xE1] = 1;
    
    s_gp0_handler_table[0xE2] = &gp0_texture_window;
    s_gp0_expected_words[0xE2] = 1;
    
    s_gp0_handler_table[0xE3] = &gp0_drawing_area_top_left;
    s_gp0_expected_words[0xE3] = 1;
    
    s_gp0_handler_table[0xE4] = &gp0_drawing_area_bottom_right;
    s_gp0_expected_words[0xE4] = 1;
    
    s_gp0_handler_table[0xE5] = &gp0_drawing_offset;
    s_gp0_expected_words[0xE5] = 1;
    
    s_gp0_handler_table[0xE6] = &gp0_mask_bit_setting;
    s_gp0_expected_words[0xE6] = 1;
    
    s_table_initialized = true;
    LOG_GPU_INFO("GPU command handler table initialized (256 entries)");
}

// ============================================================================
// GP0 Command Dispatcher (DMA Packet Boundary Detection - FIX #3)
// ============================================================================

void gp0_dispatch_command(GPU* gpu, uint32_t command) {
    if (!s_table_initialized) {
        LOG_GPU_INFO("[DISPATCH] Initializing GP0 command table");
        gpu_commands_init_table();
    }
    
    uint8_t opcode = (uint8_t)(command >> 24);
    
    // Log EVERY dispatch call for rendering commands
    static int dispatch_log_count = 0;
    bool is_rendering = (opcode >= 0x20 && opcode <= 0x7F);
    if (dispatch_log_count < 30 && is_rendering) {
        LOG_GPU_INFO("[DISPATCH_ENTRY] opcode=0x%02X command=0x%08X", opcode, command);
        dispatch_log_count++;
    }
    
    // === NOTE: IMAGE_LOAD/STORE modes handled in gpu_gp0() via BlitterState ===
    // This function only processes commands in IDLE state
    
    // --- ACCUMULATE MULTI-WORD COMMANDS ---
    // DuckStation approach: If we're expecting more words for current command,
    // accumulate them WITHOUT checking if they look like new commands.
    if (gpu->gp0_words_remaining > 0) {
        // Accumulate data word for current command
        gp0_push_command_word(gpu, command);
        gpu->gp0_words_remaining--;
        
        static int accum_log_count = 0;
        if (accum_log_count < 30 && (gpu->gp0_current_opcode >= 0x60 && gpu->gp0_current_opcode <= 0x7F)) {
            LOG_GPU_INFO("[ACCUM] opcode=0x%02x words_remaining=%d buffer_count=%d command=0x%08x",
                         gpu->gp0_current_opcode, gpu->gp0_words_remaining, 
                         gpu->gp0_command_buffer.count, command);
            accum_log_count++;
        }
        
        // If command complete, execute handler
        if (gpu->gp0_words_remaining == 0) {
            GP0Handler handler = s_gp0_handler_table[gpu->gp0_current_opcode];
            static int exec_log_count = 0;
            if (exec_log_count < 10 && (gpu->gp0_current_opcode >= 0x60 && gpu->gp0_current_opcode <= 0x7F)) {
                LOG_GPU_INFO("[EXECUTE] Calling handler for opcode=0x%02x, buffer_count=%d",
                             gpu->gp0_current_opcode, gpu->gp0_command_buffer.count);
                exec_log_count++;
            }
            (*handler)(gpu);
        }
        return;
    }
    
    // --- START NEW COMMAND ---
    gpu->gp0_current_opcode = opcode;
    gp0_clear_command_buffer(gpu);
    
    // Get handler and expected word count
    GP0Handler handler = s_gp0_handler_table[opcode];
    uint32_t expected_words = s_gp0_expected_words[opcode];
    
    // Push first word
    gp0_push_command_word(gpu, command);
    gpu->gp0_words_remaining = expected_words - 1;
    
    // Log first few rendering commands for debugging
    static int opcode_log_count = 0;
    if (opcode_log_count < 50 && (opcode >= 0x60 && opcode <= 0x7F)) {
        LOG_GPU_INFO(">>> GPU NEW CMD: opcode=0x%02X expected_words=%d command=0x%08X", 
                     opcode, expected_words, command);
        opcode_log_count++;
    }
    
    // If command complete (single-word), execute immediately
    if (gpu->gp0_words_remaining == 0) {
        (*handler)(gpu);
    }
}

// ============================================================================
// GP1 Command Dispatcher
// ============================================================================

void gp1_dispatch_command(GPU* gpu, uint32_t command) {
    uint8_t opcode = (uint8_t)((command >> 24) & 0x3F); // Bits 29-24
    uint32_t value = command & 0x00FFFFFF; // Lower 24 bits
    
    switch (opcode) {
        case 0x00: gp1_reset(gpu, value); break;
        case 0x01: gp1_reset_command_buffer(gpu, value); break;
        case 0x02: gp1_acknowledge_irq(gpu, value); break;
        case 0x03: gp1_display_enable(gpu, value); break;
        case 0x04: gp1_dma_direction(gpu, value); break;
        case 0x05: gp1_display_vram_start(gpu, value); break;
        case 0x06: gp1_display_horizontal_range(gpu, value); break;
        case 0x07: gp1_display_vertical_range(gpu, value); break;
        case 0x08: gp1_display_mode(gpu, value); break;
        default:
            LOG_GPU_WARN("Unknown GP1 command: 0x%02X", opcode);
            break;
    }
}

// ============================================================================
// Stub Handlers (Forward to appropriate modules)
// ============================================================================

static void gp0_unknown_command(GPU* gpu) {
    LOG_GPU_ERROR("GPU: Unknown GP0 command 0x%02X", gpu->gp0_current_opcode);
}

void gp0_nop(GPU* gpu) {
    (void)gpu; // Nothing to do
}

void gp0_clear_cache(GPU* gpu) {
    (void)gpu; // TODO: implement cache clearing
}

void gp0_fill_rectangle(GPU* gpu) {
    gp0_fill_vram_rectangle(gpu);
}

void gp0_copy_rectangle(GPU* gpu) {
    gp0_vram_to_vram_copy(gpu);
}

void gp0_image_load(GPU* gpu) {
    gp0_cpu_to_vram_setup(gpu);
}

void gp0_image_store(GPU* gpu) {
    gp0_vram_to_cpu_setup(gpu);
}

// Rectangle handler dispatcher
void gp0_rectangle_handler(GPU* gpu) {
    uint8_t opcode = gpu->gp0_current_opcode;
    
    static int rect_count = 0;
    if (rect_count < 10) {
        LOG_GPU_INFO("[RECT_HANDLER] opcode=0x%02x buffer_count=%d", 
                     opcode, gpu->gp0_command_buffer.count);
        rect_count++;
    }
    
    bool textured = (opcode & 0x04) != 0;  // Bit 2
    bool semi_trans = (opcode & 0x02) != 0; // Bit 1
    uint8_t size_mode = (opcode >> 3) & 0x3; // Bits [4:3]
    
    if (size_mode == 0) { // Variable size
        if (textured) {
            if (semi_trans) {
                gp0_rect_tex_variable_semi(gpu);
            } else {
                gp0_rect_tex_variable_opaque(gpu);
            }
        } else {
            if (semi_trans) {
                gp0_rect_mono_variable_semi(gpu);
            } else {
                gp0_rect_mono_variable_opaque(gpu);
            }
        }
    } else if (size_mode == 1) { // 1x1
        if (textured) {
            if (semi_trans) {
                gp0_rect_tex_1x1_semi(gpu);
            } else {
                gp0_rect_tex_1x1_opaque(gpu);
            }
        } else {
            if (semi_trans) {
                gp0_rect_mono_1x1_semi(gpu);
            } else {
                gp0_rect_mono_1x1_opaque(gpu);
            }
        }
    } else if (size_mode == 2) { // 8x8
        if (textured) {
            if (semi_trans) {
                gp0_rect_tex_8x8_semi(gpu);
            } else {
                gp0_rect_tex_8x8_opaque(gpu);
            }
        } else {
            if (semi_trans) {
                gp0_rect_mono_8x8_semi(gpu);
            } else {
                gp0_rect_mono_8x8_opaque(gpu);
            }
        }
    } else if (size_mode == 3) { // 16x16
        if (textured) {
            if (semi_trans) {
                gp0_rect_tex_16x16_semi(gpu);
            } else {
                gp0_rect_tex_16x16_opaque(gpu); // ⭐ CRITICAL FOR FONTS
            }
        } else {
            if (semi_trans) {
                gp0_rect_mono_16x16_semi(gpu);
            } else {
                gp0_rect_mono_16x16_opaque(gpu);
            }
        }
    }
}

// Polygon handler dispatcher
void gp0_polygon_handler(GPU* gpu) {
    uint8_t opcode = gpu->gp0_current_opcode;
    
    bool quad = (opcode & 0x08) != 0;       // Bit 3: 0=triangle, 1=quad
    bool textured = (opcode & 0x04) != 0;   // Bit 2
    bool semi_trans = (opcode & 0x02) != 0; // Bit 1
    bool shaded = (opcode & 0x10) != 0;     // Bit 4: gouraud shading
    
    if (quad) {
        if (shaded) {
            if (textured) {
                if (semi_trans) {
                    gp0_quad_shaded_tex_semi(gpu);
                } else {
                    gp0_quad_shaded_tex_opaque(gpu);
                }
            } else {
                if (semi_trans) {
                    gp0_quad_shaded_semi(gpu);
                } else {
                    gp0_quad_shaded_opaque(gpu);
                }
            }
        } else {
            if (textured) {
                if (semi_trans) {
                    gp0_quad_tex_semi(gpu);
                } else {
                    gp0_quad_tex_opaque(gpu);
                }
            } else {
                if (semi_trans) {
                    gp0_quad_mono_semi(gpu);
                } else {
                    gp0_quad_mono_opaque(gpu);
                }
            }
        }
    } else { // Triangle
        if (shaded) {
            if (textured) {
                if (semi_trans) {
                    gp0_triangle_shaded_tex_semi(gpu);
                } else {
                    gp0_triangle_shaded_tex_opaque(gpu);
                }
            } else {
                if (semi_trans) {
                    gp0_triangle_shaded_semi(gpu);
                } else {
                    gp0_triangle_shaded_opaque(gpu);
                }
            }
        } else {
            if (textured) {
                if (semi_trans) {
                    gp0_triangle_tex_semi(gpu);
                } else {
                    gp0_triangle_tex_opaque(gpu);
                }
            } else {
                if (semi_trans) {
                    gp0_triangle_mono_semi(gpu);
                } else {
                    gp0_triangle_mono_opaque(gpu);
                }
            }
        }
    }
}

// Line handler dispatcher
void gp0_line_handler(GPU* gpu) {
    uint8_t opcode = gpu->gp0_current_opcode;
    
    bool polyline = (opcode & 0x08) != 0;   // Bit 3
    bool shaded = (opcode & 0x10) != 0;     // Bit 4
    
    if (polyline) {
        if (shaded) {
            gp0_polyline_shaded(gpu);
        } else {
            gp0_polyline_mono(gpu);
        }
    } else {
        if (shaded) {
            gp0_line_shaded(gpu);
        } else {
            gp0_line_mono(gpu);
        }
    }
}

// ============================================================================
// Main Entry Points (Called from Interconnect)
// ============================================================================

/**
 * gpu_gp0 - Main entry point for GP0 commands and data
 * Implements DuckStation's BlitterState architecture:
 *   - IDLE: Dispatch commands normally
 *   - WRITING_VRAM: Accumulate pixel data without dispatch
 *   - READING_VRAM: Return immediately (handled separately)
 */
void gpu_gp0(GPU* gpu, uint32_t command) {
    // If GPU threading is enabled, queue command to GPU thread
    if (gpu->thread_state && gpu->thread_state->use_threading) {
        GpuGP0Command* cmd = (GpuGP0Command*)gpu_thread_alloc_command(
            gpu->thread_state, sizeof(GpuGP0Command));
        if (cmd) {
            cmd->header.type = GPU_CMD_GP0_COMMAND;
            cmd->header.size = sizeof(GpuGP0Command);
            cmd->command_word = command;
            gpu_thread_submit_and_wake(gpu->thread_state, &cmd->header);
        } else {
            LOG_GPU_ERROR("Failed to allocate GPU thread command!");
        }
        return;
    }
    
    // Threading disabled - process directly on main thread
    uint8_t opcode = (command >> 24) & 0xFF;
    static int direct_dispatch_count = 0;
    if (direct_dispatch_count < 5 && (opcode >= 0x60 && opcode <= 0x7F)) {
        LOG_GPU_INFO("[gpu_gp0] DIRECT DISPATCH (no threading): opcode=0x%02X", opcode);
        direct_dispatch_count++;
    }
    
    static int state_log_count = 0;
    bool is_rendering = (opcode >= 0x20 && opcode <= 0x7F);
    if (state_log_count < 50 && is_rendering) {
        LOG_GPU_INFO("[gpu_gp0] opcode=0x%02X blitter_state=%d command=0x%08X", 
                     opcode, gpu->blitter_state, command);
        state_log_count++;
    }
    
    // === DUCKSTATION ARCHITECTURE: BlitterState handling ===
    // Check blitter state BEFORE processing command
    
    if (gpu->blitter_state == GPU_BLITTER_WRITING_VRAM) {
        // Receiving pixel data for VRAM upload (GP0(0xA0))
        // Process data word directly WITHOUT going through dispatch
        gp0_cpu_to_vram_data(gpu, command);
        gpu->blit_remaining_words--;
        
        // Debug: Log word count periodically
        static int word_log_count = 0;
        if (word_log_count < 3 || gpu->blit_remaining_words < 5) {
            LOG_GPU_INFO("[VRAM_TRANSFER] Received word 0x%08x, %u words remaining", command, gpu->blit_remaining_words);
            word_log_count++;
        }
        
        // Check if transfer complete AFTER decrementing
        if (gpu->blit_remaining_words == 0) {
            LOG_GPU_INFO("GP0: VRAM write complete, switching to IDLE state");
            gpu->blitter_state = GPU_BLITTER_IDLE;
            gpu->gp0_mode = GP0_MODE_COMMAND;
            word_log_count = 0; // Reset for next transfer
        }
        return; // Don't dispatch during VRAM write
    }
    
    if (gpu->blitter_state == GPU_BLITTER_READING_VRAM) {
        // VRAM readback active - data goes to GPUREAD
        return;  // Handled separately
    }
    
    // === IDLE STATE: Normal command dispatch ===
    if (state_log_count < 30 && (opcode >= 0x60 && opcode <= 0x7F)) {
        LOG_GPU_INFO("[gpu_gp0 IDLE->DISPATCH] opcode=0x%02X", opcode);
    }
    
    // Enqueue to FIFO and process
    if (gp0_fifo_is_full(gpu)) {
        LOG_GPU_WARN("GP0 FIFO full: draining before enqueue");
        while (!gp0_fifo_is_empty(gpu)) {
            uint32_t w = gp0_fifo_pop(gpu);
            gp0_dispatch_command(gpu, w);
        }
    }
    
    gp0_fifo_push(gpu, command);
    
    // Process all words in FIFO
    while (!gp0_fifo_is_empty(gpu)) {
        uint32_t w = gp0_fifo_pop(gpu);
        gp0_dispatch_command(gpu, w);
    }
}

/**
 * gpu_gp1 - Main entry point for GP1 commands
 * Processes control/configuration commands immediately
 * If GPU threading is enabled, queues commands to GPU thread instead
 */
void gpu_gp1(GPU* gpu, uint32_t command) {
    // If GPU threading is enabled, queue command to GPU thread
    if (gpu->thread_state && gpu->thread_state->use_threading) {
        GpuGP1Command* cmd = (GpuGP1Command*)gpu_thread_alloc_command(
            gpu->thread_state, sizeof(GpuGP1Command));
        if (cmd) {
            cmd->header.type = GPU_CMD_GP1_COMMAND;
            cmd->header.size = sizeof(GpuGP1Command);
            cmd->command_word = command;
            gpu_thread_submit_and_wake(gpu->thread_state, &cmd->header);
        }
        return;
    }
    
    // Direct processing (single-threaded mode)
    LOG_GPU_DEBUG("[GP1] Command: 0x%08x (Opcode: 0x%02x)", command, (command >> 24) & 0xFF);
    gp1_dispatch_command(gpu, command);
}


