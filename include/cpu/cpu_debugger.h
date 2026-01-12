#ifndef CPU_DEBUGGER_H
#define CPU_DEBUGGER_H

#include "cpu_types.h"
#include <stdbool.h>
#include <stdint.h>

// ============================================================
// Breakpoint Types
// ============================================================

typedef enum {
    BREAKPOINT_TYPE_EXECUTE,    // Break on instruction execution
    BREAKPOINT_TYPE_READ,       // Break on memory read
    BREAKPOINT_TYPE_WRITE       // Break on memory write
} BreakpointType;

// ============================================================
// Breakpoint Structure
// ============================================================

typedef struct {
    uint32_t address;           // Address to break on
    BreakpointType type;        // Type of breakpoint
    bool enabled;               // Whether breakpoint is active
    uint32_t hit_count;         // Number of times hit
    bool auto_clear;            // Clear after first hit
} Breakpoint;

// ============================================================
// Breakpoint Callback
// ============================================================

/**
 * @brief Callback function for breakpoint hits
 * @param type Type of breakpoint that was hit
 * @param address Address where breakpoint was hit
 * @param data User data passed to callback
 * @return true to continue execution, false to stop
 */
typedef bool (*BreakpointCallback)(BreakpointType type, uint32_t address, void* data);

// ============================================================
// Debugger API
// ============================================================

/**
 * @brief Initialize the debugger system
 */
void cpu_debugger_init(void);

/**
 * @brief Shutdown the debugger system
 */
void cpu_debugger_shutdown(void);

/**
 * @brief Add a breakpoint
 * @param type Type of breakpoint
 * @param address Address to break on
 * @param auto_clear Whether to clear after first hit
 * @param enabled Whether breakpoint starts enabled
 * @return true if breakpoint was added successfully
 */
bool cpu_debugger_add_breakpoint(BreakpointType type, uint32_t address, bool auto_clear, bool enabled);

/**
 * @brief Add a breakpoint with callback
 * @param type Type of breakpoint
 * @param address Address to break on
 * @param callback Function to call when breakpoint is hit
 * @param data User data to pass to callback
 * @return true if breakpoint was added successfully
 */
bool cpu_debugger_add_breakpoint_with_callback(BreakpointType type, uint32_t address,
                                               BreakpointCallback callback, void* data);

/**
 * @brief Remove a breakpoint
 * @param type Type of breakpoint
 * @param address Address of breakpoint to remove
 * @return true if breakpoint was removed
 */
bool cpu_debugger_remove_breakpoint(BreakpointType type, uint32_t address);

/**
 * @brief Enable/disable a breakpoint
 * @param type Type of breakpoint
 * @param address Address of breakpoint
 * @param enabled Whether to enable or disable
 * @return true if breakpoint was found and updated
 */
bool cpu_debugger_set_breakpoint_enabled(BreakpointType type, uint32_t address, bool enabled);

/**
 * @brief Clear all breakpoints
 */
void cpu_debugger_clear_breakpoints(void);

/**
 * @brief Check if any breakpoints are set
 * @return true if any breakpoints exist
 */
bool cpu_debugger_has_any_breakpoints(void);

/**
 * @brief Check if breakpoint exists at address
 * @param type Type of breakpoint
 * @param address Address to check
 * @return true if breakpoint exists
 */
bool cpu_debugger_has_breakpoint_at_address(BreakpointType type, uint32_t address);

/**
 * @brief Set single step mode (break after each instruction)
 */
void cpu_debugger_set_single_step_flag(void);

/**
 * @brief Check if single step mode is enabled
 * @return true if single stepping
 */
bool cpu_debugger_is_single_stepping(void);

/**
 * @brief Clear single step mode
 */
void cpu_debugger_clear_single_step_flag(void);

/**
 * @brief Check if execution should break at current PC
 * @param pc Current program counter
 * @return true if execution should break
 */
bool cpu_debugger_should_break(uint32_t pc);

/**
 * @brief Check if memory access should break
 * @param type Type of access (read/write)
 * @param address Address being accessed
 * @return true if access should break
 */
bool cpu_debugger_should_break_memory(BreakpointType type, uint32_t address);

/**
 * @brief Get breakpoint type name as string
 * @param type Breakpoint type
 * @return String representation
 */
const char* cpu_debugger_get_breakpoint_type_name(BreakpointType type);

#endif // CPU_DEBUGGER_H