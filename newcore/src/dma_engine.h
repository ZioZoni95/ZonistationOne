// dma_engine.h
// Migrated from dma.c: DMA controller logic (header)
// TODO: Move DMA controller declarations here.

#ifndef DMA_ENGINE_H
#define DMA_ENGINE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// --- DMA Engine State ---
typedef struct {
    // TODO: Add DMA channel state, registers, etc.
} DmaEngine;

// --- DMA Engine API ---
void dma_engine_init(DmaEngine* dma);
void dma_engine_start_transfer(DmaEngine* dma, int channel);
void dma_engine_step(DmaEngine* dma);

#endif // DMA_ENGINE_H 