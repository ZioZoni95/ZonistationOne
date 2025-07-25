// cpu_control.h
// Migrated from cpu.c: exception, BIOS, and CPU management logic (header)
// TODO: Move exception/BIOS/CPU management declarations here.

#ifndef CPU_CONTROL_H
#define CPU_CONTROL_H

#include "cpu_core.h"
#include <stdint.h>

// --- Exception Handling ---
void cpu_control_handle_exception(CpuState* cpu, uint32_t cause, uint32_t bad_addr);

// --- BIOS Boot Flow ---
void cpu_control_boot_bios(CpuState* cpu);

// --- CPU State Management ---
void cpu_control_reset(CpuState* cpu);

#endif // CPU_CONTROL_H 