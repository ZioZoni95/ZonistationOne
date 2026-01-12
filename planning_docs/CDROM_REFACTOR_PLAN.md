# CDROM Complete Modular Refactor

## Overview
Complete PS1 CDROM controller implementation with all commands based on DuckStation and PSX-SPX documentation.

## Architecture

### Modular Structure
```
include/cdrom/
├── cdrom_types.h      - Type definitions, enums, structs
└── cdrom_core.h       - Public API

src/cdrom/
├── cdrom_core.c       - Initialization, I/O, state management
├── cdrom_commands.c   - All 32 command implementations
└── cdrom_drive.c      - Drive state machine (seeking, reading, playing)
```

### Command Implementation Status (0x00-0x1F)

#### Control Commands
- [x] 0x00: Sync - ✅ Returns error (invalid command)
- [x] 0x01: GetStat - ✅ Return status byte
- [x] 0x0A: Init - ✅ Initialize controller
- [x] 0x1C: Reset - ⚠️ Need to implement HC05 reboot

#### Seek Commands
- [x] 0x02: Setloc - ✅ Set seek position
- [x] 0x15: SeekL - ✅ Seek logical
- [x] 0x16: SeekP - ✅ Seek physical

#### Read Commands
- [x] 0x06: ReadN - ✅ Read with retry
- [x] 0x1B: ReadS - ✅ Read without retry  
- [x] 0x12: ReadT - 🔴 NOT IMPLEMENTED (ReadTOC alternative)
- [x] 0x1E: ReadTOC - 🔴 Need to implement

#### Status Commands
- [x] 0x0F: GetMode/Getparam - ✅ Get mode register
- [x] 0x10: GetLocL - ✅ Get logical position
- [x] 0x11: GetLocP - ✅ Get physical position
- [x] 0x13: GetTN - ✅ Get first/last track
- [x] 0x14: GetTD - ✅ Get track start
- [x] 0x1A: GetID - ⚠️ Partial (need full disc ID)
- [x] 0x1D: GetQ - 🔴 NOT IMPLEMENTED (SubQ data)

#### CD Audio Commands
- [x] 0x03: Play - ✅ Play CD-DA
- [x] 0x04: Forward - 🔴 NOT IMPLEMENTED
- [x] 0x05: Backward - 🔴 NOT IMPLEMENTED
- [x] 0x07: MotorOn/Standby - ✅ Spin up
- [x] 0x08: Stop - ✅ Stop motor
- [x] 0x09: Pause - ✅ Pause reading
- [x] 0x0B: Mute - ✅ Mute audio
- [x] 0x0C: Demute - ✅ Unmute audio

#### Advanced Commands
- [x] 0x0D: Setfilter - ✅ Set XA-ADPCM filter
- [x] 0x0E: Setmode - ✅ Set mode flags
- [x] 0x12: Setsession - 🔴 NOT IMPLEMENTED
- [x] 0x17: SetClock - 🔴 NOT IMPLEMENTED (undocumented)
- [x] 0x18: GetClock - 🔴 NOT IMPLEMENTED (undocumented)
- [x] 0x19: Test - ⚠️ Partial (only subcommand 0x20)
- [x] 0x1F: VideoCD - 🔴 NOT IMPLEMENTED (SCPH-5903 only)

#### Secret Unlock Commands (0x50-0x57)
- [ ] 0x50-0x56: Unlock0-6 - 🔴 NOT IMPLEMENTED (region unlock)
- [ ] 0x57: Lock - 🔴 NOT IMPLEMENTED

## Critical Features for BIOS Boot Menu

### Priority 1: GetID Command (0x1A)
**Problem:** BIOS uses GetID to detect disc presence
**Solution:**
```c
// First response: INT3(stat)
// Second response:
if (!disc_present) {
    // INT5(stat|ERROR, NOT_READY)  
    response[0] = STAT_SHELL_OPEN | STAT_ERROR;
    response[1] = ERROR_REASON_NOT_READY;
    INT5
} else {
    // INT2(stat, flags, disc_type, atip, "SCEx")
    response[0] = stat;           // Status
    response[1] = 0x00;            // Flags (0x00=licensed, 0x80=unlicensed)
    response[2] = 0x00;            // Disc type (0x00=PS1 CD)
    response[3] = 0x00;            // ATIP (0x00 for CD-ROM)
    response[4] = 'S';             // Region string "SCEI" (Japan)
    response[5] = 'C';             //  or "SCEA" (America)
    response[6] = 'E';             //  or "SCEE" (Europe)
    response[7] = 'I';             //
    INT2
}
```

### Priority 2: Test Command (0x19) Subcommand 0x20
**Current:** Only returns version
**Need:** Full response for all subcodes

```c
case 0x19: // Test
    subcommand = param[0]
    switch(subcommand) {
        case 0x20: // Get version
            response = {yy, mm, dd, ver}
            INT3
        case 0x00-0x05: // Motor control
        case 0x10-0x1A: // Lens control
        default:
            INT3(stat) or INT5(error)
    }
```

### Priority 3: ReadTOC Command (0x1E)
**Problem:** BIOS calls ReadTOC after disc insert
**Solution:**
```c
// First response: INT3(stat) - immediate
// Second response: INT2(stat) - after 1 second delay
// Populates TOC in controller memory
```

## Implementation Strategy

### Phase 1: Complete All Basic Commands ✅
- [x] Getstat, Setloc, Init, Stop, Pause
- [x] Mute, Demute, Setfilter, Setmode, Getmode
- [x] SeekL, SeekP, ReadN, ReadS, Play
- [x] GetLocL, GetLocP, GetTN, GetTD
- [x] MotorOn, Test(0x20)

### Phase 2: Fix Critical Commands for BIOS ⬅️ CURRENT
- [ ] GetID - Full implementation with disc type/region
- [ ] Test - All subcommands (at least motor/lens control stubs)
- [ ] ReadTOC - Load TOC from disc

### Phase 3: Advanced Features
- [ ] Setsession - Multi-session CD support
- [ ] GetQ - SubQ data reading
- [ ] Forward/Backward - Fast seek
- [ ] XA-ADPCM decoding - Audio playback
- [ ] DMA integration - Sector buffer DMA

### Phase 4: Polish
- [ ] Accurate timing for all commands
- [ ] Error handling for all edge cases
- [ ] CD-DA audio output to SPU
- [ ] Region checking (SCEx string validation)

## DuckStation Reference Points

### Command Execution Flow (cdrom.cpp:1829)
```cpp
void CDROM::ExecuteCommand() {
    // 1. Validate parameter count
    if (param_count < min || param_count > max)
        SendErrorResponse(INCORRECT_PARAMS);
    
    // 2. Clear response FIFO
    response_fifo.Clear();
    
    // 3. Deactivate command event
    command_event.Deactivate();
    
    // 4. Execute specific command
    switch(command) {
        case Getstat: ...
        case Setloc: ...
        ...
    }
    
    // 5. End command (clear BUSYSTS)
    EndCommand();
}
```

### GetID Implementation (DuckStation style)
```cpp
case Command::GetID:
    ClearCommandSecondResponse();
    if (!CanReadMedia()) {
        SendErrorResponse(STAT_ERROR, ERROR_NOT_READY);
    } else {
        SendACKAndStat();
        QueueCommandSecondResponse(Command::GetID, GetTicksForIDRead());
    }
    EndCommand();
    
// Later, in second response handler:
void ExecuteCommandSecondResponse() {
    if (command == GetID) {
        PushResponse(stat);
        PushResponse(flags);      // 0x00 = Licensed, 0x80 = Unlicensed
        PushResponse(disc_type);  // 0x00 = PS1, 0x10 = PS2, 0x20 = Audio
        PushResponse(0x00);       // ATIP
        PushResponse('S');        // Region: SCEI/SCEA/SCEE
        PushResponse('C');
        PushResponse('E');
        PushResponse(region_char); // I/A/E
        SendAsyncInterrupt(INT2);
    }
}
```

## Status Byte Definitions
```c
#define STAT_ERROR        (1 << 0)  // Command error
#define STAT_MOTOR_ON     (1 << 1)  // Motor spinning
#define STAT_SEEK_ERROR   (1 << 2)  // Seek failed
#define STAT_ID_ERROR     (1 << 3)  // GetID failed (no disc)
#define STAT_SHELL_OPEN   (1 << 4)  // Drive door open
#define STAT_READING      (1 << 5)  // Reading data
#define STAT_SEEKING      (1 << 6)  // Seeking
#define STAT_PLAYING_CDDA (1 << 7)  // Playing CD-DA audio
```

## Error Reason Codes
```c
#define ERROR_REASON_INVALID_ARGUMENT    0x10
#define ERROR_REASON_INCORRECT_PARAMS    0x20
#define ERROR_REASON_INVALID_COMMAND     0x40
#define ERROR_REASON_NOT_READY           0x80
```

## Test Plan

### Test 1: BIOS Boot (No Disc)
```
Expected GetID sequence:
1. BIOS: Setloc(00:02:00)
2. BIOS: GetID
3. CDROM: INT3(stat)
4. CDROM: INT5(stat|ERROR|SHELL_OPEN, NOT_READY)
5. BIOS: Shows "Please insert PlayStation CD-ROM"
```

### Test 2: BIOS Boot (With Licensed Disc)
```
Expected GetID sequence:
1. BIOS: ReadTOC
2. BIOS: GetID
3. CDROM: INT3(stat)
4. CDROM: INT2(stat, 0x00, 0x00, 0x00, "SCEI")
5. BIOS: Shows boot menu
```

### Test 3: All Commands
```bash
# Test command implementation
./myps1_emu roms/SCPH1001.BIN --test-cdrom

Expected output:
[CDROM] Testing command 0x01 (Getstat)... PASS
[CDROM] Testing command 0x02 (Setloc)... PASS
[CDROM] Testing command 0x0A (Init)... PASS
...
[CDROM] Testing command 0x1A (GetID)... PASS
[CDROM] 28/32 commands implemented
[CDROM] All critical commands working ✓
```

## Next Steps

1. ✅ Complete basic command stubs
2. ⬅️ **Implement GetID properly (CURRENT)**
3. Implement ReadTOC
4. Test with BIOS boot menu
5. Add XA-ADPCM decoding
6. Implement remaining commands

## References

- DuckStation: `duckstation/src/core/cdrom.cpp`
- PSX-SPX: `DOCS/cdromdrive.md`
- NO$PSX Docs: CDROM Controller section
- Mednafen: CDROM timing reference
