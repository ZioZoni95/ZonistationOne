// sound_spu.h
// Migrated from spu.c: sound processing unit logic (header)
// TODO: Move SPU declarations here.

#ifndef SOUND_SPU_H
#define SOUND_SPU_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// --- Sound SPU State ---
typedef struct {
    // TODO: Add SPU state, buffers, etc.
} SoundSpu;

// --- Sound SPU API ---
void sound_spu_init(SoundSpu* spu);
void sound_spu_process(SoundSpu* spu);
void sound_spu_dma(SoundSpu* spu);

#endif // SOUND_SPU_H 