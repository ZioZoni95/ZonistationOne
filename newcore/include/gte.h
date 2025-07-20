#ifndef NEWCORE_GTE_H
#define NEWCORE_GTE_H

#include <stdint.h>
#include <stdbool.h>

// GTE state structure for newcore
typedef struct {
    int32_t data[32];
    int32_t control[32];
    bool busy;
    uint32_t cycles_remaining;
} NcGte;

// Initialize GTE
void nc_gte_init(NcGte* gte);

// GTE instruction dispatcher
uint32_t nc_gte_execute_instruction(NcGte* gte, uint32_t instruction);

#endif // NEWCORE_GTE_H 