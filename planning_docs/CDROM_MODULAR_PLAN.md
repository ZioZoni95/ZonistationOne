# CDROM Modular Refactoring Plan

## Objectives
Create a modular, thread-safe CDROM controller similar to the IRQ module architecture, based on duckstation's implementation and PSX-SPX documentation.

## Architecture Overview

### Current State (src/cdrom.c)
- **Size**: 846 lines, monolithic design
- **Issues**: 
  - Not thread-safe (no mutex protection)
  - Tightly coupled with interconnect
  - Event callbacks scattered throughout
  - No clear separation of concerns

### Target State (Modular Design)
```
include/cdrom/
  cdrom_types.h    - Type definitions and structures
  cdrom_core.h     - Public API declarations
  cdrom_commands.h - Command definitions and handlers

src/cdrom/
  cdrom_core.c     - Core controller logic (thread-safe)
  cdrom_commands.c - Command implementation
  cdrom_drive.c    - Drive state machine and seeking
  cdrom_xa.c       - XA-ADPCM audio processing (optional)
```

## Module Design (Based on Duckstation)

### 1. cdrom_types.h (Type Definitions)
```c
typedef struct CdromState {
    // Hardware Registers (1f801800-1f801803)
    uint8_t status_register;      // HSTS (read), ADDRESS (write)
    uint8_t interrupt_enable;     // HINTMSK (bank 0/1)
    uint8_t interrupt_flag;       // HINTSTS (bank 1/3)
    uint8_t mode_register;        // Setmode bits (CDDA, auto_pause, etc.)
    uint8_t request_register;     // BFRD, BFWR, SMEN
    
    // FIFOs
    uint8_t param_fifo[16];
    uint8_t param_fifo_count;
    uint8_t response_fifo[16];
    uint8_t response_fifo_count;
    uint8_t data_fifo[2048];      // Sector buffer
    uint16_t data_fifo_count;
    
    // Drive State
    DriveState drive_state;
    SecondaryStatus status;
    uint8_t current_command;
    uint8_t pending_command;
    
    // Sector Position (MSF format: Minute:Second:Frame)
    uint8_t seek_minute;
    uint8_t seek_second;
    uint8_t seek_frame;
    uint32_t current_lba;         // Logical Block Address
    
    // XA-ADPCM State
    bool xa_enabled;
    uint8_t xa_filter_file;
    uint8_t xa_filter_channel;
    
    // Audio State
    uint8_t cd_audio_volume_l_to_l;
    uint8_t cd_audio_volume_l_to_r;
    uint8_t cd_audio_volume_r_to_r;
    uint8_t cd_audio_volume_r_to_l;
    
    // Disc Information
    bool disc_present;
    bool motor_on;
    uint32_t disc_size_sectors;
    FILE* disc_file;              // BIN file handle
    
    // Thread Safety
    Mutex lock;                   // Recursive mutex for thread-safe operations
    
    // Statistics (debugging)
    uint64_t command_count;
    uint64_t sector_reads;
    uint64_t interrupt_count;
    
} CdromState;
```

### 2. cdrom_core.h (Public API)
```c
// Initialization & Lifecycle
void cdrom_init(CdromState* cdrom);
void cdrom_shutdown(CdromState* cdrom);
void cdrom_reset(CdromState* cdrom);

// Disc Management (thread-safe)
bool cdrom_load_disc(CdromState* cdrom, const char* cue_path);
void cdrom_eject_disc(CdromState* cdrom);
bool cdrom_has_disc(const CdromState* cdrom);

// Hardware Register Access (thread-safe)
uint8_t cdrom_read_register(CdromState* cdrom, uint32_t offset);
void cdrom_write_register(CdromState* cdrom, uint32_t offset, uint8_t value);

// DMA Interface (thread-safe)
void cdrom_dma_read(CdromState* cdrom, uint32_t* words, uint32_t word_count);

// Event Callbacks (called by event scheduler)
void cdrom_command_event(CdromState* cdrom, uint32_t cycles_late);
void cdrom_drive_event(CdromState* cdrom, uint32_t cycles_late);
void cdrom_interrupt_event(CdromState* cdrom, uint32_t cycles_late);

// Audio Output (for SPU integration)
bool cdrom_get_audio_frame(CdromState* cdrom, int16_t* left, int16_t* right);
```

### 3. Command Handler Architecture
Based on duckstation's enum approach:

```c
typedef enum CdromCommand {
    CMD_SYNC       = 0x00,
    CMD_GETSTAT    = 0x01,
    CMD_SETLOC     = 0x02,
    CMD_PLAY       = 0x03,
    CMD_FORWARD    = 0x04,
    CMD_BACKWARD   = 0x05,
    CMD_READN      = 0x06,
    CMD_MOTORON    = 0x07,
    CMD_STOP       = 0x08,
    CMD_PAUSE      = 0x09,
    CMD_INIT       = 0x0A,
    CMD_MUTE       = 0x0B,
    CMD_DEMUTE     = 0x0C,
    CMD_SETFILTER  = 0x0D,
    CMD_SETMODE    = 0x0E,
    CMD_GETMODE    = 0x0F,
    CMD_GETLOCL    = 0x10,
    CMD_GETLOCP    = 0x11,
    CMD_READT      = 0x12,
    CMD_GETTN      = 0x13,
    CMD_GETTD      = 0x14,
    CMD_SEEKL      = 0x15,
    CMD_SEEKP      = 0x16,
    CMD_TEST       = 0x19,
    CMD_GETID      = 0x1A,
    CMD_READS      = 0x1B,
    CMD_NONE       = 0xFF
} CdromCommand;

typedef enum DriveState {
    DRIVE_IDLE,
    DRIVE_SEEKING_LOGICAL,
    DRIVE_SEEKING_PHYSICAL,
    DRIVE_READING,
    DRIVE_PLAYING,
    DRIVE_SPINNING_UP,
    DRIVE_SHELL_OPENING,
    DRIVE_CHANGING_SESSION
} DriveState;

// Interrupt types (INT1-INT5)
typedef enum CdromInterrupt {
    INT_NONE       = 0,
    INT_DATA_READY = 1,  // INT1: Data ready to read
    INT_COMPLETE   = 2,  // INT2: Command complete
    INT_ACK        = 3,  // INT3: Acknowledge
    INT_DATA_END   = 4,  // INT4: Data end
    INT_ERROR      = 5   // INT5: Error
} CdromInterrupt;
```

### 4. Thread Safety Strategy

Like the IRQ module:
1. **Recursive Mutex**: All CDROM state modifications protected
2. **Atomic Operations**: Register reads/writes are atomic
3. **Lock Granularity**: Fine-grained locking (lock per operation, not per function call)
4. **Event Integration**: Thread-safe event scheduling for command/drive events

```c
// Example: Thread-safe register write
void cdrom_write_register(CdromState* cdrom, uint32_t offset, uint8_t value) {
    mutex_lock(&cdrom->lock);
    
    uint8_t bank = cdrom->status_register & 0x03;
    
    switch(offset) {
        case 0: // ADDRESS register
            cdrom->status_register = (cdrom->status_register & ~0x03) | (value & 0x03);
            break;
            
        case 1: // Command/Data/Config (bank-dependent)
            if (bank == 0) {
                // COMMAND register
                cdrom_execute_command(cdrom, value);
            } else if (bank == 1) {
                // WRDATA register (write to data buffer)
                cdrom_write_data_byte(cdrom, value);
            }
            break;
            
        // ... other registers
    }
    
    mutex_unlock(&cdrom->lock);
}
```

## Integration with Existing Code

### Interconnect Changes
```c
// interconnect.h
#include "cdrom/cdrom_core.h"

typedef struct Interconnect {
    // ... existing fields ...
    IrqState irq_state;      // ✅ Already modular
    CdromState cdrom_state;  // 🆕 New modular CDROM
    // ...
} Interconnect;

// interconnect_load32() for CDROM region (0x1f801800-0x1f801803)
uint32_t interconnect_load32(Interconnect* inter, uint32_t addr) {
    // ...
    if (addr >= 0x1f801800 && addr < 0x1f801804) {
        return cdrom_read_register(&inter->cdrom_state, addr & 0x3);
    }
    // ...
}
```

### Event Scheduler Integration
```c
// event_scheduler.c
typedef enum EventType {
    EVQ_TIMER0,
    EVQ_TIMER1,
    EVQ_TIMER2,
    EVQ_VBLANK,
    EVQ_DMA_GPU,
    EVQ_CDROM_COMMAND,   // 🆕 CDROM command processing
    EVQ_CDROM_DRIVE,     // 🆕 CDROM drive state machine
    EVQ_CDROM_INTERRUPT, // 🆕 CDROM interrupt delivery
    EVQ_COUNT
} EventType;

static void evq_handle_cdrom_command(struct Interconnect* sys) {
    cdrom_command_event(&sys->cdrom_state, 0);
}
```

## Key Differences from Current Implementation

| Aspect | Current (cdrom.c) | Modular (cdrom/) |
|--------|-------------------|------------------|
| **Thread Safety** | ❌ None | ✅ Mutex protected |
| **File Structure** | 1 file (846 lines) | 4 files (~1200 lines) |
| **Separation** | Interconnect-coupled | Standalone module |
| **Register Access** | Direct field access | API functions |
| **Event Handling** | Inline callbacks | Dedicated event API |
| **Testing** | Hard to isolate | Easy to unit test |
| **Documentation** | Minimal | Comprehensive |

## Implementation Steps

1. **Phase 1: Types & API Design** (30 min)
   - Create `include/cdrom/cdrom_types.h`
   - Create `include/cdrom/cdrom_core.h`
   - Define all structures and function signatures

2. **Phase 2: Core Implementation** (1 hour)
   - Create `src/cdrom/cdrom_core.c`
   - Implement initialization and register access
   - Port command handling from existing cdrom.c

3. **Phase 3: Drive State Machine** (45 min)
   - Implement drive state transitions
   - Port seeking and reading logic
   - Add event callbacks

4. **Phase 4: Integration** (30 min)
   - Update interconnect.h/interconnect.c
   - Update event_scheduler.c
   - Update Makefile

5. **Phase 5: Testing** (30 min)
   - Compile and fix errors
   - Test BIOS boot
   - Test disc reading
   - Verify IRQ generation

## Benefits

1. **Thread Safety**: Multi-threaded CPU/GPU won't cause CDROM race conditions
2. **Modularity**: Clean separation allows easier debugging and testing
3. **Maintainability**: Easier to understand and modify specific components
4. **Scalability**: Can add XA-ADPCM decoding, async reading later
5. **Consistency**: Same pattern as IRQ module (proven successful)

## References

- **DuckStation**: `duckstation/src/core/cdrom.cpp` (4353 lines, well-structured)
- **PSX-SPX**: `DOCS/cdromdrive.md` (2182 lines, comprehensive spec)
- **Current**: `src/cdrom.c` (846 lines, needs refactoring)

## Status

- [x] IRQ module completed (thread-safe, modular)
- [ ] CDROM module design (in progress)
- [ ] CDROM module implementation
- [ ] GPU module refactoring
- [ ] DMA module refactoring
- [ ] Timers module refactoring
