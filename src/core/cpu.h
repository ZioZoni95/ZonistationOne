/*
 * ZonistationOne - PlayStation One Emulator
 * MIPS R3000A CPU Interface
 */

#ifndef PSX_CPU_H
#define PSX_CPU_H

#include "system.h"
#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CPU interface functions */
psx_cpu_t *cpu_create(void);
void cpu_destroy(psx_cpu_t *cpu);

int cpu_init(psx_cpu_t *cpu, psx_memory_t *memory);
void cpu_shutdown(psx_cpu_t *cpu);
void cpu_reset(psx_cpu_t *cpu);

/* Execution */
int cpu_step(psx_cpu_t *cpu);
uint32_t cpu_get_cycles(psx_cpu_t *cpu);

/* Register access */
uint32_t cpu_get_register(psx_cpu_t *cpu, int reg);
void cpu_set_register(psx_cpu_t *cpu, int reg, uint32_t value);
uint32_t cpu_get_pc(psx_cpu_t *cpu);
void cpu_set_pc(psx_cpu_t *cpu, uint32_t pc);

/* Interrupt handling */
void cpu_set_interrupt(psx_cpu_t *cpu, uint32_t mask);
void cpu_clear_interrupt(psx_cpu_t *cpu, uint32_t mask);

/* Debug support */
void cpu_set_debug_mode(psx_cpu_t *cpu, int enabled);

#ifdef __cplusplus
}
#endif

#endif /* PSX_CPU_H */