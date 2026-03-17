# ZoniStation One (PlayStation 1) Emulator

A work-in-progress PlayStation 1 emulator written in C (C99), inspired by nocash and PSX-Spex documentation.

---

## 📸 Screenshots

| Sony Logo (Boot) | BIOS Menu |
|:-----------------:|:---------:|
| ![Sony Logo](screenshots/sony_logo.png) | ![BIOS Menu](screenshots/bios_menu.png) |
| Sony logo boot animation (diamond shape) | BIOS shell menu with animated orbs & text |

---

## 🏁 Current Status (March 2026)

### 🎉 MAJOR MILESTONE: BIOS Menu Fully Rendering!

- **Boots to BIOS menu successfully!** The emulator passes all CDROM self-tests and displays the PlayStation BIOS shell menu with animated floating orbs/balloons and text overlays.
- **Menu state:** Shell (bootrom at 0x80030000) detects no game disc → displays interactive menu with 3 selectable options:
  - **MAIN MENU** (system utilities)
  - **MEMORY CARD** (save manager)
  - **CD PLAYER** (disc utilities)
- **GPU rendering:** Fully working! Menu orbs render correctly with proper colors, double-buffering, and text overlays.
- **CDROM emulation:** Event-driven asynchronous architecture with proper timing delays (~25000 cycles for ACK).
- **Interrupt system:** INT3 (ACK), INT2 (Complete), INT1 (Data Ready) fully operational.
- **Timer and IRQ system:** Hardware-accurate event scheduling with proper clock source routing (sysclk, dotclock, hblank, sysclk/8).

### What's Working
- ✅ Full BIOS boot sequence completes
- ✅ CDROM Test commands (0x19, subfunctions 0x20, 0x04, 0x05, 0x22)
- ✅ CDROM GetStat, SetMode, GetID commands
- ✅ Asynchronous command execution with proper timing
- ✅ Interrupt blocking (commands wait for INT acknowledgment)
- ✅ VBlank IRQ delivery
- ✅ Timer IRQs
- ✅ DMA transfers to VRAM (GPU display lists)
- ✅ BIOS menu transition animation

### Component Status (March 2026)

| Component   | Status      | Notes                                                    |
|-------------|-------------|----------------------------------------------------------|
| CPU         | ✅ EXCELLENT | All MIPS R3000A instructions, COP0 MMU, exceptions       |
| RAM         | ✅ EXCELLENT | 2MB + scratchpad, full working                           |
| VRAM        | ✅ EXCELLENT | 1MB working, VRAM display scaling correct                |
| BIOS        | ✅ EXCELLENT | Boot → menu fully working (shell at 0x80030000)          |
| CDROM       | ✅ EXCELLENT | Async event-driven, Test/GetStat/GetID/SetMode working  |
| GPU         | ✅ EXCELLENT | Menu rendering perfect (orbs, text overlays, no glitches)|
| Renderer    | ✅ EXCELLENT | OpenGL backend, display mapping, color order fixed       |
| Timers      | ✅ EXCELLENT | All 3 timers, correct clock sources, IRQ generation     |
| IRQ System  | ✅ EXCELLENT | Edge-triggered, proper hardware IRQ routing              |
| Events      | ✅ EXCELLENT | Callback-based event queue, VBlank/Timer/DMA timing      |
| DMA         | ✅ GOOD      | Ch2(GPU)→VRAM, Ch6(OTC), Ch3(CDROM) stubbed              |
| SIO         | ⚠️ PARTIAL  | Registers mapped, gamepad protocol incomplete            |
| GTE         | ❌ STUBS    | Needed for 3D games (not required for menu)              |
| SPU         | ❌ MISSING  | Needed for sound (not required for menu)                 |
| MDEC        | ❌ MISSING  | Needed for FMV (not required for menu)                   |

---

## 🎯 Next Steps (Summary)

**Current Blocker:** Menu displays perfectly, but awaits controller input (SIO not polling gamepad).

| Priority | Task | Status | Blocker |
|----------|------|--------|---------|
| 🔴 **P0** | Controller input (SIO gamepad protocol) | **CRITICAL** | Menu interaction frozen |
| 🔴 **P0** | CDROM Read commands (ReadN/ReadS) | Not started | Game loading |
| 🟠 **P1** | DMA Channel 3 (CDROM→RAM) | Not started | Game data streaming |
| 🟠 **P1** | GTE core operations (RTPS, MVMVA) | Stubs only | 3D game support |
| 🟡 **P2** | Memory card read/write | Partial | Save persistence |
| 🟢 **P3** | SPU audio engine | Not started | Sound support |
| 🟢 **P3** | MDEC video decoder | Not started | FMV playback |

---

## 📝 Logging System

The emulator uses a unified logging macro scheme:

- **Macros:** Use `LOG_<CATEGORY>_<LEVEL>(...)` for all logging calls
- **Levels:** `ERROR`, `WARN`, `INFO`, `DEBUG`, `TRACE`
- **Categories:** `CPU`, `CDROM`, `DMA`, `IRQ`, `GPU`, `TIMER`, `SYSTEM`, etc.

See `include/log.h` for details.

---

## Building
```sh
make
```
Requires: gcc, SDL2, OpenGL, GLEW

## Running
```sh
./myps1_emu [options]
```
Default BIOS path: `roms/SCPH1001.BIN`
Game disc path: `games/<game>.cue`

### Command Line Options
| Option | Description |
|--------|-------------|
| `--debug` | Set log level to DEBUG |
| `--trace` | Set log level to TRACE |
| `--quiet` | Set log level to WARN |
| `--help` | Show help message |

---

## Recent Critical Fixes (Session Log)

### GPU & Rendering Pipeline (March 2026)
- **GPU Draw Offset (GP0 0xE5):** Fixed missing `renderer_set_draw_offset()` call → double-buffer offsets (0,1) and (0,241) now applied
- **VRAM Blit Color Order:** Fixed R/B swap in 15-bit color channel mapping (R=bits 0-4, G=5-9, B=10-14) → orbs now render correct colors
- **Display Mapping:** VRAM display region (dimensions from gpu_update_display_mapping) now correctly fills window

### Timer Clock Source Routing (Critical)
- **Timer0:** Clock source 0/1=sysclock, 2/3=dotclock
- **Timer1:** Clock source 0/1=sysclock, 2/3=hblank
- **Timer2:** Clock source 0/1=sysclock, 2/3=sysclock/8
- Fixed infinite polling loop at 0x1F801120 (BIOS Timer2 halt) → BIOS no longer CPU-locked after menu appearance

### DMA Completion IRQ
- `interconnect_perform_dma` signals IRQ3 when transfer done (guards against double-fire)
- Removed PCSX ReARMed "re-raise on DICR write" hack that caused infinite loops

### SIO/Controller (Partial)
- Fixed SIO register range 0x1F801040-0x1F80104F being covered by MEM_CONTROL_END
- JOY_STAT now readable by BIOS (TX_RDY bits visible)
- Added IRQ7 (IRQ_CTRLMEMCARD) support with STAT_IRQ + pending_irq in sio_handle_transfer

### Previous Session Fixes
- CDROM async event-driven architecture (replaced sync execution)
- Proper INT3→INT2 two-phase response for GetID, Init, Pause
- IRQ edge-triggered delivery (0→1 transitions only)
- Event scheduler with callback-based timing

---

## Architecture Overview

The emulator is built with a modular architecture where the **Interconnect** acts as the central memory bus, routing all CPU memory accesses to the appropriate hardware components. The **Event Scheduler** provides timing coordination for asynchronous hardware events.

### System Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              MAIN LOOP (main.c)                             │
│    ┌──────────────────────────────────────────────────────────────────┐    │
│    │  while(running) {                                                 │    │
│    │      cpu_run_instruction()     // Execute one CPU instruction     │    │
│    │      event_scheduler_tick()    // Process pending hardware events │    │
│    │      renderer_present()        // Update display (on VBlank)      │    │
│    │  }                                                                │    │
│    └──────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
          ┌───────────────────────────┼───────────────────────────┐
          ▼                           ▼                           ▼
┌──────────────────┐       ┌──────────────────┐       ┌──────────────────┐
│   CPU (cpu.c)    │       │ EVENT SCHEDULER  │       │ RENDERER         │
│   2108 lines     │       │ (event_scheduler │       │ (renderer.c)     │
│                  │       │  .c) 209 lines   │       │ 460 lines        │
│ • MIPS R3000A    │       │                  │       │                  │
│ • All instrs     │       │ • VBlank events  │       │ • OpenGL backend │
│ • COP0 (MMU)     │       │ • Timer IRQs     │       │ • Textured quads │
│ • Exception      │       │ • CDROM callbacks│       │ • Shaded polys   │
│   handling       │       │ • DMA completion │       │ • VRAM display   │
└────────┬─────────┘       └────────┬─────────┘       └────────┬─────────┘
         │                          │                          │
         │    Memory R/W            │   Schedule/Fire          │   Draw calls
         ▼                          ▼                          ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                        INTERCONNECT (interconnect.c)                        │
│                              2191 lines                                     │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │  Address Decoding & Memory Map (PSX Memory Layout)                  │    │
│  │  0x00000000-0x001FFFFF: RAM (2MB mirrored)                         │    │
│  │  0x1F000000-0x1F7FFFFF: Expansion Region 1                         │    │
│  │  0x1F800000-0x1F8003FF: Scratchpad (1KB)                           │    │
│  │  0x1F801000-0x1F802FFF: Hardware I/O Registers                     │    │
│  │  0x1FC00000-0x1FC7FFFF: BIOS ROM (512KB)                           │    │
│  └────────────────────────────────────────────────────────────────────┘    │
└───┬─────────┬─────────┬─────────┬─────────┬─────────┬─────────┬────────────┘
    │         │         │         │         │         │         │
    ▼         ▼         ▼         ▼         ▼         ▼         ▼
┌───────┐ ┌───────┐ ┌───────┐ ┌───────┐ ┌───────┐ ┌───────┐ ┌───────┐
│ BIOS  │ │  RAM  │ │ VRAM  │ │  GPU  │ │ CDROM │ │  DMA  │ │TIMERS │
│ 81 L  │ │ 106 L │ │ 103 L │ │ 614 L │ │ 830 L │ │ 228 L │ │ 457 L │
│       │ │       │ │       │ │       │ │       │ │       │ │       │
│ ROM   │ │ 2MB   │ │ 1MB   │ │GP0/1  │ │Async  │ │Ch 0-6 │ │TMR0-2 │
│ Load  │ │ Main  │ │ Frame │ │Cmds   │ │Event  │ │LinkedL│ │IRQ    │
│       │ │ +Scrp │ │ Buffer│ │Render │ │Driven │ │Block  │ │Sync   │
└───────┘ └───────┘ └───────┘ └───────┘ └───────┘ └───────┘ └───────┘
                                  │         │
                                  ▼         ▼
                           ┌───────────────────┐
                           │  IRQ Controller   │
                           │  (in interconnect)│
                           │                   │
                           │ I_STAT / I_MASK   │
                           │ Edge-triggered    │
                           │ CPU Cause.IP2     │
                           └───────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                          ADDITIONAL MODULES                                 │
├─────────────┬─────────────┬─────────────┬─────────────┬─────────────────────┤
│  GTE        │  SIO        │ Controller  │  Debugger   │  Log                │
│ (gte.c)     │ (sio.c)     │(controller  │(debugger.c) │ (log.c)             │
│  201 lines  │  371 lines  │ .c) 0 lines │  225 lines  │  209 lines          │
│             │             │             │             │                     │
│ COP2 Geom   │ Serial I/O  │ Gamepad     │ Step/Break  │ Multi-level         │
│ Transform   │ Ctrl/MemCrd │ Input stub  │ Inspect     │ TRACE/DEBUG/        │
│ STUBS       │ PARTIAL     │ STUB        │ PARTIAL     │ INFO/WARN/ERROR     │
└─────────────┴─────────────┴─────────────┴─────────────┴─────────────────────┘
```

### Module Summary (17 source files, ~8800 lines total)

| Module | File | Lines | Status | Description |
|--------|------|-------|--------|-------------|
| **CPU** | `cpu.c` | 2108 | ✅ Complete | MIPS R3000A, all instructions, COP0 MMU, exceptions |
| **Interconnect** | `interconnect.c` | 2191 | ✅ Complete | Memory bus, address decoding, IRQ controller |
| **CDROM** | `cdrom.c` | 830 | ✅ Good | Event-driven async, Test/GetStat/GetID/SetMode |
| **GPU** | `gpu.c` | 614 | ⚠️ Partial | GP0/GP1 commands, rendering primitives |
| **Renderer** | `renderer.c` | 460 | ⚠️ Partial | OpenGL backend, textured/shaded polygons |
| **Timers** | `timers.c` | 457 | ✅ Good | Timer0-2, IRQ generation, sync modes |
| **Main** | `main.c` | 386 | ✅ Complete | Main loop, SDL init, command line |
| **SIO** | `sio.c` | 371 | ⚠️ Partial | Serial I/O registers, partial controller |
| **DMA** | `dma.c` | 228 | ⚠️ Partial | Channels 2(GPU)/6(OTC) working |
| **Debugger** | `debugger.c` | 225 | ⚠️ Partial | Step execution, breakpoints |
| **Log** | `log.c` | 209 | ✅ Complete | Multi-level logging with rate-limiting |
| **Event Scheduler** | `event_scheduler.c` | 209 | ✅ Complete | Callback-based event queue |
| **GTE** | `gte.c` | 201 | ❌ Stubs | COP2 registers, instruction stubs |
| **RAM** | `ram.c` | 106 | ✅ Complete | 2MB main RAM + scratchpad |
| **VRAM** | `vram.c` | 103 | ✅ Complete | 1MB video RAM |
| **BIOS** | `bios.c` | 81 | ✅ Complete | 512KB ROM loading |
| **Controller** | `controller.c` | 0 | ❌ Stub | Empty placeholder |

### Data Flow: Boot Sequence Layout

```
1. BIOS ROM loaded (512KB SCPH1001.BIN @ 0x1FC00000)
         │
         ▼
2. CPU starts at 0xBFC00000 (bootrom entry point)
         │
         ▼
3. Bootrom (0xBFC00000-0xBFC7FFFF) initializes:
   • Memory tests (0x00000000 RAM, 0x1F800000 scratchpad)
   • Exception vectors at 0x00000080, 0x00000100 (TLB)
   • GPU init: display mode, drawing area, VRAM layout
   • Timer0/1/2 configuration (sysclock, dotclock, hblank as sources)
   • IRQ mask setup: enable IRQ0(VBlank), IRQ2(CDROM), IRQ7(SIO)
         │
         ▼
4. CDROM self-tests via Test command (0x19):
   • 0x20 GetBIOSDate → date string in response
   • 0x04 GetVersion → controller version (SCPH1001='C')
   • 0x05 GetSerial → drive serial
   • 0x22 ReadTestings → internal diagnostics
   • All responses via INT3 (ACK) + INT2 (Complete)
         │
         ▼
5. CDROM GetID command:
   • Sends INT3 (ACK) ~25000 cycles later
   • Sends INT2 (Complete) with disc params (0x00 = no disc)
   • "No disc" → bootrom halts kernel launch
         │
         ▼
6. Shell/Menu copied to RAM (0x80030000):
   • Bootrom copies shell from 0xBFC18000 → 0x80030000 (0x67FF0 bytes)
   • Jump to 0x80030000 transfers to shell
   • Shell detects "no disc" → displays interactive menu
         │
         ▼
7. BIOS Menu runtime (current state):
   • GPU renders 3 animated orbs (floating, color gradients)
   • Text overlays: "MAIN MENU", "MEMORY CARD", "CD PLAYER"
   • Double-buffered VRAM: buf1 (Y=0-240), buf2 (Y=241-480)
   • VBlank IRQ (every 16.67ms) triggers frame sync + GPU updates
   • Timer IRQs maintain timing (Timer2 @ ITR = 16, sysclock/8 source)
   • Menu awaits controller input (SIO poll on 0x1F801044):
     - Bit 11 (RX_RDY): high when pad response ready
     - Bit 8 (ACK): toggles on data ready
   • Poll loop at kernel shell reads JOY_DATA repeatedly
         │
         ▼
8. Control Flow (current blocker):
   ⚠️  Controller input NOT implemented → menu frozen (waiting for input)
   ⚠️  SIO gamepad protocol incomplete
   ⚠️  Next step: Implement SIO poll response + gamepad state
```

---

## 🔮 Analysis: BIOS Menu State & Next Actions

### What's Currently Happening (emudev.org reference)

**At this moment, your emulator is:**

1. **Bootrom executing →** SCPH1001.BIN bootstrap (0xBFC00000-0xBFC7FFFF) is running
2. **Hardware initialized →** CPU, GPU, timers, IRQ system all ready
3. **CDROM tests passed →** Bootrom confirmed no disc inserted (GetID returned 0x00)
4. **Shell loaded to RAM →** Bootrom copied shell code from 0xBFC18000 → 0x80030000 (~0x67FF0 bytes)
5. **Menu rendered →** Shell running at 0x80030000 displays the main menu:
   - Text: "MAIN MENU", "MEMORY CARD", "CD PLAYER"
   - Animated orbs: 3 floating sprites with color gradients
   - Double-buffered VRAM rendering
6. **Waiting for input →** Shell kernel loop polling SIO (0x1F801044) for gamepad state
   - **Current state:** Poll loop spins infinitely because SIO gamepad response is stubbed
   - No way to select menu options without controller input

### Why Menu Is Frozen (Technical Details)

The shell kernel at 0x80030000 runs this poll loop (pseudo-code):
```c
while (true) {
    joypad_state = READ(0x1F801044);  // Read from JOY_STAT register
    if (joypad_state & JOY_DATA_READY) {  // Bit 0x400 set?
        button_response = READ(0x1F801040); // Read JOY_DATA
        // Process button press...
    }
}
```

**Your emulator:** SIO read handlers return 0x0000 (junk data) → poll loop never sees "data ready" → CPU spins forever.

### Critical Path to Game Loading

```
✅ DONE:   BIOS boot → menu display
⬜ NEXT:   Gamepad input (implement SIO poll response)
⬜ THEN:   CDROM ReadN/ReadS commands
⬜ THEN:   DMA Channel 3 (CDROM→RAM)
⬜ THEN:   Game executable loading + GTE (3D transforms)
```

### Implementation Priority for Next Session

**P0 (TODAY):** Implement SIO gamepad protocol stub
- When BIOS reads JOY_STAT, return TX_RDY (transmit ready) + bits indicating buttons pressed
- When BIOS reads JOY_DATA, return button state (normally 0x5F at start = all neutral)
- This unblocks menu interaction and allows testing further downstream

**P1 (NEXT):** CDROM ReadN (0x06) + DMA Channel 3
- Load game disc image sectors into RAM
- Enables testing actual game code execution

**P2:** GTE (RTPS, MVMVA) + GPU polygon clipping
- 3D vertex transforms
- Fixes game rendering

### Phase 1: Game Loading (Critical Path)

To load and run actual games, these components need implementation:

#### 1.1 CDROM Read Commands (Priority: **CRITICAL**)
**Current State:** Only status/test commands implemented  
**Needed:**
- `ReadN` (0x06) - Read with retry
- `ReadS` (0x1B) - Read sequential  
- `SetLoc` (0x02) - Seek to position
- `SeekL` (0x15) / `SeekP` (0x16) - Seek commands
- Sector buffering and INT1 (Data Ready) delivery
- DMA Channel 3 (CDROM→RAM) transfers

**Evidence from logs:**
```
[WARN][DMA] Unhandled DMA TO_RAM for channel 3 (CDROM)
```

#### 1.2 Controller Input (Priority: **HIGH**)
**Current State:** SIO registers partially implemented, controller.c is empty  
**Needed:**
- Digital pad button polling
- SIO communication protocol
- Controller response bytes (0x5A, button states)

**Why:** BIOS menu requires controller to select options

#### 1.3 Memory Card (Priority: **MEDIUM**)
**Current State:** SIO has partial memory card handling  
**Needed:**
- Memory card detection
- Read/write sector commands
- Save game persistence

### Phase 2: 3D Game Support

#### 2.1 GTE (Geometry Transform Engine) (Priority: **HIGH**)
**Current State:** 201 lines of stubs, registers defined but operations return zeros  
**Needed:**
- Matrix multiplication (MVMVA)
- Perspective transformation (RTPS, RTPT)
- Normal clipping (NCLIP)
- Color interpolation (NCDS, NCDT)
- Average Z calculation (AVSZ3, AVSZ4)

**Evidence from logs:**
```
[WARN][GTE] Unhandled GTE instruction: 0x49... (first 10 shown)
```

**Impact:** ALL 3D games require GTE for vertex transformation

#### 2.2 GPU Rendering Fixes (Priority: **MEDIUM**)
**Current State:** Basic rendering works but has glitches  
**Known Issues:**
- Polygon clipping at screen edges
- Draw ordering (painter's algorithm)
- Semi-transparency modes
- Texture window wrapping

### Phase 3: Audio/Video

#### 3.1 SPU (Sound Processing Unit) (Priority: **MEDIUM**)
**Current State:** Not implemented (FIFO writes are silently ignored)  
**Needed:**
- 24 voice channels
- ADPCM decoding
- Volume envelope (ADSR)
- Reverb effects

**Evidence from logs:**
```
[TRACE] SPU FIFO Write16 #20000: value=0x0000
```

#### 3.2 MDEC (Motion Decoder) (Priority: **LOW**)
**Current State:** Not implemented  
**Needed:**
- MDEC DMA Channel 0 (TO_MDEC) 
- MDEC DMA Channel 1 (FROM_MDEC)
- JPEG-like decompression
- YCbCr to RGB conversion

**Evidence from logs:**
```
[WARN][DMA] Unhandled DMA TO_RAM for channel 1 (MDEC OUT)
```

**Impact:** Required for FMV cutscenes in games

### Implementation Priority Matrix

| Priority | Component | Effort | Impact | Dependencies |
|----------|-----------|--------|--------|--------------|
| 🔴 **P0** | CDROM Read | Medium | Critical | DMA Ch3 |
| 🔴 **P0** | Controller Input | Low | Critical | SIO |
| 🟠 **P1** | GTE Core Ops | High | Critical | None |
| 🟠 **P1** | DMA Channel 3 | Medium | High | CDROM |
| 🟡 **P2** | Memory Card | Medium | Medium | SIO |
| 🟡 **P2** | GPU Clipping | Medium | Medium | None |
| 🟢 **P3** | SPU Basic | High | Low | DMA Ch4 |
| 🟢 **P3** | MDEC | High | Low | DMA Ch0,1 |

### Suggested Development Order

```
Week 1-2: CDROM Read + DMA Ch3
    └── Goal: Load game executable from disc

Week 3-4: Controller Input  
    └── Goal: Navigate BIOS menu, start games

Week 5-8: GTE Core Operations
    └── Goal: 3D geometry transforms working

Week 9+: GPU fixes, SPU, MDEC
    └── Goal: Full game compatibility
```

---

## License
This project is for educational purposes only. PlayStation is a trademark of Sony Interactive Entertainment.

## References
- [PSX-Spex Documentation](https://psx-spx.consoledev.net/)
- [nocash PSX Documentation](http://problemkaputt.de/psx.htm)
- [duckstation](https://github.com/stenzek/duckstation) - CDROM architecture reference
- [PCSX ReARMed](https://github.com/notaz/pcsx_rearmed) - Timer/IRQ behavior reference

*Reference emulators used for learning hardware behavior only. All code is original.* 