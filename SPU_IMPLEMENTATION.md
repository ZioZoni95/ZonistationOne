# SPU Implementation Checklist

## Overview
Complete PS1 SPU implementation based on DuckStation and PCSX-Redux references.
Target: 100% functional SPU with audio output, ADPCM, ADSR, DMA, IRQ, reverb.

---

## Phase 1: Data Structures
- [x] `include/spu.h`: Complete SPU structures
  - [x] `SpuVoice` (24 voices): volume L/R, pitch, ADPCM start/repeat, ADSR registers, current state
  - [x] `SpuState`: 512KB RAM, global registers, reverb, FIFO, noise, capture buffer
  - [x] Constants: `SPU_RAM_SIZE=524288`, `NUM_VOICES=24`, `SAMPLE_RATE=44100`, `CYCLES_PER_SPU_TICK=768`
  - [x] ADPCM block format: 16 bytes → 28 samples
  - [x] ADSR phase enum: Off, Attack, Decay, Sustain, Release
  - [x] Volume sweep state
  - [x] Gaussian interpolation table (512 entries)
  - [x] Reverb registers (32 halfwords)
  - [x] FIFO (32 halfwords)
  - [x] Transfer mode enum: Stopped, ManualWrite, DMAWrite, DMARead

## Phase 2: MMIO Registers
- [x] Voice registers (0x1C00-0x1D7F): 24 × 0x10 bytes
  - [x] Volume L/R (0x0, 0x2)
  - [x] Pitch (0x4)
  - [x] ADPCM start address (0x6)
  - [x] ADSR low/high (0x8, 0xA)
  - [x] ADSR volume (0xC)
  - [x] ADPCM repeat address (0xE)
- [x] Control registers (0x1D80-0x1DBF)
  - [x] Main volume L/R (0x1D80, 0x1D82)
  - [x] Reverb output volume L/R (0x1D84, 0x1D86)
  - [x] Key On L/H (0x1D88, 0x1D8A)
  - [x] Key Off L/H (0x1D8C, 0x1D8E)
  - [x] Pitch modulation L/H (0x1D90, 0x1D92)
  - [x] Noise mode L/H (0x1D94, 0x1D96)
  - [x] Reverb on L/H (0x1D98, 0x1D9A)
  - [x] ENDX L/H (0x1D9C, 0x1D9E) - read-only
  - [x] Reverb base address (0x1DA2)
  - [x] IRQ address (0x1DA4)
  - [x] Transfer address (0x1DA6)
  - [x] Transfer data FIFO (0x1DA8)
  - [x] SPU Control (0x1DAA)
  - [x] SPU Status (0x1DAE) - read-only
  - [x] CD audio volume L/R (0x1DB0, 0x1DB2)
  - [x] External volume L/R (0x1DB4, 0x1DB6)
  - [x] Main volume current L/R (0x1DB8, 0x1DBA) - read-only
- [x] Reverb registers (0x1DC0-0x1DFF): 32 halfwords
- [x] Extended read-only (0x1E00-0x1E5F): current voice volume L/R

## Phase 3: ADPCM Decoder
- [x] Block format: header (shift/filter, flags) + 28 nibbles
- [x] 5 prediction filters (coeffs: 0,0 / 60,0 / 115,-52 / 98,-55 / 122,-60)
- [x] Decode 28 samples from 14 bytes
- [x] Loop flags: loop_end, loop_repeat, loop_start
- [x] Trailing samples (3) for interpolation
- [x] Gaussian 4-tap interpolation
- [x] Block loading on demand

## Phase 4: ADSR Envelope
- [x] Phases: Off → Attack → Decay → Sustain → Release → Off
- [x] Attack: linear/exponential to 32767
- [x] Decay: exponential to sustain level
- [x] Sustain: linear/exponential, direction configurable
- [x] Release: exponential to 0
- [x] Rate formulas (Neill Corlett model)
- [x] Rate 0x7F = never ticks
- [x] Envelope step/counter per phase
- [x] Volume target calculation

## Phase 5: Volume & Mixing
- [x] Volume sweep (fixed or envelope-modulated)
- [x] Per-voice volume L/R calculation
- [x] Pitch modulation: voice N uses voice N-1 output
- [x] Noise generator (Dr. Hell): 64-entry wave table, LFSR, clock
- [x] Mixing: sum 24 voices → clamp to 16-bit
- [x] Apply main volume sweep
- [x] Mix CD audio (scaled by CD volume)
- [x] Mix reverb output

## Phase 6: SPU RAM & DMA
- [x] 512KB SPU RAM (byte array)
- [x] Transfer address (0x1DA6): byte offset = reg × 8
- [x] Auto-increment by 2 per halfword, wrap at 512KB
- [x] FIFO: 32 halfwords (64 bytes)
- [x] Transfer modes: Stopped, ManualWrite, DMAWrite, DMARead
- [x] DMARead: fill FIFO from RAM, 1 halfword per 16 ticks
- [x] DMAWrite: drain FIFO to RAM, 1 halfword per 16 ticks
- [x] DMA request signals: full (read) / empty (write)
- [x] DMA underflow: repeat last value
- [x] DMA overflow: drop excess

## Phase 7: IRQ System
- [x] IRQ9 (SPU RAM IRQ)
- [x] IRQ address (0x1DA4): halfword offset → byte = reg × 8
- [x] Trigger on crossing IRQ boundary:
  - [x] ADPCM block reads
  - [x] Transfer address writes
  - [x] DMA FIFO read/write
  - [x] Manual transfer writes
  - [x] Capture buffer writes
  - [x] Late IRQ checks (when IRQ address changes)
- [x] SPUCNT.irq9_enable bit: set/clear flag
- [x] Clear irq9_flag when irq9_enable=0
- [x] Signal IRQ9 to interconnect

## Phase 8: Capture Buffer
- [x] 4 channels × 0x400 halfwords each
- [x] Channel 0: CD audio left
- [x] Channel 1: CD audio right
- [x] Channel 2: Voice 1 output
- [x] Channel 3: Voice 3 output
- [x] Write during mixing
- [x] Increment position: +2 per sample, wrap at 0x400
- [x] SPUSTAT.second_half_capture_buffer flag
- [x] IRQ check on write

## Phase 9: Reverb
- [x] 32 reverb registers
- [x] Reverb work area in SPU RAM
- [x] Memory address wrapping logic
- [x] Downsample: 44100→22050Hz (FIR 39-tap, zeros removed → 20 taps)
- [x] IIR input filters (IIR_COEF, IIR_ALPHA)
- [x] Same-side reflection (IIR)
- [x] Comb filter (early echo): ACC_COEF_A/B/C/D
- [x] All-pass filter 1: FB_SRC_A, FB_ALPHA
- [x] All-pass filter 2: FB_SRC_B, FB_X
- [x] Output: clamp, duplicate for upsample
- [x] Upsample: 22050→44100Hz (each sample duplicated)
- [x] Reverb output volume L/R
- [x] iiasm special case (IIR_ALPHA == -32768)
- [x] Master reverb enable

## Phase 10: Audio Output (SDL)
- [x] SDL audio device initialization
- [x] Audio callback function
- [x] Sample generation pipeline per 44100Hz frame
- [x] Batch processing: generate N samples per callback
- [x] Output: interleaved stereo (L,R,L,R,...)
- [x] SDL audio format: S16, 44100Hz, stereo
- [x] Volume clipping/clamping
- [x] Mute handling (SPUCNT bit 14)

## Phase 11: Core Integration
- [x] `src/interconnect.c`: route SPU MMIO reads/writes
- [x] `src/bus.c`: SPU address range handling
- [x] `src/main.c`: SPU sample generation in emulation loop
- [x] Event scheduler: SPU tick events
- [x] CD-ROM audio integration (cdrom_audio.c)
- [x] CPU cycle counting for SPU timing
- [x] Build system: update Makefile with new SPU modules

## Phase 12: Logging & ImGui Debug
- [x] SPU log category with levels (ERROR, WARN, INFO, DEBUG, TRACE)
- [x] Log on: Key On/Off, DMA transfers, IRQ triggers, register changes
- [x] Rate limiting for frequent logs
- [x] ImGui window: SPU State Viewer
  - [x] Voice list (24 voices): status, ADSR phase, volume, pitch
  - [x] Global registers display
  - [x] SPU RAM dump (hex viewer)
  - [x] FIFO state
  - [x] Capture buffer state
  - [x] Reverb register display
- [x] ImGui window: Audio Debug
  - [x] Waveform visualization
  - [x] Sample rate display
  - [x] Audio output level meters

## Build & Test
- [x] Clean build: `make clean && make`
- [x] BIOS boot test: no audio errors
- [ ] Game test with audio: verify sound output
- [ ] IRQ test: games using SPU IRQ (e.g., Crash Team Racing)
- [ ] DMA test: verify sound data transfer
- [ ] Performance: no audio glitches at 60fps

---

## Status
- Completed: Phases 1-12 (full SPU implementation, SDL audio, ImGui debug, logging)
- In progress: Game testing with audio
- Remaining: Game disc testing, performance tuning

## Build Result
- `make clean && make` succeeds with 0 errors
- Binary size: ~4.8MB
- All SPU modules compile cleanly
- `spu_step()` integrated in main.c emulation loop
- ImGui SPU debug window: voice list, audio meters, register display
- SPU log component added to ImGui log viewer

## Files Created/Modified
- `include/spu.h` - Complete SPU header with all structures and constants
- `include/log.h` - Added SPU log category and macros
- `src/log.c` - Added "SPU" to category names
- `src/spu.c` - MMIO read/write, init, reset, key on/off, control register
- `src/spu_voice.c` - ADPCM decoder, Gaussian interpolation, voice sample generation
- `src/spu_adsr.c` - ADSR envelope processing
- `src/spu_mixing.c` - Noise generator, capture buffer, reverb, main sample pipeline
- `src/spu_dma.c` - DMA transfer, FIFO, manual transfer
- `src/spu_irq.c` - IRQ9 handling
- `Makefile` - Added new SPU source files
