#ifndef SAVESTATE_H
#define SAVESTATE_H

#include <stdbool.h>

struct Cpu;
struct Interconnect;

/* Full-machine savestate.
 *
 * Everything that decides what the machine does next is written: CPU (with the
 * GTE and the I-cache), RAM, scratchpad, the interrupt controller, the event
 * queue, GPU state and VRAM, DMA, timers, CDROM, SPU with its RAM, SIO and the
 * MDEC. What is deliberately not written is anything the host owns rather than
 * the guest — GL objects, threads, file handles, the debugger's breakpoints —
 * because those belong to the running process, not to the emulated state.
 *
 * Section sizes are stored and checked on load. A struct that changed shape
 * since the state was written is refused with a message instead of loaded into
 * a mismatched layout, which is the failure mode that costs a debugging
 * session to recognise. */

#define SAVESTATE_DEFAULT_PATH "savestates/slot0.zst"

bool savestate_save(const char* path, struct Cpu* cpu, struct Interconnect* inter);
bool savestate_load(const char* path, struct Cpu* cpu, struct Interconnect* inter);

#endif /* SAVESTATE_H */
