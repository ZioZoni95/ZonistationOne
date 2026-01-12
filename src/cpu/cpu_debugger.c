#include "cpu/cpu_debugger.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================
// Internal Structures
// ============================================================

#define MAX_BREAKPOINTS 64

typedef struct {
    Breakpoint breakpoint;
    BreakpointCallback callback;
    void* callback_data;
    bool has_callback;
} BreakpointEntry;

static struct {
    BreakpointEntry breakpoints[MAX_BREAKPOINTS];
    int num_breakpoints;
    bool single_step;
} debugger_state = {0};

// ============================================================
// Internal Helper Functions
// ============================================================

static int find_breakpoint_index(BreakpointType type, uint32_t address) {
    for (int i = 0; i < debugger_state.num_breakpoints; i++) {
        BreakpointEntry* entry = &debugger_state.breakpoints[i];
        if (entry->breakpoint.type == type && entry->breakpoint.address == address) {
            return i;
        }
    }
    return -1;
}

static bool add_breakpoint_internal(BreakpointType type, uint32_t address, bool auto_clear,
                                   bool enabled, BreakpointCallback callback, void* data) {
    if (debugger_state.num_breakpoints >= MAX_BREAKPOINTS) {
        LOG_ERROR("Maximum number of breakpoints reached (%d)", MAX_BREAKPOINTS);
        return false;
    }

    // Check if breakpoint already exists
    if (find_breakpoint_index(type, address) >= 0) {
        LOG_WARN("Breakpoint already exists at 0x%08X type %d", address, type);
        return false;
    }

    BreakpointEntry* entry = &debugger_state.breakpoints[debugger_state.num_breakpoints++];
    entry->breakpoint.address = address;
    entry->breakpoint.type = type;
    entry->breakpoint.enabled = enabled;
    entry->breakpoint.hit_count = 0;
    entry->breakpoint.auto_clear = auto_clear;
    entry->callback = callback;
    entry->callback_data = data;
    entry->has_callback = (callback != NULL);

    LOG_INFO("Added %s breakpoint at 0x%08X", cpu_debugger_get_breakpoint_type_name(type), address);
    return true;
}

// ============================================================
// Public API Implementation
// ============================================================

void cpu_debugger_init(void) {
    memset(&debugger_state, 0, sizeof(debugger_state));
    LOG_INFO("CPU debugger initialized");
}

void cpu_debugger_shutdown(void) {
    cpu_debugger_clear_breakpoints();
    LOG_INFO("CPU debugger shutdown");
}

bool cpu_debugger_add_breakpoint(BreakpointType type, uint32_t address, bool auto_clear, bool enabled) {
    return add_breakpoint_internal(type, address, auto_clear, enabled, NULL, NULL);
}

bool cpu_debugger_add_breakpoint_with_callback(BreakpointType type, uint32_t address,
                                               BreakpointCallback callback, void* data) {
    return add_breakpoint_internal(type, address, false, true, callback, data);
}

bool cpu_debugger_remove_breakpoint(BreakpointType type, uint32_t address) {
    int index = find_breakpoint_index(type, address);
    if (index < 0) {
        return false;
    }

    // Shift remaining breakpoints down
    memmove(&debugger_state.breakpoints[index],
            &debugger_state.breakpoints[index + 1],
            (debugger_state.num_breakpoints - index - 1) * sizeof(BreakpointEntry));
    debugger_state.num_breakpoints--;

    LOG_INFO("Removed %s breakpoint at 0x%08X", cpu_debugger_get_breakpoint_type_name(type), address);
    return true;
}

bool cpu_debugger_set_breakpoint_enabled(BreakpointType type, uint32_t address, bool enabled) {
    int index = find_breakpoint_index(type, address);
    if (index < 0) {
        return false;
    }

    debugger_state.breakpoints[index].breakpoint.enabled = enabled;
    LOG_INFO("%s %s breakpoint at 0x%08X",
             enabled ? "Enabled" : "Disabled",
             cpu_debugger_get_breakpoint_type_name(type), address);
    return true;
}

void cpu_debugger_clear_breakpoints(void) {
    debugger_state.num_breakpoints = 0;
    debugger_state.single_step = false;
    LOG_INFO("Cleared all breakpoints");
}

bool cpu_debugger_has_any_breakpoints(void) {
    return debugger_state.num_breakpoints > 0 || debugger_state.single_step;
}

bool cpu_debugger_has_breakpoint_at_address(BreakpointType type, uint32_t address) {
    return find_breakpoint_index(type, address) >= 0;
}

void cpu_debugger_set_single_step_flag(void) {
    debugger_state.single_step = true;
    LOG_DEBUG("Single step mode enabled");
}

bool cpu_debugger_is_single_stepping(void) {
    return debugger_state.single_step;
}

void cpu_debugger_clear_single_step_flag(void) {
    debugger_state.single_step = false;
    LOG_DEBUG("Single step mode disabled");
}

bool cpu_debugger_should_break(uint32_t pc) {
    // Check single step first
    if (debugger_state.single_step) {
        debugger_state.single_step = false; // Clear after one step
        LOG_DEBUG("Single step breakpoint hit at 0x%08X", pc);
        return true;
    }

    // Check execute breakpoints
    for (int i = 0; i < debugger_state.num_breakpoints; i++) {
        BreakpointEntry* entry = &debugger_state.breakpoints[i];
        if (entry->breakpoint.type == BREAKPOINT_TYPE_EXECUTE &&
            entry->breakpoint.address == pc &&
            entry->breakpoint.enabled) {

            entry->breakpoint.hit_count++;

            LOG_INFO("Execute breakpoint hit at 0x%08X (hit count: %u)",
                    pc, entry->breakpoint.hit_count);

            // Call callback if present
            if (entry->has_callback) {
                if (!entry->callback(entry->breakpoint.type, pc, entry->callback_data)) {
                    // Callback requested to remove breakpoint
                    cpu_debugger_remove_breakpoint(entry->breakpoint.type, pc);
                    return true;
                }
            }

            // Auto-clear if requested
            if (entry->breakpoint.auto_clear) {
                cpu_debugger_remove_breakpoint(entry->breakpoint.type, pc);
            }

            return true;
        }
    }

    return false;
}

bool cpu_debugger_should_break_memory(BreakpointType type, uint32_t address) {
    if (type != BREAKPOINT_TYPE_READ && type != BREAKPOINT_TYPE_WRITE) {
        return false;
    }

    for (int i = 0; i < debugger_state.num_breakpoints; i++) {
        BreakpointEntry* entry = &debugger_state.breakpoints[i];
        if (entry->breakpoint.type == type &&
            entry->breakpoint.address == address &&
            entry->breakpoint.enabled) {

            entry->breakpoint.hit_count++;

            LOG_INFO("%s breakpoint hit at 0x%08X (hit count: %u)",
                    cpu_debugger_get_breakpoint_type_name(type),
                    address, entry->breakpoint.hit_count);

            // Call callback if present
            if (entry->has_callback) {
                if (!entry->callback(entry->breakpoint.type, address, entry->callback_data)) {
                    // Callback requested to remove breakpoint
                    cpu_debugger_remove_breakpoint(entry->breakpoint.type, address);
                    return true;
                }
            }

            // Auto-clear if requested
            if (entry->breakpoint.auto_clear) {
                cpu_debugger_remove_breakpoint(entry->breakpoint.type, address);
            }

            return true;
        }
    }

    return false;
}

const char* cpu_debugger_get_breakpoint_type_name(BreakpointType type) {
    switch (type) {
        case BREAKPOINT_TYPE_EXECUTE: return "execute";
        case BREAKPOINT_TYPE_READ:    return "read";
        case BREAKPOINT_TYPE_WRITE:   return "write";
        default:                      return "unknown";
    }
}