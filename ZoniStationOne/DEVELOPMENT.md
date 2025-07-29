# ZoniStationOne Development Guide

## 🎯 **Current Development Status**

### ✅ **Phase 1 Complete: Core Foundation**
- **Memory System**: Complete PlayStation memory map
- **CPU Core**: Basic MIPS instruction set working
- **BIOS Loading**: Complete BIOS loading and validation
- **BIOS Execution**: Working BIOS execution with timeout
- **Hardware Access**: Basic hardware register support
- **Development Tools**: Comprehensive logging and debugging

### 🔄 **Phase 2 In Progress: Hardware Emulation**
- **GPU Implementation**: ✅ Graphics Processing Unit for display
- **SPU Implementation**: ✅ Sound Processing Unit for audio (Basic skeleton)
- **CD-ROM Emulation**: ✅ CD-ROM drive emulation (Basic skeleton)
- **Controller Input**: Input device emulation

## 🚀 **Next Development Priorities**

### **1. BIOS Loop Analysis** 🔍 **HIGH PRIORITY**

#### **Current Issue**
The BIOS is stuck in a loop at addresses `0xBFC00250-0xBFC00270`:
- Writing zeros to memory addresses `0x00000000` through `0x00000070`
- Branch instruction `BNE` keeps jumping back to `0xBFC00250`
- No interaction with GPU, SPU, or CD-ROM yet

#### **Goal**
Understand what the BIOS is waiting for and get it to progress beyond this initialization loop.

#### **Implementation Plan**
1. **Analyze BIOS Loop**
   - Examine the condition in the BNE instruction
   - Understand what the BIOS is checking for
   - Identify missing hardware components

2. **Implement Missing Components**
   - Interrupt handling system
   - Timer functionality
   - Controller support
   - Additional hardware registers

3. **Debug BIOS Progress**
   - Monitor when BIOS accesses different hardware
   - Track BIOS progression through different phases
   - Identify blocking conditions

#### **Expected Result**
BIOS progresses beyond the initialization loop and starts using GPU, SPU, and CD-ROM.

### **2. GPU Implementation** 🎯 **HIGH PRIORITY**

#### **Goal**
Display the classic PlayStation boot screen and provide visual feedback.

#### **Benefits**
- Visual confirmation that BIOS is working
- Real PlayStation experience
- Foundation for game graphics
- Better debugging with visual output

#### **Implementation Plan**
1. **SDL2 Graphics Setup**
   - Initialize SDL2 video subsystem
   - Create window and renderer
   - Set up framebuffer management

2. **PlayStation GPU Emulation**
   - Implement basic GPU command processing
   - Handle framebuffer writes
   - Manage display timing

3. **BIOS Screen Display**
   - Show PlayStation boot screen
   - Handle "no disc" screen
   - Display BIOS messages

#### **Expected Result**
Classic PlayStation boot screen with Sony logo and "Please insert a PlayStation or PlayStation 2 format disc" message.

### **3. SPU Implementation** 🎵 **MEDIUM PRIORITY**

#### **Current Status** ✅ **BASIC SKELETON IMPLEMENTED**
- **SPU Structure**: Basic SPU emulation structure created
- **Memory Integration**: SPU integrated into memory system
- **Hardware Routing**: SPU registers routed through hardware system
- **Initialization**: SPU initialization and shutdown in main.c

#### **Goal**
Provide audio output for PlayStation sounds and music.

#### **Benefits**
- Complete audio experience
- BIOS audio feedback
- Foundation for game audio
- Enhanced immersion

#### **Implementation Plan**
1. **SDL2 Audio Setup**
   - Initialize SDL2 audio subsystem
   - Set up audio buffer management
   - Configure audio format (44.1 kHz, 16-bit)

2. **PlayStation SPU Emulation**
   - Implement basic SPU command processing
   - Handle audio buffer writes
   - Manage audio timing

3. **BIOS Audio**
   - Play BIOS startup sounds
   - Handle "no disc" audio
   - Audio feedback for user actions

#### **Expected Result**
PlayStation BIOS audio including startup sounds and disc-related audio cues.

### **4. CD-ROM Emulation** 💿 **MEDIUM PRIORITY**

#### **Current Status** ✅ **BASIC SKELETON IMPLEMENTED**
- **CD-ROM Structure**: Basic CD-ROM emulation structure created
- **Memory Integration**: CD-ROM integrated into memory system
- **Hardware Routing**: CD-ROM registers routed through hardware system
- **Initialization**: CD-ROM initialization and shutdown in main.c

#### **Goal**
Properly handle CD-ROM drive emulation and disc detection.

#### **Benefits**
- Proper BIOS disc detection
- Handle "no disc" state correctly
- Foundation for game loading
- More accurate PlayStation behavior

#### **Implementation Plan**
1. **CD-ROM Controller**
   - Implement CD-ROM controller registers
   - Handle CD-ROM commands
   - Manage disc status

2. **Disc Detection**
   - Detect "no disc" state
   - Handle disc insertion/removal
   - Provide disc information

3. **BIOS Integration**
   - BIOS disc detection
   - Proper "no disc" handling
   - Disc-related BIOS functions

#### **Expected Result**
BIOS properly detects "no disc" state and shows appropriate messages.

### **5. Controller Input** 🎮 **LOW PRIORITY**

#### **Goal**
Handle user input through PlayStation controller emulation.

#### **Benefits**
- User interaction with BIOS
- Navigate BIOS menus
- Foundation for game input
- Complete user experience

#### **Implementation Plan**
1. **SDL2 Input Setup**
   - Initialize SDL2 input subsystem
   - Handle keyboard/mouse input
   - Map to PlayStation controller

2. **PlayStation Controller Emulation**
   - Implement controller protocol
   - Handle button presses
   - Manage controller state

3. **BIOS Integration**
   - BIOS input handling
   - Menu navigation
   - User interaction

#### **Expected Result**
Ability to interact with BIOS using keyboard/mouse mapped to PlayStation controller.

## 🔧 **Development Guidelines**

### **Code Quality Standards**
1. **Clean Code**: Well-documented, readable implementation
2. **Error Handling**: Comprehensive error checking and reporting
3. **Logging**: Professional logging with appropriate verbosity
4. **Testing**: Test each component thoroughly before integration
5. **Documentation**: Keep documentation updated with changes

### **Performance Guidelines**
1. **Efficiency**: Optimize for performance where appropriate
2. **Accuracy**: Prioritize accuracy over speed for emulation
3. **Timing**: Maintain proper timing relationships
4. **Memory**: Efficient memory usage and management

### **Debugging Guidelines**
1. **Logging**: Use comprehensive logging for development
2. **Error Messages**: Clear, descriptive error messages
3. **Validation**: Validate inputs and state at each step
4. **Testing**: Test incrementally and thoroughly

## 📊 **Current Architecture**

### **Memory System** ✅
```
RAM: 0x00000000-0x007FFFFF (8MB)
BIOS: 0x1FC00000-0x1FC7FFFF (512KB)
Scratchpad: 0x1F800000-0x1F8003FF (1KB)
Hardware Registers: 0x1F801000-0x1F802FFF (8KB)
Cache Control: 0xFFFE0000-0xFFFEFFFF (64KB)
```

### **CPU System** ✅
- **Architecture**: MIPS R3000A
- **Registers**: 32 GPR + LO/HI + CP0
- **Instructions**: Core MIPS instruction set
- **Pipeline**: Fetch → Decode → Execute
- **Load Delay**: Proper slot handling
- **Exception Handling**: SYSCALL and BREAK working

### **BIOS System** ✅
- **Loading**: Real PlayStation BIOS loading
- **Execution**: Controlled execution with timeout
- **Validation**: BIOS file validation and identification
- **Logging**: Comprehensive development logging

### **Hardware System** ✅
- **GPU**: Basic GPU structure and hardware routing
- **SPU**: Basic SPU structure and hardware routing
- **CD-ROM**: Basic CD-ROM structure and hardware routing
- **Hardware Registers**: Complete hardware register access

## 🎮 **Testing Strategy**

### **Current Testing**
- **BIOS Loading**: Validates BIOS files correctly
- **BIOS Execution**: Runs without errors for 10K cycles (testing mode)
- **Memory Access**: Proper memory protection and access
- **CPU Instructions**: All implemented instructions work
- **Hardware Access**: Basic hardware register support
- **SPU/CD-ROM**: Basic structures initialized and integrated

### **Next Testing Priorities**
1. **BIOS Loop Analysis**: Understand and fix BIOS loop
2. **GPU Testing**: Verify display output and timing
3. **SPU Testing**: Verify audio output and quality
4. **CD-ROM Testing**: Verify disc detection and handling
5. **Controller Testing**: Verify input handling and mapping

## 📈 **Success Metrics**

### **Phase 1 Metrics** ✅ **ACHIEVED**
- [x] **BIOS loads successfully**: SCPH-1001 BIOS loaded
- [x] **BIOS executes without errors**: No unknown instructions
- [x] **Memory system works**: Complete memory map
- [x] **CPU instructions work**: All implemented instructions
- [x] **Hardware access works**: Basic hardware support
- [x] **Development tools work**: Good logging and debugging

### **Phase 2 Metrics** 📋 **IN PROGRESS**
- [x] **SPU skeleton implemented**: Basic SPU structure and integration
- [x] **CD-ROM skeleton implemented**: Basic CD-ROM structure and integration
- [ ] **BIOS progresses beyond loop**: BIOS moves past initialization
- [ ] **GPU displays boot screen**: Visual PlayStation boot screen
- [ ] **SPU provides audio**: BIOS audio output
- [ ] **CD-ROM handles disc state**: Proper disc detection
- [ ] **Controller accepts input**: User interaction with BIOS

## 🚀 **Getting Started with Current Development**

### **Current Focus**
1. **BIOS Loop Analysis** - Understand why BIOS is stuck
2. **GPU Implementation** - Provides immediate visual feedback
3. **SPU/CD-ROM Enhancement** - Complete the basic implementations

### **Development Setup**
```bash
# Build the emulator
cd ZoniStationOne
make clean
make

# Run with current BIOS execution (10K cycles for testing)
./bin/zonistationone

# Expected output: BIOS loads and executes for 10K cycles
# Current behavior: BIOS stuck in loop at 0xBFC00250-0xBFC00270
```

### **Next Steps**
1. **Analyze BIOS loop** to understand what it's waiting for
2. **Implement missing hardware** components (interrupts, timers)
3. **Enhance GPU implementation** for visual feedback
4. **Complete SPU/CD-ROM** implementations

## 📚 **Resources**

### **Technical References**
- **PCSX-ReARMed**: Reference implementation
- **PlayStation Technical Reference**: Hardware documentation
- **SDL2 Documentation**: Graphics and audio library
- **MIPS R3000A Documentation**: CPU specifications

### **Development Tools**
- **GCC Compiler**: C compilation
- **SDL2 Libraries**: Graphics, audio, and input
- **OpenGL**: Graphics rendering
- **Make**: Build system

---

**Status**: 🟡 **Development Phase** - Core systems complete, SPU/CD-ROM skeletons added, BIOS loop analysis needed 