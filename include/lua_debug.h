/*
 * lua_debug.h — embedded Lua 5.4 scripting/debug console.
 *
 * Redux-inspired but scoped to plain Lua 5.4 (vendored in third_party/lua/,
 * no LuaJIT build machinery) and to what's actually useful for debugging
 * this emulator: memory/register inspection, breakpoints, watchpoints,
 * pause/resume, and a couple of named native-event probes for internal
 * state transitions that have no CPU-side PC to breakpoint on (see
 * lua_debug_notify).
 */
#ifndef LUA_DEBUG_H
#define LUA_DEBUG_H

#include <stdbool.h>
#include <stddef.h>

struct Interconnect;
struct Cpu;

void lua_debug_init(struct Interconnect* inter, struct Cpu* cpu);
void lua_debug_shutdown(void);

/* Run inline source (console REPL / "Run" button on the scratch buffer). */
bool lua_debug_run_string(const char* code);
/* Load and run a .lua file (script-editor "Load & Run"). */
bool lua_debug_run_file(const char* path);

/* Console output buffer (print()/emu.log() land here) for the ImGui panel. */
const char* lua_debug_console_text(void);
void        lua_debug_console_clear(void);

/* Native-probe hook: fires the registered emu.on_event(name) callback, if
 * any. Used for internal state transitions (e.g. a DMA channel completing)
 * that have no PSX-side PC to set a breakpoint on. Cheap no-op when no
 * callback is registered — safe to call from hot-ish paths. */
void lua_debug_notify(const char* event_name);

/* Called from debugger_handle_break() — dispatches to emu.on_break(reason),
 * if registered. The callback may call emu.resume() to un-pause. */
void lua_debug_dispatch_break(const char* reason);

/* emu.load_state() parks its request instead of restoring in place — see the
 * comment on l_emu_load_state. The host loop drains it between frames. */
bool lua_debug_take_pending_state_load(char* out, size_t out_size);
bool lua_debug_take_pending_state_save(char* out, size_t out_size);

#endif // LUA_DEBUG_H
