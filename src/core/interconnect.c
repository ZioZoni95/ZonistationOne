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
    mdec_init(&inter->mdec);
    // Initialize SPU
    spu_init(&inter->spu);

    // Initialize BIOS TTY line buffer
    inter->tty_line_len = 0;
    inter->tty_line_buf[0] = '\0';

    // Initialize BIOS TTY input buffer (for kernel getc support)
    inter->tty_input_read_idx = 0;
    inter->tty_input_write_idx = 0;
    memset(inter->tty_input_buf, 0, sizeof(inter->tty_input_buf));

    debugger_init(&inter->debugger);
    bus_hw_tables_init();

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

// --- CDROM Event Scheduler ---
// Schedule an event for the CDROM (simple callback-based timer)
typedef struct {
    void (*callback)(void*, uint32_t);
    void* context;
    uint32_t target_cycle;
    bool active;
    const char* name;
} CdromEvent;

#define MAX_CDROM_EVENTS 8
static CdromEvent cdrom_events[MAX_CDROM_EVENTS];

void interconnect_schedule_event(Interconnect* inter, uint32_t cycles,
                                 void (*callback)(void*, uint32_t), void* context,
                                 const char* name) {
    uint32_t target = inter->cpu_cycle_counter + cycles;

    // Find free slot
    for (int i = 0; i < MAX_CDROM_EVENTS; i++) {
        if (!cdrom_events[i].active) {
            cdrom_events[i].callback = callback;
            cdrom_events[i].context = context;
            cdrom_events[i].target_cycle = target;
            cdrom_events[i].active = true;
            cdrom_events[i].name = name;
            static uint32_t evt_sched_count = 0;
            if (++evt_sched_count <= 10 || evt_sched_count % 50 == 0) {
                LOG_CDROM_DEBUG("[SYSTEM] Scheduled #%u: %s for cycle %u (now=%u, delay=%u)",
                         evt_sched_count, name, target, inter->cpu_cycle_counter, cycles);
            }
            return;
        }
    }
    LOG_CDROM_ERROR("[SYSTEM] No free event slots for %s!", name);
}

// Called by main loop to check/fire CDROM events
void interconnect_check_cdrom_events(Interconnect* inter) {
    for (int i = 0; i < MAX_CDROM_EVENTS; i++) {
        if (cdrom_events[i].active) {
            if (inter->cpu_cycle_counter >= cdrom_events[i].target_cycle) {
                cdrom_events[i].active = false;
                uint32_t cycles_late = inter->cpu_cycle_counter - cdrom_events[i].target_cycle;
                static uint32_t evt_fire_count = 0;
                if (++evt_fire_count <= 10 || evt_fire_count % 50 == 0) {
                    LOG_SYSTEM_DEBUG("[SYSTEM] Firing #%u: %s (late=%u, target=%u, now=%u)",
                             evt_fire_count, cdrom_events[i].name, cycles_late,
                             cdrom_events[i].target_cycle, inter->cpu_cycle_counter);
                }
                cdrom_events[i].callback(cdrom_events[i].context, cycles_late);
            }
        }
    }
}

// --- TTY Input Buffer Management (for kernel getc support) ---
/**
 * @brief Add a character to the TTY input buffer
 * Called by main.c when reading keyboard input to be available for BIOS getc
 */
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
