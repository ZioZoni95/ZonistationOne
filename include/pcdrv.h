/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#ifndef PCDRV_H
#define PCDRV_H

#include <stdbool.h>
#include <stdint.h>
#include "cpu.h"

// Initialize PCDrv subsystem
void PCDrv_Initialize(void);

// Reset PCDrv subsystem (close files)
void PCDrv_Reset(void);

// Shutdown PCDrv subsystem
void PCDrv_Shutdown(void);

// Handle a break instruction that might be a PCDrv syscall
// Returns true if handled, false otherwise (which should trigger a normal break exception)
bool PCDrv_HandleSyscall(Cpu* cpu, uint32_t instruction);

#endif // PCDRV_H