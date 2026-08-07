/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#include "interconnect.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "log.h"
#include "dma.h"
#include "mdec.h"
#include "gpu.h"
#include "ram.h"
#include "cpu.h"
#include "debugger.h"

// --- Initialization ---
/**
 * @brief Initializes the Interconnect struct.
 * Assigns component pointers, initializes embedded structs (GPU, DMA),
 * and resets interrupt controller state.
 * @param inter Pointer to the Interconnect struct.
 * @param bios Pointer to the loaded Bios struct.
 * @param ram Pointer to the initialized Ram struct.
 */
void interconnect_init(Interconnect* inter, Bios* bios, Ram* ram) {
    LOG_INTERCONNECT_DEBUG("[SYSTEM] Interconnect initialized");
    inter->bios = bios;
    inter->ram = ram;
    inter->cpu = NULL; // Will be set later via interconnect_set_cpu()
    dma_init(&inter->dma, inter); // Initialize DMA controller state
    gpu_init_full(&inter->gpu, inter); // Initialize GPU state (now contains Renderer)

    // Initialize Scratchpad (1KB fast RAM)
    memset(inter->scratchpad, 0, SCRATCHPAD_SIZE);

    cdrom_init(&inter->cdrom,inter);
    // Initialize Interrupt Controller state
    inter->irq_status = 0;     // No pending interrupts (I_STAT)
    inter->irq_mask = 0;       // Mask all IRQs at startup (I_MASK)
    inter->irq_line_state = 0; // No IRQ lines active (for edge detection)

    // Initialize Timer state
    timers_init(&inter->timers_state, inter);

    // Initialize SIO (Controller and Memory Card)
    sio_init(&inter->sio);
    sio_set_interconnect(&inter->sio, inter);  // wire back-pointer for deferred transfers
    sio_load_memcard(&inter->sio.card_slot1, "memcard1.mcd");
    sio_load_memcard(&inter->sio.card_slot2, "memcard2.mcd");
    mdec_init(&inter->mdec);
    // Initialize SPU
    spu_init(&inter->spu);

    // Initialize BIOS TTY line buffer
    inter->tty_line_len = 0;
    inter->tty_line_buf[0] = '\0';
    inter->tty_duart_seen = false;

    // Initialize BIOS TTY input buffer (for kernel getc support)
    inter->tty_input_read_idx = 0;
    inter->tty_input_write_idx = 0;
    memset(inter->tty_input_buf, 0, sizeof(inter->tty_input_buf));

    debugger_init(&inter->debugger);
    bus_hw_tables_init();
    bus_memctrl_init(inter);

    LOG_INTERCONNECT_DEBUG("[SYSTEM] Interconnect Initialized (BIOS, RAM, DMA, GPU, CDROM, SIO, Timers, IRQ states set).");
}

/**
 * @brief Sets the CPU pointer for direct exception triggering.
 * Called after CPU initialization to establish bidirectional reference.
 * @param inter Pointer to the Interconnect instance.
 * @param cpu Pointer to the CPU instance.
 */
void interconnect_set_cpu(Interconnect* inter, struct Cpu* cpu) {
    inter->cpu = cpu;
    LOG_INTERCONNECT_DEBUG("[SYSTEM] Interconnect CPU pointer set (exception triggering enabled).");
}

// --- TTY Input Buffer Management (for kernel getc support) ---
/**
 * @brief Add a character to the TTY input buffer
 * Called by main.c when reading keyboard input to be available for BIOS getc
 */
void interconnect_tty_char(Interconnect* inter, char ch, bool from_duart) {
    if (!inter) return;

    /* The BIOS puts its own printf output on the DUART, so from the moment that
     * port carries a byte the syscall side-channel is a second copy of the same
     * text. Muting it there keeps the early banner — printed before the DUART is
     * up, and visible only to the syscall hook — without printing everything
     * after it twice. */
    if (from_duart) inter->tty_duart_seen = true;
    else if (inter->tty_duart_seen) return;

    uint8_t b = (uint8_t)ch;
    if (ch == '\n' || ch == '\r') {
        if (inter->tty_line_len > 0) {
            inter->tty_line_buf[inter->tty_line_len] = '\0';
            log_print_tty(inter->tty_line_buf);
        }
        inter->tty_line_len = 0;
    } else if (b >= 0x20 && b < 0x7F) {
        /* Printable ASCII only — control characters would corrupt the log. */
        if (inter->tty_line_len < (int)(sizeof(inter->tty_line_buf) - 1))
            inter->tty_line_buf[inter->tty_line_len++] = ch;
    }
}

void interconnect_tty_input_add(Interconnect* inter, char ch) {
    if (!inter) return;
    // Circular buffer: if full (read_idx == write_idx after increment), skip
    int next_write = (inter->tty_input_write_idx + 1) % sizeof(inter->tty_input_buf);
    if (next_write == inter->tty_input_read_idx) {
        // Buffer full, skip (overrun)
        return;
    }
    inter->tty_input_buf[inter->tty_input_write_idx] = ch;
    inter->tty_input_write_idx = next_write;
}

/**
 * @brief Read a character from the TTY input buffer (for kernel getc)
 * Returns the next character from the buffer, or -1 if empty
 */
int interconnect_tty_input_get(Interconnect* inter) {
    if (!inter) return -1;
    if (inter->tty_input_read_idx == inter->tty_input_write_idx) {
        // Buffer empty
        return -1;
    }
    int ch = (unsigned char)inter->tty_input_buf[inter->tty_input_read_idx];
    inter->tty_input_read_idx = (inter->tty_input_read_idx + 1) % sizeof(inter->tty_input_buf);
return ch;
}
