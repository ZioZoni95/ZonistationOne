// mdec_unit.h
// Migrated from mdec.c: motion decompression logic (header)
// TODO: Move MDEC declarations here.

#ifndef MDEC_UNIT_H
#define MDEC_UNIT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// --- MDEC Unit State ---
typedef struct {
    // TODO: Add MDEC state, registers, etc.
} MdecUnit;

// --- MDEC Unit API ---
void mdec_unit_init(MdecUnit* mdec);
void mdec_unit_execute(MdecUnit* mdec, uint32_t instruction);

#endif // MDEC_UNIT_H 