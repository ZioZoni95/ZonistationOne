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
#include "cpu.h"
#include "debugger.h"
#include "ram.h"
#include "bios.h"
#include "gte.h"
#include "log.h"

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
