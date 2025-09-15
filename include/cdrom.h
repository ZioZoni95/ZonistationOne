/**
 * cdrom.h
 * Header file for the PlayStation CD-ROM Drive emulation.
 */
#ifndef CDROM_H
#define CDROM_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h> // For FILE* type

// Forward declaration
struct Interconnect;
struct Cdrom;

// --- PSX-SPX CDROM Register Layout ---
// Bank-switched registers based on ADDRESS register (0x1F801800) bits 0-1
// All registers are 8-bit, bank switching controls functionality

// Physical addresses (offsets from 0x1F801800)
#define CDROM_REG0  0  // 0x1F801800: HSTS (read all banks) / ADDRESS (write all banks)
#define CDROM_REG1  1  // 0x1F801801: Bank-switched read/write
#define CDROM_REG2  2  // 0x1F801802: Bank-switched read/write  
#define CDROM_REG3  3  // 0x1F801803: Bank-switched read/write

// Bank-switched register functions per PSX-SPX:
// Read functions:
// Bank 0,2: HSTS, RESULT, RDDATA, HINTMSK
// Bank 1,3: HSTS, RESULT, RDDATA, HINTSTS

// Write functions:
// Bank 0: ADDRESS, COMMAND, PARAMETER, HCHPCTL
// Bank 1: ADDRESS, WRDATA, HINTMSK, HCLRCTL  
// Bank 2: ADDRESS, CI, ATV0, ATV1
// Bank 3: ADDRESS, ATV2, ATV3, ADPCTL

// --- CDROM Commands (Partial List) ---
#define CDC_GETSTAT     0x01 // Get current drive status
#define CDC_SETLOC      0x02 // Set position (LBA) for read/play
#define CDC_READN       0x06 // Read sectors starting at SetLoc position (Normal read)
#define CDC_PAUSE       0x09 // Pause playback/reading, sends response
#define CDC_INIT        0x0A // Initialize controller/drive state
#define CDC_SEEKL       0x15 // Seek to LBA (Logical - data track only?)
#define CDC_TEST        0x19 // Test commands (various subfunctions, see nocash/PSX-Spex for response sequence)
#define CDC_GETID       0x1A // Get drive ID (returns SCEx string / No Disc / Licensed status)
#define CDC_STOP        0x08 // Stop CD-DA playback/Read <<< Add this if missing

#define CD_SECTOR_SIZE 2352 // Common raw sector size for Mode 2

// --- PSX-SPX CDROM Status Register (HSTS) Bits ---
#define HSTS_RA_MASK    0x03  // Bits 0-1: Register bank (R/W)
#define HSTS_ADPBUSY    0x04  // Bit 2: ADPCM busy (R, 1=playing XA-ADPCM)
#define HSTS_PRMEMPT    0x08  // Bit 3: Parameter empty (R, 1=parameter FIFO empty)  
#define HSTS_PRMWRDY    0x10  // Bit 4: Parameter write ready (R, 1=parameter FIFO not full)
#define HSTS_RSLRRDY    0x20  // Bit 5: Result read ready (R, 1=result FIFO not empty)
#define HSTS_DRQSTS     0x40  // Bit 6: Data request (R, 1=data read/write pending)
#define HSTS_BUSYSTS    0x80  // Bit 7: Busy status (R, 1=HC05 busy acknowledging command)

// --- PSX-SPX CDROM Interrupt Types (HINTSTS bits 0-2) ---
#define INT_NOINTR      0  // No interrupt pending
#define INT_DATAREADY   1  // New sector (ReadN/ReadS) or report packet available
#define INT_COMPLETE    2  // Command finished processing (after INT3)
#define INT_ACKNOWLEDGE 3  // Command received and acknowledged (all commands)  
#define INT_DATAEND     4  // Reached end of disc/track (auto-pause enabled)
#define INT_DISKERROR   5  // Command error, read error, license error, lid opened

// --- CDROM FIFO Structure ---
#define FIFO_SIZE 16
typedef struct {
    uint8_t data[FIFO_SIZE];
    uint8_t count;    // Number of bytes currently in FIFO
    uint8_t read_ptr; // Index of the next byte to read
} Fifo8;

// --- CDROM Internal State ---
// Basic state machine for the drive
typedef enum {
    CD_STATE_IDLE,       // Doing nothing
    CD_STATE_CMD_EXEC,   // Processing a command (e.g., Seek, Init) that takes time
    CD_STATE_READING,    // Executing ReadN/ReadS, data transfer pending/active
    CD_STATE_ERROR       // An error occurred on the last command
} CdromState;

// --- CDROM State Structure ---
// Holds the complete state of the emulated CD-ROM drive and controller per PSX-SPX.
typedef struct Cdrom {
    // --- PSX-SPX Controller Registers ---
    /** @brief Current register bank (0-3) from ADDRESS register bits 0-1 */
    uint8_t register_bank;
    /** @brief HSTS register (0x1F801800 read) - drive/controller status */
    uint8_t hsts_register;
    /** @brief HINTSTS register - interrupt type (bits 0-2) and flags (bits 3-4) */
    uint8_t hintsts_register; 
    /** @brief HINTMSK register - interrupt enable mask */
    uint8_t hintmsk_register;
    
    // --- PSX-SPX FIFOs ---
    /** @brief Parameter FIFO for command parameters (16 bytes max) */
    Fifo8 param_fifo;
    /** @brief Result FIFO for command responses (16 bytes max) */
    Fifo8 result_fifo;
    
    // --- Data Buffer for Sector Reads ---
    /** @brief Buffer to hold sector data for RDDATA reads */
    uint8_t data_buffer[CD_SECTOR_SIZE];
    /** @brief Number of bytes available in data buffer */
    uint32_t data_buffer_count;
    /** @brief Read pointer within data buffer */
    uint32_t data_buffer_read_ptr;
    
    // --- Drive State Machine ---
    /** @brief Current operational state of the drive */
    CdromState current_state;
    /** @brief Command currently being processed */
    uint8_t pending_command;

    // --- Timing & Scheduling ---
    /** @brief Cycles until the current command is complete */
    uint32_t cycles_until_event;
    /** @brief Completion handler for delayed commands */
    void (*pending_completion_handler)(struct Cdrom*);

    // --- Drive Parameters ---
    /** @brief Logical Block Address target set by SetLoc */
    uint32_t target_lba;
    /** @brief Drive speed (0=normal, 1=double) */
    bool double_speed;
    /** @brief Sector size selection (0=2048 bytes, 1=2340 bytes) */
    bool sector_size_is_2340;
    
    // --- Disc State ---
    /** @brief Flag indicating if a valid disc is loaded */
    bool disc_present;
    /** @brief Flag indicating if the disc is audio CD */
    bool is_cd_da;
    /** @brief File handle for disc image (.bin/.iso) */
    FILE* disc_file;

    /** @brief Pointer to interconnect for interrupt requests */
    struct Interconnect* inter;

} Cdrom;


// --- Function Prototypes ---

/**
 * @brief Initializes the CD-ROM drive state to default values.
 * @param cdrom Pointer to the Cdrom state structure.
 * @param inter Pointer to the Interconnect structure.
 */
void cdrom_init(Cdrom* cdrom, struct Interconnect* inter);

/**
 * @brief Reads an 8-bit value from a CD-ROM register address per PSX-SPX bank switching.
 * Handles bank-switched register access based on register_bank (ADDRESS bits 0-1).
 * @param cdrom Pointer to the Cdrom state structure.
 * @param addr The physical address being accessed (0x1F801800 - 0x1F801803).
 * @return The 8-bit value read from the bank-switched register.
 */
uint8_t cdrom_read_register(Cdrom* cdrom, uint32_t addr);

/**
 * @brief Writes an 8-bit value to a CD-ROM register address per PSX-SPX bank switching.
 * Handles bank-switched register access and triggers command execution.
 * @param cdrom Pointer to the Cdrom state structure.
 * @param addr The physical address being accessed (0x1F801800 - 0x1F801803).
 * @param value The 8-bit value to write.
 */
void cdrom_write_register(Cdrom* cdrom, uint32_t addr, uint8_t value);

/**
 * @brief Attempts to load a disc image file (.bin/.iso).
 * Currently does not handle CUE sheets.
 * @param cdrom Pointer to the Cdrom state structure.
 * @param bin_filename Path to the .bin or .iso file.
 * @return True if successful, false otherwise.
 */
bool cdrom_load_disc(Cdrom* cdrom, const char* bin_filename);

/**
 * @brief Steps the CD-ROM state machine, handling command delays and completion.
 * NOTE: This function must be called regularly from the main loop to ensure CDROM IRQs are triggered.
 * @param cdrom Pointer to the Cdrom state structure.
 * @param cycles The number of CPU cycles that have passed since the last step.
 */
void cdrom_step(Cdrom* cdrom, uint32_t cycles);

void cdrom_exec_cmd(Cdrom* cdrom, uint8_t cmd);

/**
 * @brief Handles the Test command (0x19) and its subcommands, strictly following nocash/PSX-Spex:
 * - Response FIFO must contain status, then result bytes (e.g., BIOS date/version for sub 0x20)
 * - Status register must reflect RSLRDY and not busy after command
 * - INT3 (response ready) and INT2 (command complete) must be triggered in correct order
 */
void cmd_test(struct Cdrom* cdrom);

#endif // CDROM_H