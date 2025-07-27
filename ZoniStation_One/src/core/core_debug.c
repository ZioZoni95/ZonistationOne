#include "zonistation_common.h"
#include "zonistation_core.h"

// Placeholder implementations for core debugging
zs_error_t zs_core_enable_debugger(zs_core_t* core) {
    // Placeholder
    return ZS_SUCCESS;
}

zs_error_t zs_core_disable_debugger(zs_core_t* core) {
    // Placeholder
    return ZS_SUCCESS;
}

zs_error_t zs_core_set_breakpoint(zs_core_t* core, zs_u32 address) {
    // Placeholder
    return ZS_SUCCESS;
}

zs_error_t zs_core_clear_breakpoint(zs_core_t* core, zs_u32 address) {
    // Placeholder
    return ZS_SUCCESS;
}

zs_error_t zs_core_step_instruction(zs_core_t* core) {
    // Placeholder
    return ZS_SUCCESS;
}

const char* zs_core_get_last_error(const zs_core_t* core) {
    // Placeholder
    return "No error";
}

zs_error_t zs_core_clear_error(zs_core_t* core) {
    // Placeholder
    return ZS_SUCCESS;
} 