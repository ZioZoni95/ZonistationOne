#include "zonistation_common.h"
#include "zonistation_cpu.h"

// Placeholder implementations for CPU exception handling
zs_error_t zs_cpu_trigger_exception(zs_cpu_t* cpu, zs_u32 exception_code) {
    ZS_UNUSED(cpu);
    ZS_UNUSED(exception_code);
    // Placeholder
    return ZS_SUCCESS;
}

zs_error_t zs_cpu_handle_interrupt(zs_cpu_t* cpu, zs_u32 interrupt_level) {
    ZS_UNUSED(cpu);
    ZS_UNUSED(interrupt_level);
    // Placeholder
    return ZS_SUCCESS;
}

zs_error_t zs_cpu_return_from_exception(zs_cpu_t* cpu) {
    ZS_UNUSED(cpu);
    // Placeholder
    return ZS_SUCCESS;
} 