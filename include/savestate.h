/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
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

/* A picture of what was on screen, for the slot tiles: 160x120, RGB, written beside
 * the state as <slot>.thm ("ZTH1", two little-endian u16 for width and height, then
 * the pixels). It is a sidecar and not a section of the state, so the state format
 * (v12) is untouched and a state with no picture loads exactly as before.
 * savestate_save() calls it after a successful write; it is best effort. */
#define SAVESTATE_THUMB_W 160
#define SAVESTATE_THUMB_H 120
bool savestate_thumbnail_path(const char* state_path, char* out, unsigned long n);
bool savestate_write_thumbnail(const char* state_path, const struct Interconnect* inter);

#endif /* SAVESTATE_H */
