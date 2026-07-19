/**
 * debugger.c
 * Implementation of the simple debugger component.
 */
#include "debugger.h"
#include "cpu.h"
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "lua_debug.h"

/**
 * @brief Initializes the debugger state.
 */
void debugger_init(Debugger* dbg) {
    dbg->breakpoint_count = 0;
    dbg->read_watchpoint_count = 0;
    dbg->write_watchpoint_count = 0;
    dbg->paused = false;
    dbg->step_skip_bp = false;
    for (int i = 0; i < MAX_BREAKPOINTS; i++) dbg->bp_enabled[i] = true;
}

// ============================================================================
// Breakpoint Management
// ============================================================================
bool debugger_add_breakpoint(Debugger* dbg, uint32_t addr) {
    for (uint32_t i = 0; i < dbg->breakpoint_count; ++i)
        if (dbg->breakpoints[i] == addr) return true;
    if (dbg->breakpoint_count >= MAX_BREAKPOINTS) return false;
    uint32_t idx = dbg->breakpoint_count++;
    dbg->breakpoints[idx] = addr;
    dbg->bp_enabled[idx] = true;
    return true;
}

bool debugger_remove_breakpoint(Debugger* dbg, uint32_t addr) {
    for (uint32_t i = 0; i < dbg->breakpoint_count; ++i) {
        if (dbg->breakpoints[i] == addr) {
            uint32_t last = --dbg->breakpoint_count;
            dbg->breakpoints[i] = dbg->breakpoints[last];
            dbg->bp_enabled[i]  = dbg->bp_enabled[last];
            return true;
        }
    }
    return false;
}

void debugger_check_breakpoint(Debugger* dbg, struct Cpu* cpu) {
    if (dbg->paused) return;
    uint32_t current_pc = ((Cpu*)cpu)->current_pc;
    for (uint32_t i = 0; i < dbg->breakpoint_count; ++i) {
        if (dbg->bp_enabled[i] && dbg->breakpoints[i] == current_pc) {
            char reason[64];
            snprintf(reason, sizeof(reason), "Breakpoint at 0x%08x", current_pc);
            debugger_handle_break(dbg, cpu, reason);
            return;
        }
    }
}

// ============================================================================
// Watchpoint Management
// ============================================================================
/**
 * @brief Adds a read watchpoint at a specific memory address.
 * @param dbg Pointer to the Debugger instance.
 * @param addr The memory address to watch.
 * @return True if added successfully or already exists, false if the list is full.
 */
bool debugger_add_read_watchpoint(Debugger* dbg, uint32_t addr) {
    // Check for duplicates
    for (uint32_t i = 0; i < dbg->read_watchpoint_count; ++i) {
        if (dbg->read_watchpoints[i] == addr) {
             printf("Debugger: Read watchpoint at 0x%08x already exists.\n", addr);
             return true; // Success (already present)
        }
    }
    // Check for space
    if (dbg->read_watchpoint_count >= MAX_WATCHPOINTS) {
        fprintf(stderr, "Debugger Error: Cannot add read watchpoint at 0x%08x. Maximum (%d) reached.\n", addr, MAX_WATCHPOINTS);
        return false; // Failure (array full)
    }
    // Add
    dbg->read_watchpoints[dbg->read_watchpoint_count] = addr;
    dbg->read_watchpoint_count++;
    printf("Debugger: Read watchpoint added at 0x%08x. (%u/%d)\n", addr, dbg->read_watchpoint_count, MAX_WATCHPOINTS);
    return true; // Success
}

/**
 * @brief Removes a read watchpoint at the specified address.
 * @param dbg Pointer to the Debugger instance.
 * @param addr The memory address of the watchpoint to remove.
 * @return True if removed successfully, false if not found.
 */
bool debugger_remove_read_watchpoint(Debugger* dbg, uint32_t addr) {
     for (uint32_t i = 0; i < dbg->read_watchpoint_count; ++i) {
        if (dbg->read_watchpoints[i] == addr) {
            // Found: Use swap-with-last for O(1) removal
            dbg->read_watchpoints[i] = dbg->read_watchpoints[dbg->read_watchpoint_count - 1];
            // Optional: Zero out the now unused last slot
            // dbg->read_watchpoints[dbg->read_watchpoint_count - 1] = 0;
            dbg->read_watchpoint_count--;
            printf("Debugger: Read watchpoint removed at 0x%08x. (%u/%d)\n", addr, dbg->read_watchpoint_count, MAX_WATCHPOINTS);
            return true; // Success
        }
    }
    printf("Debugger: Read watchpoint at 0x%08x not found for removal.\n", addr);
    return false; // Failure (not found)
}

/**
 * @brief Adds a write watchpoint at a specific memory address.
 * @param dbg Pointer to the Debugger instance.
 * @param addr The memory address to watch.
 * @return True if added successfully or already exists, false if the list is full.
 */
bool debugger_add_write_watchpoint(Debugger* dbg, uint32_t addr) {
    // Check for duplicates
    for (uint32_t i = 0; i < dbg->write_watchpoint_count; ++i) {
        if (dbg->write_watchpoints[i] == addr) {
             printf("Debugger: Write watchpoint at 0x%08x already exists.\n", addr);
             return true; // Success (already present)
        }
    }
    // Check for space
    if (dbg->write_watchpoint_count >= MAX_WATCHPOINTS) {
        fprintf(stderr, "Debugger Error: Cannot add write watchpoint at 0x%08x. Maximum (%d) reached.\n", addr, MAX_WATCHPOINTS);
        return false; // Failure (array full)
    }
    // Add
    dbg->write_watchpoints[dbg->write_watchpoint_count] = addr;
    dbg->write_watchpoint_count++;
    printf("Debugger: Write watchpoint added at 0x%08x. (%u/%d)\n", addr, dbg->write_watchpoint_count, MAX_WATCHPOINTS);
    return true; // Success
}

/**
 * @brief Removes a write watchpoint at the specified address.
 * @param dbg Pointer to the Debugger instance.
 * @param addr The memory address of the watchpoint to remove.
 * @return True if removed successfully, false if not found.
 */
bool debugger_remove_write_watchpoint(Debugger* dbg, uint32_t addr) {
     for (uint32_t i = 0; i < dbg->write_watchpoint_count; ++i) {
        if (dbg->write_watchpoints[i] == addr) {
            // Found: Use swap-with-last
            dbg->write_watchpoints[i] = dbg->write_watchpoints[dbg->write_watchpoint_count - 1];
            // Optional: Zero out the now unused last slot
            // dbg->write_watchpoints[dbg->write_watchpoint_count - 1] = 0;
            dbg->write_watchpoint_count--;
            printf("Debugger: Write watchpoint removed at 0x%08x. (%u/%d)\n", addr, dbg->write_watchpoint_count, MAX_WATCHPOINTS);
            return true; // Success
        }
    }
    printf("Debugger: Write watchpoint at 0x%08x not found for removal.\n", addr);
    return false; // Failure (not found)
}

/**
 * @brief Checks if a memory read access overlaps with any active read watchpoints.
 * @param dbg Pointer to the Debugger instance.
 * @param cpu Pointer to the Cpu instance (using 'struct Cpu*' from forward decl).
 * @param addr The starting memory address being read from.
 * @param size The size of the read access (1, 2, or 4 bytes).
 */
void debugger_check_read_watchpoint(Debugger* dbg, struct Cpu* cpu, uint32_t addr, uint32_t size) {
    if (dbg->paused) return;
    Cpu* c = (Cpu*)cpu;
    for (uint32_t i = 0; i < dbg->read_watchpoint_count; ++i) {
        uint32_t wp = dbg->read_watchpoints[i];
        if (wp >= addr && wp < (addr + size)) {
            char reason[100];
            snprintf(reason, sizeof(reason), "Read watchpoint 0x%08x (PC=0x%08x)", wp, c->current_pc);
            debugger_handle_break(dbg, cpu, reason);
            return;
        }
    }
}

/**
 * @brief Checks if a memory write access overlaps with any active write watchpoints.
 * @param dbg Pointer to the Debugger instance.
 * @param cpu Pointer to the Cpu instance (using 'struct Cpu*' from forward decl).
 * @param addr The starting memory address being written to.
 * @param size The size of the write access (1, 2, or 4 bytes).
 */
void debugger_check_write_watchpoint(Debugger* dbg, struct Cpu* cpu, uint32_t addr, uint32_t size) {
    if (dbg->paused) return;
    Cpu* c = (Cpu*)cpu;
    for (uint32_t i = 0; i < dbg->write_watchpoint_count; ++i) {
        uint32_t wp = dbg->write_watchpoints[i];
        if (wp >= addr && wp < (addr + size)) {
            char reason[100];
            snprintf(reason, sizeof(reason), "Write watchpoint 0x%08x (PC=0x%08x)", wp, c->current_pc);
            debugger_handle_break(dbg, cpu, reason);
            return;
        }
    }
}

// ============================================================================
// Break Handler
// ============================================================================

void debugger_handle_break(Debugger* dbg, struct Cpu* cpu, const char* reason) {
    (void)cpu;
    dbg->paused = true;
    /* May flip dbg->paused back to false via emu.resume() — lets a script
     * count hits and only really halt on a specific one. No-op, zero
     * behavior change, when no Lua on_break callback is registered. */
    lua_debug_dispatch_break(reason);
}
