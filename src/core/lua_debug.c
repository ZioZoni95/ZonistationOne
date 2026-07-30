/*
 * lua_debug.c — see lua_debug.h.
 *
 * Owns a single lua_State, a console text ring-buffer, and two registry-ref
 * callback slots (on_break, on_event) — one each, not per-address: this is
 * sized for one interactive debugging session at a time, not a general
 * hook-table framework.
 */
#include "lua_debug.h"
#include "interconnect.h"
#include "spu.h"
#include "cdrom.h"
#include "cpu.h"
#include "debugger.h"
#include "ram.h"
#include "bios.h"
#include "gte.h"
#include "log.h"
#include <SDL2/SDL.h>

#include <string.h>
#include <stdio.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define LUA_DEBUG_CONSOLE_CAP (64 * 1024)

static lua_State*    g_L = NULL;
static Interconnect* g_inter = NULL;
static Cpu*          g_cpu = NULL;
static bool          g_active = false;

static int g_break_cb_ref = LUA_NOREF;
static int g_event_cb_ref = LUA_NOREF;

static char   g_console[LUA_DEBUG_CONSOLE_CAP];
static size_t g_console_len = 0;
static FILE*  g_log_file = NULL;

/* ---- console buffer ------------------------------------------------------
 * Also mirrors everything to logs/Lua.log, flushed on every line — unlike
 * the ImGui per-category log windows (debug_ui.cpp), which batch-flush every
 * 64 writes. A debug script's whole point is "run it, then go read what
 * happened" within the same short session, so immediate durability matters
 * more here than the small extra I/O cost. */

static void console_append(const char* s) {
    size_t n = strlen(s);
    if (n >= LUA_DEBUG_CONSOLE_CAP) {
        /* Pathological single write larger than the whole buffer: keep tail. */
        s += (n - (LUA_DEBUG_CONSOLE_CAP - 1));
        n = LUA_DEBUG_CONSOLE_CAP - 1;
    }
    if (g_console_len + n >= LUA_DEBUG_CONSOLE_CAP) {
        /* Drop the oldest half to make room, keep it simple/cheap. */
        size_t keep = g_console_len / 2;
        memmove(g_console, g_console + (g_console_len - keep), keep);
        g_console_len = keep;
    }
    memcpy(g_console + g_console_len, s, n);
    g_console_len += n;
    g_console[g_console_len] = '\0';

    if (g_log_file) {
        fputs(s, g_log_file);
        fflush(g_log_file);
    }
}

const char* lua_debug_console_text(void) {
    return g_console;
}

void lua_debug_console_clear(void) {
    g_console_len = 0;
    g_console[0] = '\0';
}

/* ---- direct memory peek (RAM/BIOS/scratchpad only — see header) -------- */

static bool peek_bytes(uint32_t vaddr, uint32_t size, uint32_t* out) {
    uint32_t phys = vaddr & 0x1FFFFFFF;
    const uint8_t* p = NULL;

    if (phys < RAM_SIZE) {
        p = &g_inter->ram->data[phys];
        if (phys + size > RAM_SIZE) return false;
    } else if (phys >= 0x1FC00000 && phys < 0x1FC80000) {
        uint32_t off = phys - 0x1FC00000;
        if (off + size > BIOS_SIZE) return false;
        p = &g_inter->bios->data[off];
    } else if (phys >= 0x1F800000 && phys < 0x1F800400) {
        uint32_t off = phys - 0x1F800000;
        if (off + size > SCRATCHPAD_SIZE) return false;
        p = &g_inter->scratchpad[off];
    } else {
        return false;
    }

    uint32_t v = 0;
    for (uint32_t i = 0; i < size; i++) v |= ((uint32_t)p[i]) << (8 * i);
    *out = v;
    return true;
}

/* ---- emu.* bindings ------------------------------------------------------ */

static int l_emu_log(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    console_append(msg);
    console_append("\n");
    LOG_DEBUGGER_INFO("[LUA] %s", msg);
    return 0;
}

/* print() override: same sink as emu.log, so plain `print(...)` in scripts
 * shows up in the console panel without scripts needing to know emu.log. */
static int l_print(lua_State* L) {
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        if (i > 1) console_append("\t");
        const char* s = luaL_tolstring(L, i, NULL);
        console_append(s);
        lua_pop(L, 1);
    }
    console_append("\n");
    return 0;
}

static int l_emu_cycles(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)g_inter->cpu_cycle_counter);
    return 1;
}

static int l_emu_gte_data(lua_State* L) {
    lua_Integer i = luaL_checkinteger(L, 1);
    luaL_argcheck(L, i >= 0 && i < 32, 1, "GTE data register index 0-31");
    lua_pushinteger(L, (lua_Integer)gte_read_data_register(&g_cpu->gte, (uint32_t)i));
    return 1;
}

/* emu.disasm(addr) -> "mnemonic operands" string, via the real disassembler
 * (src/cpu/cpu_disasm.c) — use this instead of hand-decoding hex in scripts,
 * it's the same disassembler the Live Disasm/Exec Trace panels use. */
static int l_emu_disasm(lua_State* L) {
    uint32_t addr = (uint32_t)luaL_checkinteger(L, 1);
    uint32_t v = 0;
    if (!peek_bytes(addr, 4, &v)) return luaL_error(L, "disasm: address 0x%08x out of range", addr);
    const char* s = disassemble_mips(v, addr);
    lua_pushstring(L, s ? s : "?");
    return 1;
}

static int l_emu_pc(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)(uint32_t)g_cpu->current_pc);
    return 1;
}

static int l_emu_reg(lua_State* L) {
    lua_Integer i = luaL_checkinteger(L, 1);
    luaL_argcheck(L, i >= 0 && i < 32, 1, "register index 0-31");
    lua_pushinteger(L, (lua_Integer)(uint32_t)g_cpu->regs[i]);
    return 1;
}

static int l_emu_read_u8(lua_State* L) {
    uint32_t addr = (uint32_t)luaL_checkinteger(L, 1);
    uint32_t v = 0;
    if (!peek_bytes(addr, 1, &v)) return luaL_error(L, "read_u8: address 0x%08x out of range", addr);
    lua_pushinteger(L, (lua_Integer)v);
    return 1;
}
static int l_emu_read_u16(lua_State* L) {
    uint32_t addr = (uint32_t)luaL_checkinteger(L, 1);
    uint32_t v = 0;
    if (!peek_bytes(addr, 2, &v)) return luaL_error(L, "read_u16: address 0x%08x out of range", addr);
    lua_pushinteger(L, (lua_Integer)v);
    return 1;
}
static int l_emu_read_u32(lua_State* L) {
    uint32_t addr = (uint32_t)luaL_checkinteger(L, 1);
    uint32_t v = 0;
    if (!peek_bytes(addr, 4, &v)) return luaL_error(L, "read_u32: address 0x%08x out of range", addr);
    lua_pushinteger(L, (lua_Integer)v);
    return 1;
}

static int l_emu_add_breakpoint(lua_State* L) {
    uint32_t addr = (uint32_t)luaL_checkinteger(L, 1);
    lua_pushboolean(L, debugger_add_breakpoint(&g_inter->debugger, addr));
    return 1;
}
static int l_emu_remove_breakpoint(lua_State* L) {
    uint32_t addr = (uint32_t)luaL_checkinteger(L, 1);
    lua_pushboolean(L, debugger_remove_breakpoint(&g_inter->debugger, addr));
    return 1;
}
static int l_emu_add_write_watch(lua_State* L) {
    uint32_t addr = (uint32_t)luaL_checkinteger(L, 1);
    lua_pushboolean(L, debugger_add_write_watchpoint(&g_inter->debugger, addr));
    return 1;
}
static int l_emu_remove_write_watch(lua_State* L) {
    uint32_t addr = (uint32_t)luaL_checkinteger(L, 1);
    lua_pushboolean(L, debugger_remove_write_watchpoint(&g_inter->debugger, addr));
    return 1;
}
static int l_emu_add_read_watch(lua_State* L) {
    uint32_t addr = (uint32_t)luaL_checkinteger(L, 1);
    lua_pushboolean(L, debugger_add_read_watchpoint(&g_inter->debugger, addr));
    return 1;
}
static int l_emu_remove_read_watch(lua_State* L) {
    uint32_t addr = (uint32_t)luaL_checkinteger(L, 1);
    lua_pushboolean(L, debugger_remove_read_watchpoint(&g_inter->debugger, addr));
    return 1;
}

static int l_emu_pause(lua_State* L) {
    (void)L;
    g_inter->debugger.paused = true;
    return 0;
}
static int l_emu_resume(lua_State* L) {
    (void)L;
    g_inter->debugger.paused = false;
    return 0;
}
static int l_emu_is_paused(lua_State* L) {
    lua_pushboolean(L, g_inter->debugger.paused);
    return 1;
}

static int l_emu_on_break(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (g_break_cb_ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, g_break_cb_ref);
    lua_pushvalue(L, 1);
    g_break_cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}
static int l_emu_on_event(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (g_event_cb_ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, g_event_cb_ref);
    lua_pushvalue(L, 1);
    g_event_cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

static int l_emu_gp0_opcode(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)g_inter->gpu.gp0_current_opcode);
    return 1;
}

static int l_emu_gp0_word_count(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)g_inter->gpu.gp0_command_buffer.count);
    return 1;
}

static int l_emu_gp0_word(lua_State* L) {
    lua_Integer i = luaL_checkinteger(L, 1);
    luaL_argcheck(L, i >= 0 && i < MAX_GPU_COMMAND_WORDS, 1, "gp0 word index out of range");
    lua_pushinteger(L, (lua_Integer)g_inter->gpu.gp0_command_buffer.buffer[i]);
    return 1;
}

/* One pixel of the macroblock MDEC has just finished decoding: block_rgb is the
 * 16x16 RGB result, before the output FIFO / DMA / VRAM ever see it, so this
 * isolates "is the decoder producing a real image?" from every downstream
 * placement question. Index 0..255, row-major within the 16x16 block. */
static int l_emu_mdec_block(lua_State* L) {
    lua_Integer i = luaL_checkinteger(L, 1);
    luaL_argcheck(L, i >= 0 && i < 256, 1, "mdec block index out of range");
    lua_pushinteger(L, (lua_Integer)g_inter->mdec.block_rgb[i]);
    return 1;
}

/* Live timer counter + mode/source, without a bus read (bypasses side effects).
 * counter is whatever timers_step last left — lets a script see how stale it is
 * between chunks. */
static int l_emu_timer(lua_State* L) {
    lua_Integer i = luaL_checkinteger(L, 1);
    luaL_argcheck(L, i >= 0 && i < 3, 1, "timer index out of range");
    const Timer* t = &g_inter->timers_state.timers[i];
    lua_pushinteger(L, (lua_Integer)t->counter);
    lua_pushinteger(L, (lua_Integer)t->clock_source);
    lua_pushboolean(L, t->counting_enabled);
    return 3;
}

/* Read one VRAM halfword directly from the CPU-side buffer at (x,y), same
 * store the viewer's readout uses — for checking whether A0 uploads actually
 * landed. */
static int l_emu_vram16(lua_State* L) {
    lua_Integer x = luaL_checkinteger(L, 1);
    lua_Integer y = luaL_checkinteger(L, 2);
    if (x < 0 || x >= 1024 || y < 0 || y >= 512) { lua_pushnil(L); return 1; }
    const uint16_t* v = (const uint16_t*)g_inter->gpu.vram.data;
    lua_pushinteger(L, (lua_Integer)v[(size_t)y * 1024 + (size_t)x]);
    return 1;
}

/* Peek the MDEC input FIFO without consuming it: index 0 is the next halfword
 * the RLE decoder will read. Lets a script see the raw compressed stream as
 * MDEC sees it, to tell a bad bitstream apart from a bad decoder. */
static int l_emu_mdec_in_peek(lua_State* L) {
    lua_Integer i = luaL_checkinteger(L, 1);
    const Mdec* m = &g_inter->mdec;
    if (i < 0 || (uint32_t)i >= m->in_count) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, (lua_Integer)m->in_buf[(m->in_head + (uint32_t)i) % MDEC_IN_FIFO_HW]);
    return 1;
}

static int l_emu_mdec_in_count(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)g_inter->mdec.in_count);
    return 1;
}

/* Live MDEC DMA cursors: where ch0 is reading the compressed stream from and
 * where ch1 is writing decoded output to, plus words left on each. Lets a
 * script read the same RAM the DMA is feeding MDEC and compare. */
static int l_emu_mdec_dma(lua_State* L) {
    const Dma* d = &g_inter->dma;
    lua_pushinteger(L, (lua_Integer)d->mdec_in_addr);
    lua_pushinteger(L, (lua_Integer)d->mdec_in_remaining);
    lua_pushinteger(L, (lua_Integer)d->mdec_out_addr);
    lua_pushinteger(L, (lua_Integer)d->mdec_out_remaining);
    return 4;
}

/* MDEC decode context: output depth, halfwords left in the command, and the
 * quant/scale tables the IDCT is running with. */
static int l_emu_mdec_info(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)g_inter->mdec.output_depth);
    lua_pushinteger(L, (lua_Integer)g_inter->mdec.remaining_halfwords);
    lua_pushinteger(L, (lua_Integer)g_inter->mdec.current_q_scale);
    return 3;
}

static int l_emu_mdec_scale(lua_State* L) {
    lua_Integer i = luaL_checkinteger(L, 1);
    luaL_argcheck(L, i >= 0 && i < 64, 1, "scale table index out of range");
    lua_pushinteger(L, (lua_Integer)g_inter->mdec.scale_table[i]);
    return 1;
}

static int l_emu_mdec_qtable(lua_State* L) {
    lua_Integer i = luaL_checkinteger(L, 1);
    int chroma = lua_toboolean(L, 2);
    luaL_argcheck(L, i >= 0 && i < 64, 1, "quant table index out of range");
    lua_pushinteger(L, (lua_Integer)(chroma ? g_inter->mdec.iq_uv[i]
                                            : g_inter->mdec.iq_y[i]));
    return 1;
}

/* Renderer staging-pool telemetry: used, peak, queued updates, dropped rects. */
static int l_emu_gpu_pool(lua_State* L) {
    uint32_t used = 0, peak = 0, updates = 0, skips = 0;
    renderer_get_pool_stats(&g_inter->gpu.renderer, &used, &peak, &updates, &skips);
    lua_pushinteger(L, (lua_Integer)used);
    lua_pushinteger(L, (lua_Integer)peak);
    lua_pushinteger(L, (lua_Integer)updates);
    lua_pushinteger(L, (lua_Integer)skips);
    return 4;
}

/* Geometry of the most recent GP0(0xA0) CPU/DMA→VRAM upload, in VRAM
 * halfword units — pairs with the "gp0_vram_upload" event. */
static int l_emu_vram_upload_rect(lua_State* L) {
    Gpu* g = &g_inter->gpu;
    lua_pushinteger(L, (lua_Integer)g->vram_load_x);
    lua_pushinteger(L, (lua_Integer)g->vram_load_y);
    lua_pushinteger(L, (lua_Integer)g->vram_load_w);
    lua_pushinteger(L, (lua_Integer)g->vram_load_h);
    return 4;
}

/* GPUSTAT as the CPU would read it — display depth (bit 21), video mode,
 * resolution, display-disable etc. without having to breakpoint the port read. */
static int l_emu_gpustat(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)gpu_read_status(&g_inter->gpu));
    return 1;
}

/* Display area origin/size as programmed by GP1(05)/(06)/(07). */
static int l_emu_display_area(lua_State* L) {
    Gpu* g = &g_inter->gpu;
    lua_pushinteger(L, (lua_Integer)g->display_vram_x_start);
    lua_pushinteger(L, (lua_Integer)g->display_vram_y_start);
    lua_pushinteger(L, (lua_Integer)g->display_horiz_start);
    lua_pushinteger(L, (lua_Integer)g->display_horiz_end);
    lua_pushinteger(L, (lua_Integer)g->display_line_start);
    lua_pushinteger(L, (lua_Integer)g->display_line_end);
    return 6;
}

/* Drawing area (GP0 E3/E4) and drawing offset (GP0 E5). The GL path scissors
 * every batch to this area, so it decides which primitives can reach the
 * unified VRAM texture — a fill written unclipped into the CPU-side VRAM can
 * still be clipped away on its way to the texture. */
static int l_emu_draw_area(lua_State* L) {
    Gpu* g = &g_inter->gpu;
    lua_pushinteger(L, (lua_Integer)g->drawing_area_left);
    lua_pushinteger(L, (lua_Integer)g->drawing_area_top);
    lua_pushinteger(L, (lua_Integer)g->drawing_area_right);
    lua_pushinteger(L, (lua_Integer)g->drawing_area_bottom);
    lua_pushinteger(L, (lua_Integer)g->drawing_x_offset);
    lua_pushinteger(L, (lua_Integer)g->drawing_y_offset);
    return 6;
}

/* SPU output-path health: samples produced from the emulated clock, samples
 * dropped because the output ring was full, and how full that ring is right
 * now. Sample count against emulated time is the direct check that generation
 * is paced by the guest and not by the host. */
static int l_emu_spu_stats(lua_State* L) {
    Spu* spu = &g_inter->spu;
    int head = spu->sample_buf_head, tail = spu->sample_buf_tail;
    int used = (tail - head + SPU_SAMPLE_BUFFER_SIZE) % SPU_SAMPLE_BUFFER_SIZE;
    lua_pushinteger(L, (lua_Integer)spu->total_samples_generated);
    lua_pushinteger(L, (lua_Integer)spu->dropped_samples);
    lua_pushinteger(L, (lua_Integer)used);
    lua_pushinteger(L, (lua_Integer)SPU_SAMPLE_BUFFER_SIZE);
    lua_pushinteger(L, (lua_Integer)spu->total_key_on_events);
    return 5;
}

/* Reverb internal state — what a register poll cannot see. Lets a Lua trace tell
 * "the game switched reverb off" (control/vol/EON) from "the reverb network's own
 * tail is decaying wrong" (out_l/out_r while still enabled). Returns:
 *   control, reverb_enable(bool), vol_l, vol_r, eon_mask, base, cur_addr,
 *   in_l, in_r, out_l, out_r */
static int l_emu_reverb(lua_State* L) {
    Spu* spu = &g_inter->spu;
    lua_pushinteger(L, (lua_Integer)spu->control);
    lua_pushboolean(L, (spu->control & (1u << 7)) != 0);      /* SPUCNT reverb master enable */
    lua_pushinteger(L, (lua_Integer)spu->reverb_vol_left);
    lua_pushinteger(L, (lua_Integer)spu->reverb_vol_right);
    lua_pushinteger(L, (lua_Integer)spu->reverb_on);          /* per-voice EON mask */
    lua_pushinteger(L, (lua_Integer)spu->reverb_base);
    lua_pushinteger(L, (lua_Integer)spu->reverb_current_addr);
    lua_pushinteger(L, (lua_Integer)spu->reverb_in_l);
    lua_pushinteger(L, (lua_Integer)spu->reverb_in_r);
    lua_pushinteger(L, (lua_Integer)spu->reverb_out_l);
    lua_pushinteger(L, (lua_Integer)spu->reverb_out_r);
    return 11;
}

/* CD audio path health: FIFO depth, samples the drive has fed in, samples the
 * SPU has taken out, and samples lost to overflow. During FMV playback the XA
 * stream is the audio source, so a starving or overflowing FIFO here is heard
 * directly. Also reports SPUCNT, whose reverb/CD-audio enables decide what the
 * mixer is even supposed to be doing. */
static int l_emu_cd_audio(lua_State* L) {
    Cdrom* cd = &g_inter->cdrom;
    lua_pushinteger(L, (lua_Integer)cd->audio_fifo.count);
    lua_pushinteger(L, (lua_Integer)cd->audio_fifo.total_pushed);
    lua_pushinteger(L, (lua_Integer)cd->audio_fifo.total_popped);
    lua_pushinteger(L, (lua_Integer)cd->audio_fifo.total_dropped);
    lua_pushinteger(L, (lua_Integer)g_inter->spu.control);
    lua_pushinteger(L, (lua_Integer)cd->sectors_read_total);
    lua_pushinteger(L, (lua_Integer)cd->xa_sectors_total);
    return 7;
}

/* Host wall-clock milliseconds. Emulated cycles against this is the emulator's
 * real-time speed, which is what decides whether the audio device can be fed at
 * the rate it drains. */
static int l_emu_host_ms(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)SDL_GetTicks());
    return 1;
}

/* Interrupt controller state: I_STAT (latched requests), I_MASK (enables), and
 * the per-source line levels. A source that latches in I_STAT but is masked, or
 * that stays latched forever, means the guest's handler is not running — which
 * looks from the guest's side like an event that never arrives. */
static int l_emu_irq(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)g_inter->irq_status);
    lua_pushinteger(L, (lua_Integer)g_inter->irq_mask);
    lua_pushinteger(L, (lua_Integer)(uint32_t)g_cpu->sr);
    lua_pushinteger(L, (lua_Integer)(uint32_t)g_cpu->cause);
    return 4;
}

static const luaL_Reg s_emu_funcs[] = {
    {"log",               l_emu_log},
    {"pc",                l_emu_pc},
    {"cycles",            l_emu_cycles},
    {"gte_data",          l_emu_gte_data},
    {"disasm",            l_emu_disasm},
    {"reg",               l_emu_reg},
    {"read_u8",           l_emu_read_u8},
    {"read_u16",          l_emu_read_u16},
    {"read_u32",          l_emu_read_u32},
    {"add_breakpoint",    l_emu_add_breakpoint},
    {"remove_breakpoint", l_emu_remove_breakpoint},
    {"add_write_watch",   l_emu_add_write_watch},
    {"remove_write_watch",l_emu_remove_write_watch},
    {"add_read_watch",    l_emu_add_read_watch},
    {"remove_read_watch", l_emu_remove_read_watch},
    {"pause",             l_emu_pause},
    {"resume",            l_emu_resume},
    {"is_paused",         l_emu_is_paused},
    {"on_break",          l_emu_on_break},
    {"on_event",          l_emu_on_event},
    {"gp0_opcode",        l_emu_gp0_opcode},
    {"gp0_word_count",    l_emu_gp0_word_count},
    {"gp0_word",          l_emu_gp0_word},
    {"gpustat",           l_emu_gpustat},
    {"draw_area",         l_emu_draw_area},
    {"spu_stats",         l_emu_spu_stats},
    {"reverb",            l_emu_reverb},
    {"cd_audio",          l_emu_cd_audio},
    {"host_ms",           l_emu_host_ms},
    {"irq",               l_emu_irq},
    {"vram_upload_rect",  l_emu_vram_upload_rect},
    {"gpu_pool",          l_emu_gpu_pool},
    {"mdec_block",        l_emu_mdec_block},
    {"mdec_in_peek",      l_emu_mdec_in_peek},
    {"mdec_in_count",     l_emu_mdec_in_count},
    {"mdec_dma",          l_emu_mdec_dma},
    {"vram16",            l_emu_vram16},
    {"timer",             l_emu_timer},
    {"mdec_info",         l_emu_mdec_info},
    {"mdec_scale",        l_emu_mdec_scale},
    {"mdec_qtable",       l_emu_mdec_qtable},
    {"display_area",      l_emu_display_area},
    {NULL, NULL}
};

/* ---- lifecycle ----------------------------------------------------------- */

void lua_debug_init(struct Interconnect* inter, struct Cpu* cpu) {
    g_inter = (Interconnect*)inter;
    g_cpu   = (Cpu*)cpu;
    g_console_len = 0;
    g_console[0] = '\0';
    g_break_cb_ref = LUA_NOREF;
    g_event_cb_ref = LUA_NOREF;
    g_log_file = fopen("logs/Lua.log", "w");

    g_L = luaL_newstate();
    if (!g_L) {
        LOG_DEBUGGER_ERROR("[LUA] luaL_newstate failed");
        return;
    }
    /* Individually require the libs we want instead of luaL_openlibs(), since
     * loadlib.c (package/require/dlopen) is deliberately excluded from the
     * build — a debug script has no legitimate use for dynamic native-lib
     * loading, and skipping it drops the -ldl link requirement too. */
    static const luaL_Reg libs[] = {
        {"_G",     luaopen_base},
        {"table",  luaopen_table},
        {"string", luaopen_string},
        {"math",   luaopen_math},
        {"os",     luaopen_os},
        {NULL, NULL}
    };
    for (const luaL_Reg* l = libs; l->func; l++) {
        luaL_requiref(g_L, l->name, l->func, 1);
        lua_pop(g_L, 1);
    }

    /* Global print() -> console panel (in addition to emu.log). */
    lua_pushcfunction(g_L, l_print);
    lua_setglobal(g_L, "print");

    /* emu.* table */
    luaL_newlib(g_L, s_emu_funcs);
    lua_setglobal(g_L, "emu");

    g_active = true;
    console_append("Lua 5.4 debug console ready.\n");
}

void lua_debug_shutdown(void) {
    if (g_L) {
        lua_close(g_L);
        g_L = NULL;
    }
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    g_active = false;
}

static bool report_lua_error(int status) {
    if (status != LUA_OK) {
        const char* msg = lua_tostring(g_L, -1);
        console_append("error: ");
        console_append(msg ? msg : "(unknown)");
        console_append("\n");
        lua_pop(g_L, 1);
        return false;
    }
    return true;
}

bool lua_debug_run_string(const char* code) {
    if (!g_active) return false;
    int status = luaL_loadstring(g_L, code);
    if (status == LUA_OK) status = lua_pcall(g_L, 0, 0, 0);
    return report_lua_error(status);
}

bool lua_debug_run_file(const char* path) {
    if (!g_active) return false;
    int status = luaL_loadfile(g_L, path);
    if (status == LUA_OK) status = lua_pcall(g_L, 0, 0, 0);
    return report_lua_error(status);
}

/* ---- native -> Lua dispatch ----------------------------------------------- */

void lua_debug_notify(const char* event_name) {
    if (!g_active || g_event_cb_ref == LUA_NOREF) return;
    lua_rawgeti(g_L, LUA_REGISTRYINDEX, g_event_cb_ref);
    lua_pushstring(g_L, event_name);
    int status = lua_pcall(g_L, 1, 0, 0);
    report_lua_error(status);
}

void lua_debug_dispatch_break(const char* reason) {
    if (!g_active || g_break_cb_ref == LUA_NOREF) return;
    lua_rawgeti(g_L, LUA_REGISTRYINDEX, g_break_cb_ref);
    lua_pushstring(g_L, reason);
    int status = lua_pcall(g_L, 1, 0, 0);
    report_lua_error(status);
}
