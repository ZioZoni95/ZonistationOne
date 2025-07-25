// gte_unit.h
// Migrated from gte.c: geometry transformation engine logic (header)
// TODO: Move GTE declarations here.

#ifndef GTE_UNIT_H
#define GTE_UNIT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// --- GTE Unit State ---
typedef struct {
    // TODO: Add GTE state, registers, etc.
} GteUnit;

// --- GTE Unit API ---
void gte_unit_init(GteUnit* gte);
void gte_unit_execute(GteUnit* gte, uint32_t instruction);

#endif // GTE_UNIT_H 