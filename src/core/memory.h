/*
 * ZonistationOne - PlayStation One Emulator
 * Memory Management System Interface
 */

#ifndef PSX_MEMORY_H
#define PSX_MEMORY_H

#include "system.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Memory interface functions */
psx_memory_t *memory_create(void);
void memory_destroy(psx_memory_t *mem);

int memory_init(psx_memory_t *mem);
void memory_shutdown(psx_memory_t *mem);
void memory_reset(psx_memory_t *mem);

/* BIOS loading */
int memory_load_bios(psx_memory_t *mem, const uint8_t *data, size_t size);

/* Memory access functions */
uint8_t memory_read8(psx_memory_t *mem, uint32_t address);
uint16_t memory_read16(psx_memory_t *mem, uint32_t address);
uint32_t memory_read32(psx_memory_t *mem, uint32_t address);

void memory_write8(psx_memory_t *mem, uint32_t address, uint8_t value);
void memory_write16(psx_memory_t *mem, uint32_t address, uint16_t value);
void memory_write32(psx_memory_t *mem, uint32_t address, uint32_t value);

/* Direct memory access (for DMA, etc.) */
void *memory_get_ptr(psx_memory_t *mem, uint32_t address, size_t size);

/* Memory mapping utilities */
uint32_t memory_translate_address(uint32_t virtual_addr);
int memory_is_valid_address(uint32_t address);

#ifdef __cplusplus
}
#endif

#endif /* PSX_MEMORY_H */