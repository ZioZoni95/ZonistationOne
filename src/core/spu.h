/*
 * ZonistationOne - PlayStation One Emulator
 * SPU Interface
 */

#ifndef PSX_SPU_H
#define PSX_SPU_H

#include "system.h"
#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SPU interface functions */
psx_spu_t *spu_create(void);
void spu_destroy(psx_spu_t *spu);

int spu_init(psx_spu_t *spu, psx_memory_t *memory);
void spu_shutdown(psx_spu_t *spu);
void spu_reset(psx_spu_t *spu);

/* Execution */
int spu_step(psx_spu_t *spu, uint32_t cycles);

/* Register access */
uint16_t spu_read_register(psx_spu_t *spu, uint32_t address);
void spu_write_register(psx_spu_t *spu, uint32_t address, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif /* PSX_SPU_H */