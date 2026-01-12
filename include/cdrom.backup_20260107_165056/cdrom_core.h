#ifndef CDROM_CORE_H
#define CDROM_CORE_H

/**
 * @file cdrom_core.h
 * @brief CDROM Controller Public API
 * 
 * Thread-safe CDROM controller interface for PlayStation 1 emulator.
 * All functions are protected by internal mutex for multi-threaded safety.
 * 
 * Usage Example:
 * ```c
 * CdromState cdrom;
 * cdrom_init(&cdrom);
 * cdrom_load_disc(&cdrom, "game.cue");
 * 
 * // Register access (thread-safe)
 * uint8_t status = cdrom_read_register(&cdrom, 0);
 * cdrom_write_register(&cdrom, 1, 0x01); // Send GETSTAT command
 * 
 * // Event handling (called by event scheduler)
 * cdrom_command_event(&cdrom, 0);
 * ```
 * 
 * Computational Complexity Guarantees:
 * - cdrom_init: O(1)
 * - cdrom_read_register: O(1) - direct array access
 * - cdrom_write_register: O(1) - direct state update
 * - cdrom_push_param: O(1) - FIFO append
 * - cdrom_pop_response: O(1) - FIFO pop
 * - cdrom_execute_command: O(1) - function pointer dispatch
 * - cdrom_read_sector: O(1) - single file seek + read
 */

#include "cdrom/cdrom_types.h"
#include <stdint.h>
#include <stdbool.h>

// Forward declarations
struct Interconnect;

// ============================================================================
// Lifecycle Management
// ============================================================================

/**
 * @brief Initialize CDROM controller
 * 
 * Sets up initial state, creates mutex, resets all registers.
 * Complexity: O(1)
 * Thread-safety: Call once from main thread before multi-threading starts
 * 
 * @param cdrom CDROM state structure
 */
void cdrom_init(CdromState* cdrom);

/**
 * @brief Shutdown CDROM controller
 * 
 * Closes disc file, destroys mutex, prints statistics.
 * Complexity: O(1)
 * Thread-safety: Call once from main thread after all threads stopped
 * 
 * @param cdrom CDROM state structure
 */
void cdrom_shutdown(CdromState* cdrom);

/**
 * @brief Reset CDROM controller to initial state
 * 
 * Preserves disc and file handle, resets all other state.
 * Complexity: O(1)
 * Thread-safety: Thread-safe (mutex protected)
 * 
 * @param cdrom CDROM state structure
 */
void cdrom_reset(CdromState* cdrom);

// ============================================================================
// Disc Management
// ============================================================================

/**
 * @brief Load disc from CUE file
 * 
 * Parses CUE sheet, opens BIN file, reads TOC.
 * Complexity: O(n) where n = CUE file size (typically < 1KB)
 * Thread-safety: Thread-safe (mutex protected)
 * 
 * @param cdrom CDROM state structure
 * @param cue_path Path to CUE file
 * @return true on success, false on error
 */
bool cdrom_load_disc(CdromState* cdrom, const char* cue_path);

/**
 * @brief Eject disc
 * 
 * Closes disc file, resets disc-related state.
 * Complexity: O(1)
 * Thread-safety: Thread-safe (mutex protected)
 * 
 * @param cdrom CDROM state structure
 */
void cdrom_eject_disc(CdromState* cdrom);

/**
 * @brief Check if disc is present
 * 
 * Complexity: O(1)
 * Thread-safety: Thread-safe (mutex protected)
 * 
 * @param cdrom CDROM state structure
 * @return true if disc loaded, false otherwise
 */
bool cdrom_has_disc(const CdromState* cdrom);

// ============================================================================
// Hardware Register Access (PSX-SPX: CDROM Controller I/O Ports)
// ============================================================================

/**
 * @brief Read CDROM register
 * 
 * Handles bank-switched register access:
 * - Offset 0: HSTS (status register)
 * - Offset 1: RESULT (response FIFO, all banks)
 * - Offset 2: RDDATA (sector data, all banks)
 * - Offset 3: HINTSTS (interrupt status) or HINTMSK (interrupt mask)
 * 
 * Complexity: O(1) - switch statement with direct access
 * Thread-safety: Thread-safe (mutex protected)
 * 
 * @param cdrom CDROM state structure
 * @param offset Register offset (0-3)
 * @return Register value (8-bit)
 */
uint8_t cdrom_read_register(CdromState* cdrom, uint32_t offset);

/**
 * @brief Write CDROM register
 * 
 * Handles bank-switched register access:
 * - Offset 0: ADDRESS (bank select, all banks)
 * - Offset 1: COMMAND (bank 0), WRDATA (bank 1), CI (bank 2), ATV2 (bank 3)
 * - Offset 2: PARAMETER (bank 0), HINTMSK (bank 1), ATV0 (bank 2), ATV3 (bank 3)
 * - Offset 3: HCHPCTL (bank 0), HCLRCTL (bank 1), ATV1 (bank 2), ADPCTL (bank 3)
 * 
 * Complexity: O(1) - switch statement with direct state update
 * Thread-safety: Thread-safe (mutex protected)
 * 
 * @param cdrom CDROM state structure
 * @param inter Interconnect pointer (for command execution)
 * @param offset Register offset (0-3)
 * @param value Value to write (8-bit)
 */
void cdrom_write_register(CdromState* cdrom, struct Interconnect* inter, uint32_t offset, uint8_t value);

// ============================================================================
// DMA Interface
// ============================================================================

/**
 * @brief DMA read from sector buffer
 * 
 * Reads 32-bit words from current sector buffer for DMA transfer.
 * Used by DMA channel 3 (CDROM -> RAM).
 * 
 * Complexity: O(n) where n = word_count (typically 512 for 2KB sector)
 * Thread-safety: Thread-safe (mutex protected)
 * 
 * @param cdrom CDROM state structure
 * @param words Output buffer for 32-bit words
 * @param word_count Number of words to read
 */
void cdrom_dma_read(CdromState* cdrom, uint32_t* words, uint32_t word_count);

// ============================================================================
// Event Callbacks (called by event scheduler)
// ============================================================================

/**
 * @brief Command execution event
 * 
 * Called by event scheduler when command processing delay expires.
 * Executes pending command and generates appropriate response.
 * 
 * Complexity: O(1) - command dispatch via function pointer table
 * Thread-safety: Thread-safe (mutex protected)
 * 
 * @param cdrom CDROM state structure
 * @param cycles_late How many cycles late the event fired
 */
void cdrom_command_event(CdromState* cdrom, uint32_t cycles_late);

/**
 * @brief Drive state machine event
 * 
 * Called by event scheduler for drive operations (seeking, reading, playing).
 * Advances drive state machine, reads sectors, generates interrupts.
 * 
 * Complexity: O(1) - state machine switch + single sector read
 * Thread-safety: Thread-safe (mutex protected)
 * 
 * @param cdrom CDROM state structure
 * @param cycles_late How many cycles late the event fired
 */
void cdrom_drive_event(CdromState* cdrom, uint32_t cycles_late);

/**
 * @brief Interrupt delivery event
 * 
 * Called by event scheduler to deliver pending CDROM interrupt to CPU.
 * Sets interrupt flag and triggers IRQ_CDROM line.
 * 
 * Complexity: O(1)
 * Thread-safety: Thread-safe (mutex protected)
 * 
 * @param cdrom CDROM state structure
 * @param inter Interconnect for IRQ triggering
 * @param cycles_late How many cycles late the event fired
 */
void cdrom_interrupt_event(CdromState* cdrom, struct Interconnect* inter, uint32_t cycles_late);

// ============================================================================
// Audio Output (for SPU integration)
// ============================================================================

/**
 * @brief Get next audio frame from CD-DA or XA-ADPCM
 * 
 * Returns stereo audio samples for SPU mixing.
 * Call at 44.1kHz rate for CD-DA, 18.9kHz or 37.8kHz for XA-ADPCM.
 * 
 * Complexity: O(1) - read from ring buffer
 * Thread-safety: Thread-safe (mutex protected)
 * 
 * @param cdrom CDROM state structure
 * @param left_out Left channel sample output (-32768 to 32767)
 * @param right_out Right channel sample output (-32768 to 32767)
 * @return true if samples available, false if buffer empty
 */
bool cdrom_get_audio_frame(CdromState* cdrom, int16_t* left_out, int16_t* right_out);

// ============================================================================
// FIFO Operations (O(1) complexity)
// ============================================================================

/**
 * @brief Push byte to parameter FIFO
 * 
 * Used internally by register write (PARAMETER register).
 * Complexity: O(1) - circular buffer append
 * 
 * @param cdrom CDROM state structure
 * @param value Byte to push
 * @return true on success, false if FIFO full
 */
bool cdrom_push_param(CdromState* cdrom, uint8_t value);

/**
 * @brief Pop byte from parameter FIFO
 * 
 * Used internally by command handlers to read parameters.
 * Complexity: O(1) - circular buffer pop
 * 
 * @param cdrom CDROM state structure
 * @param value_out Popped byte output
 * @return true on success, false if FIFO empty
 */
bool cdrom_pop_param(CdromState* cdrom, uint8_t* value_out);

/**
 * @brief Push byte to response FIFO
 * 
 * Used internally by command handlers to send responses.
 * Complexity: O(1) - circular buffer append
 * 
 * @param cdrom CDROM state structure
 * @param value Byte to push
 * @return true on success, false if FIFO full
 */
bool cdrom_push_response(CdromState* cdrom, uint8_t value);

/**
 * @brief Pop byte from response FIFO
 * 
 * Used internally by register read (RESULT register).
 * Complexity: O(1) - circular buffer pop
 * 
 * @param cdrom CDROM state structure
 * @param value_out Popped byte output
 * @return true on success, false if FIFO empty
 */
bool cdrom_pop_response(CdromState* cdrom, uint8_t* value_out);

/**
 * @brief Clear parameter FIFO
 * 
 * Used by HCLRCTL register (bit 6).
 * Complexity: O(1) - reset head/tail/count
 * 
 * @param cdrom CDROM state structure
 */
void cdrom_clear_param_fifo(CdromState* cdrom);

/**
 * @brief Clear response FIFO
 * 
 * Used internally after interrupt acknowledgment.
 * Complexity: O(1) - reset head/tail/count
 * 
 * @param cdrom CDROM state structure
 */
void cdrom_clear_response_fifo(CdromState* cdrom);

// ============================================================================
// Interrupt Management
// ============================================================================

/**
 * @brief Trigger CDROM interrupt
 * 
 * Sets interrupt flag register, requests IRQ_CDROM via interconnect.
 * Complexity: O(1)
 * Thread-safety: Thread-safe (mutex protected)
 * 
 * @param cdrom CDROM state structure
 * @param inter Interconnect for IRQ signaling
 * @param interrupt_type Interrupt type (INT1-INT5)
 */
void cdrom_trigger_interrupt(CdromState* cdrom, struct Interconnect* inter, CdromInterrupt interrupt_type);

/**
 * @brief Check if interrupt is pending
 * 
 * Complexity: O(1)
 * Thread-safety: Thread-safe (mutex protected)
 * 
 * @param cdrom CDROM state structure
 * @return true if interrupt pending, false otherwise
 */
bool cdrom_has_pending_interrupt(const CdromState* cdrom);

/**
 * @brief Acknowledge interrupt
 * 
 * Clears interrupt flag register (bits 0-2).
 * Called when CPU writes to HCLRCTL register.
 * Complexity: O(1)
 * Thread-safety: Thread-safe (mutex protected)
 * 
 * @param cdrom CDROM state structure
 * @param ack_mask Bits to acknowledge (typically 0x07 for all)
 */
void cdrom_acknowledge_interrupt(CdromState* cdrom, uint8_t ack_mask);

// ============================================================================
// Command Execution (internal, called by command_event)
// ============================================================================

/**
 * @brief Execute CDROM command
 * 
 * Dispatches to specific command handler based on command byte.
 * Generates INT3 (ACK) or INT5 (ERROR) response.
 * 
 * Complexity: O(1) - function pointer dispatch
 * Thread-safety: Thread-safe (mutex protected)
 * 
 * @param cdrom CDROM state structure
 * @param inter Interconnect for event scheduling and IRQ
 * @param command Command byte (0x00-0x1F)
 */
void cdrom_execute_command(CdromState* cdrom, struct Interconnect* inter, uint8_t command);

// ============================================================================
// Sector Reading (internal, called by drive_event)
// ============================================================================

/**
 * @brief Read sector from disc
 * 
 * Reads 2352-byte raw sector from BIN file at current LBA.
 * Stores in sector buffer for CPU/DMA access.
 * 
 * Complexity: O(1) - single fseek + fread
 * Thread-safety: Thread-safe (mutex protected)
 * 
 * @param cdrom CDROM state structure
 * @return true on success, false on read error
 */
bool cdrom_read_sector(CdromState* cdrom);

/**
 * @brief Get current secondary status byte
 * 
 * Builds status byte from drive state flags.
 * Complexity: O(1) - bitwise OR of flags
 * 
 * @param cdrom CDROM state structure
 * @return Status byte (STAT_* flags)
 */
uint8_t cdrom_get_status_byte(const CdromState* cdrom);

// ============================================================================
// Debug and Statistics
// ============================================================================

/**
 * @brief Print CDROM statistics
 * 
 * Displays command count, sector reads, interrupts, etc.
 * Called by cdrom_shutdown.
 * Complexity: O(1)
 * 
 * @param cdrom CDROM state structure
 */
void cdrom_print_stats(const CdromState* cdrom);

/**
 * @brief Enable/disable debug logging
 * 
 * Complexity: O(1)
 * Thread-safety: Thread-safe (mutex protected)
 * 
 * @param cdrom CDROM state structure
 * @param enabled true to enable logging, false to disable
 */
void cdrom_set_logging(CdromState* cdrom, bool enabled);

#endif // CDROM_CORE_H
