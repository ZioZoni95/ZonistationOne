# Missing Components Checklist
## Quick Reference for ZonistationOne Development

**Last Updated**: January 6, 2026

---

## 🚨 Critical Path to First Playable Game

### ✅ What You Have (Working)
- [x] CPU (MIPS R3000A) - COMPLETE
- [x] RAM (2MB) - COMPLETE
- [x] VRAM (1MB) - COMPLETE
- [x] BIOS boot - COMPLETE
- [x] Event scheduler - COMPLETE
- [x] CDROM (Test/GetStat/GetID) - PARTIAL
- [x] GPU (basic rendering) - PARTIAL
- [x] DMA Channel 2 (GPU) - WORKING

### ❌ What You're Missing (Blockers)

#### 🔴 **CRITICAL - P0** (Required for ANY game to work)

| # | Component | Status | Priority | Effort | Blocking |
|---|-----------|--------|----------|--------|----------|
| 1 | **Controller Input** | ❌ Missing | P0 | 2-3 days | ALL GAMES |
| 2 | **CDROM Read (ReadN/ReadS)** | ❌ Missing | P0 | 4-5 days | ALL GAMES |
| 3 | **DMA Channel 3 (CDROM)** | ❌ Missing | P0 | 2-3 days | ALL GAMES |
| 4 | **SIO Protocol (Complete)** | 🔴 Stubs | P0 | 2-3 days | Controller |

**Total P0 Effort**: ~10-14 days

#### 🟠 **HIGH PRIORITY - P1** (Required for 3D games)

| # | Component | Status | Priority | Effort | Blocking |
|---|-----------|--------|----------|--------|----------|
| 5 | **GTE (Core ops)** | 🔴 Stubs | P1 | 1-2 weeks | 3D GAMES |
| 6 | **DMA Channel 6 (OTC)** | ❌ Missing | P1 | 1 day | Display lists |
| 7 | **Memory Card I/O** | 🔴 Stubs | P1 | 3-4 days | Save data |
| 8 | **Timer System (Complete)** | ⚠️ Partial | P1 | 2-3 days | Timing bugs |

**Total P1 Effort**: ~3-4 weeks

#### 🟡 **MEDIUM PRIORITY - P2** (Nice to have)

| # | Component | Status | Priority | Effort | Blocking |
|---|-----------|--------|----------|--------|----------|
| 9 | **SPU (Audio)** | 🔴 Stubs | P2 | 2-3 weeks | Sound |
| 10 | **DMA Channel 4 (SPU)** | ❌ Missing | P2 | 1 day | SPU |
| 11 | **GPU Clipping/Blending** | ⚠️ Buggy | P2 | 1 week | Visual bugs |
| 12 | **Analog Controller** | ❌ Missing | P2 | 2-3 days | DualShock |

**Total P2 Effort**: ~4-5 weeks

#### 🟢 **LOW PRIORITY - P3** (Optional features)

| # | Component | Status | Priority | Effort | Blocking |
|---|-----------|--------|----------|--------|----------|
| 13 | **MDEC (FMV decoder)** | ❌ Missing | P3 | 2 weeks | Cutscenes |
| 14 | **DMA Ch 0/1 (MDEC)** | ❌ Missing | P3 | 1 day | MDEC |
| 15 | **Multitap** | ❌ Missing | P3 | 3-4 days | 4-player |
| 16 | **CPU Recompiler** | ❌ Missing | P3 | 4+ weeks | Performance |
| 17 | **GPU HW Renderer** | ❌ Missing | P3 | 3-4 weeks | Speed |
| 18 | **Save States** | ❌ Missing | P3 | 1 week | User feature |

---

## 📋 Implementation Order (Recommended)

### **Week 1-2: Core Input & Storage**
```
Day 1-3:   Implement Digital Controller (src/controller.c)
           ├─ SIO byte protocol
           ├─ Button state (0x5A41 response)
           └─ SDL keyboard mapping
           
Day 4-8:   CDROM Read Commands (src/cdrom.c)
           ├─ Command 0x02 SetLoc
           ├─ Command 0x06 ReadN
           ├─ Command 0x1B ReadS
           ├─ Sector buffer (2048 bytes)
           └─ Data Ready interrupt
           
Day 9-11:  DMA Channel 3 (src/dma.c)
           ├─ CDROM→RAM transfer
           ├─ Linked-list mode
           └─ Completion IRQ
```

### **Week 3-4: 3D Graphics Foundation**
```
Day 12-18: GTE Implementation (src/gte.c)
           ├─ RTPS (perspective transform)
           ├─ RTPT (triple transform)
           ├─ MVMVA (matrix multiply)
           ├─ NCLIP (normal clip)
           └─ Fixed-point math
           
Day 19-20: DMA Channel 6 OTC (src/dma.c)
           └─ Ordering table clear
           
Day 21-24: Memory Card I/O (src/sio.c)
           ├─ Read/Write commands
           ├─ Checksum validation
           └─ .mcr file save/load
```

### **Week 5-8: Audio & Polish** (Optional)
```
Week 5-7:  SPU Implementation (src/spu.c)
           ├─ 24 ADPCM voices
           ├─ Voice ADSR
           ├─ Reverb engine
           ├─ Audio mixing
           └─ DMA Channel 4
           
Week 8:    GPU Fixes (src/gpu.c)
           ├─ Polygon clipping
           ├─ Texture window
           ├─ Semi-transparency
           └─ Mask bits
```

---

## 🎯 Milestone Targets

### **Milestone 1: "Hello, Controller!"** (Week 1-2)
**Goal**: Navigate BIOS menu with keyboard input  
**Required Components**:
- ✅ Digital controller polling
- ✅ SIO byte transfers
- ✅ Button state reading

**Success Criteria**:
- Can move cursor in BIOS menu
- Can select/cancel with buttons
- No crashes during input

---

### **Milestone 2: "First Boot!"** (Week 2-3)
**Goal**: Boot a game past the logo screen  
**Required Components**:
- ✅ CDROM ReadN/ReadS
- ✅ DMA Channel 3 (CDROM→RAM)
- ✅ File loading from ISO/CUE

**Success Criteria**:
- Game logo appears
- Game starts loading
- Reaches in-game menu (even if black screen)

---

### **Milestone 3: "First Playable Game!"** (Week 4-5)
**Goal**: Play a simple 3D game (Crash Bandicoot, Spyro)  
**Required Components**:
- ✅ GTE core operations
- ✅ DMA Channel 6 (OTC)
- ✅ Memory card saves

**Success Criteria**:
- 3D graphics render correctly
- Character moves on screen
- Game logic runs
- Can save progress

---

### **Milestone 4: "Full Experience"** (Week 8+)
**Goal**: Audio working, high compatibility  
**Required Components**:
- ✅ SPU audio
- ✅ DMA Channel 4 (SPU)
- ✅ GPU visual fixes

**Success Criteria**:
- Music plays
- Sound effects work
- Games look correct
- 50+ games playable

---

## 🔧 DuckStation Reference Files (Study These)

### **For Controller Implementation**:
```
📁 duckstation/src/core/
   ├─ digital_controller.cpp  [STUDY THIS]
   ├─ digital_controller.h
   ├─ controller.cpp
   ├─ controller.h
   └─ pad.cpp                 [SIO management]
```

### **For CDROM Read**:
```
📁 duckstation/src/core/
   ├─ cdrom.cpp               [Lines 500-800: Read logic]
   ├─ cdrom.h
   └─ cdrom_async_reader.cpp  [Optional: async I/O]
```

### **For GTE**:
```
📁 duckstation/src/core/
   ├─ gte.cpp                 [COMPLETE REFERENCE]
   ├─ gte.h
   └─ gte_types.h
```

### **For DMA**:
```
📁 duckstation/src/core/
   ├─ dma.cpp                 [All 7 channels]
   └─ dma.h
```

### **For SPU** (later):
```
📁 duckstation/src/core/
   ├─ spu.cpp                 [Full audio engine]
   └─ spu.h
```

### **For Memory Cards**:
```
📁 duckstation/src/core/
   ├─ memory_card.cpp         [Protocol implementation]
   ├─ memory_card.h
   └─ memory_card_image.cpp   [File I/O]
```

---

## 📊 Current Completion Status

```
┌─────────────────────────────────────────────────┐
│  ZonistationOne Emulator Completeness          │
├─────────────────────────────────────────────────┤
│                                                 │
│  ████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░  32%  │
│                                                 │
│  ✅ Core Architecture      [████████████] 100% │
│  ✅ CPU Emulation          [████████████] 100% │
│  ✅ Memory (RAM/VRAM)      [████████████] 100% │
│  ⚠️  GPU Rendering         [██████░░░░░░]  50% │
│  ⚠️  CDROM Controller      [████░░░░░░░░]  40% │
│  ⚠️  DMA (7 channels)      [██░░░░░░░░░░]  20% │
│  🔴 GTE (3D Geometry)      [░░░░░░░░░░░░]   5% │
│  🔴 Controller Input       [░░░░░░░░░░░░]   0% │
│  🔴 Memory Cards           [█░░░░░░░░░░░]  10% │
│  🔴 SPU (Audio)            [░░░░░░░░░░░░]   0% │
│  ❌ MDEC (Video)           [░░░░░░░░░░░░]   0% │
│  ❌ Advanced Features      [░░░░░░░░░░░░]   0% │
│                                                 │
└─────────────────────────────────────────────────┘
```

---

## 🚀 Quick Start Commands

### **Build Current Version**
```bash
make clean && make
./myps1_emu --debug --log-single-file
```

### **Test with Game**
```bash
# After implementing controller + CDROM read:
./myps1_emu --game games/crash_bandicoot.cue
```

### **Compare with DuckStation**
```bash
# Run DuckStation for reference behavior:
cd duckstation
./build/bin/duckstation-qt
```

---

## 📝 Implementation Notes

### **Controller Button Mapping** (Digital Pad)
```c
// Button bits (active LOW)
#define BTN_SELECT   0x0001
#define BTN_L3       0x0002  // (not on digital pad)
#define BTN_R3       0x0004  // (not on digital pad)
#define BTN_START    0x0008
#define BTN_UP       0x0010
#define BTN_RIGHT    0x0020
#define BTN_DOWN     0x0040
#define BTN_LEFT     0x0080
#define BTN_L2       0x0100
#define BTN_R2       0x0200
#define BTN_L1       0x0400
#define BTN_R1       0x0800
#define BTN_TRIANGLE 0x1000
#define BTN_CIRCLE   0x2000
#define BTN_CROSS    0x4000
#define BTN_SQUARE   0x8000
```

### **CDROM Sector Format**
```c
// Mode 1 (data): 2048 bytes
// Mode 2 Form 1: 2048 bytes  
// Mode 2 Form 2: 2324 bytes
// Raw (with headers): 2352 bytes

typedef struct {
    uint8_t sync[12];       // 00 FF FF FF FF FF FF FF FF FF FF 00
    uint8_t header[4];      // MM:SS:FF + mode
    uint8_t data[2048];     // User data
    uint8_t edc[4];         // Error detection code
    uint8_t ecc[276];       // Error correction code
} CdromSector_Raw;
```

### **GTE Fixed-Point Math**
```c
// All GTE operations use fixed-point:
// - 16.16 fixed point for most values
// - 12.4 for some values
// - Special handling for overflow

// Example: RTPS (Perspective Transform)
// Input:  V0 (vector), RT (rotation), TR (translation)
// Output: SXY FIFO (screen coordinates), SZ (depth)

int32_t mac1 = (RT[0][0] * V0.x + RT[0][1] * V0.y + RT[0][2] * V0.z) >> 12;
// ... more matrix math
```

---

## 🎮 Test Game Recommendations

### **Easy to Emulate** (Start Here)
1. **Crash Bandicoot** - Simple 3D, good for GTE testing
2. **Tomb Raider** - Basic 3D engine
3. **Spyro the Dragon** - Colorful 3D, easy to debug

### **Medium Difficulty**
4. **Final Fantasy VII** - Complex but well-documented
5. **Gran Turismo** - Racing game, good for SPU testing
6. **Tekken 3** - Fighting game, fast response needed

### **Hard to Emulate** (Save for Later)
7. **Metal Gear Solid** - Complex systems, MDEC cutscenes
8. **Silent Hill** - Fog effects, advanced GPU features
9. **Ridge Racer Type 4** - Complex rendering

---

## ✅ Daily Checklist Template

```markdown
## Daily Progress - [DATE]

### Today's Goal:
- [ ] Task 1
- [ ] Task 2
- [ ] Task 3

### Code Changes:
- Modified: `src/filename.c` - Description
- Added: `include/newfile.h` - Description

### Testing:
- [ ] Compiles without errors
- [ ] No new crashes
- [ ] Tested with: [game/test name]

### Blockers:
- None / [Describe issue]

### Tomorrow's Plan:
- [ ] Next task
```

---

## 🆘 Help Resources

### **Documentation**
- ✅ Your DOCS/ folder (excellent!)
- ✅ PSX-SPX (nocash) - http://problemkaputt.de/psx-spx.htm
- ✅ DuckStation source code
- ✅ PCSX-Redux source code

### **Communities**
- /r/EmuDev (Reddit)
- #emulation on Discord
- Emulation Development Discord servers

### **Reference Emulators**
1. **DuckStation** - Modern C++, excellent reference
2. **PCSX-Redux** - Clean C++ implementation
3. **Mednafen** - Cycle-accurate, good for testing
4. **No$PSX** - Debugger extraordinaire

---

## 📈 Progress Tracking

| Week | Milestone | Status | Games Playable |
|------|-----------|--------|----------------|
| 0 | BIOS Menu | ✅ DONE | 0 |
| 1-2 | Controller + CDROM | 🟡 IN PROGRESS | 0 |
| 3-4 | GTE + Memory Cards | ⚪ TODO | 0 |
| 5-6 | First Playable Game | ⚪ TODO | 1-5 |
| 7-8 | SPU Audio | ⚪ TODO | 10-20 |
| 9+ | Polish & Compatibility | ⚪ TODO | 50+ |

---

**Remember**: Don't try to implement everything at once!  
Focus on **P0 components first** to get your first game running, then iterate.

**Good luck! You're 32% there already!** 🎮✨
