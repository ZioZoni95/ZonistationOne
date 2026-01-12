#ifndef CPU_H
#define CPU_H

/**
 * @file cpu.h
 * @brief CPU Compatibility Wrapper - Legacy Header
 * 
 * The CPU has been refactored into a modular architecture (January 2026).
 * This file includes all modular headers for backward compatibility.
 * 
 * New Modular Architecture:
 *   - cpu/cpu_types.h       - Type definitions, enums, helpers
 *   - cpu/cpu_cache.h       - I-cache interface
 *   - cpu/cpu_exceptions.h  - Exception handling, BIOS syscalls
 *   - cpu/cpu_instructions.h - All 60+ instruction handlers
 *   - cpu/cpu_core.h        - Main CPU state and operations
 */

#include "cpu/cpu_types.h"
#include "cpu/cpu_cache.h"
#include "cpu/cpu_exceptions.h"
#include "cpu/cpu_instructions.h"
#include "cpu/cpu_core.h"

#endif // CPU_H
