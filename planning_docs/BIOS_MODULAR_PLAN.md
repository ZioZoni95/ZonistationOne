# BIOS Modular System Design
**Based on DuckStation Architecture**
**Date:** January 7, 2026

## Overview
Refactor the BIOS subsystem into a modular, DuckStation-inspired design with:
- Multi-BIOS support (SCPH-1001, SCPH-5500, SCPH-7001, etc.)
- MD5 checksum verification
- BIOS call interception and logging
- Region detection (NTSC-J, NTSC-U, PAL)
- Fast-boot patching support
- O(1) complexity for all operations

## Architecture

### Module Structure
```
include/bios/
  ├── bios_types.h     # Type definitions, enums, constants
  └── bios_core.h      # Public API

src/bios/
  └── bios_core.c      # Implementation
```

### Type System (bios_types.h)

#### Constants
```c
#define BIOS_SIZE 524288        // 512KB (0x80000)
#define BIOS_BASE_ADDR 0x1FC00000
#define BIOS_HASH_SIZE 16       // MD5 hash size
```

#### Enums
```c
typedef enum {
    REGION_AUTO = 0,
    REGION_NTSC_J,    // Japan
    REGION_NTSC_U,    // North America
    REGION_PAL,       // Europe/Australia
} BiosRegion;

typedef enum {
    FASTBOOT_UNSUPPORTED = 0,
    FASTBOOT_TYPE1,
    FASTBOOT_TYPE2,
} BiosFastBootPatch;
```

#### Structures
```c
// BIOS image information (based on DuckStation)
typedef struct {
    const char* description;           // "SCPH-1001 (v2.2 12-04-95 A)"
    BiosRegion region;                 // Region code
    bool region_check;                 // Has region check
    BiosFastBootPatch fastboot_patch;  // Fast boot support
    uint8_t priority;                  // Selection priority
    uint8_t hash[BIOS_HASH_SIZE];      // MD5 hash
} BiosImageInfo;

// Loaded BIOS image
typedef struct {
    uint8_t data[BIOS_SIZE];           // 512KB ROM data
    uint8_t hash[BIOS_HASH_SIZE];      // Computed MD5 hash
    const BiosImageInfo* info;         // Matched BIOS info (NULL if unknown)
    BiosRegion region;                 // Detected region
    bool verified;                     // Hash matched known BIOS
} BiosState;

// BIOS function call info (for logging/interception)
typedef struct {
    uint8_t table;     // 'A', 'B', or 'C'
    uint8_t function;  // Function number
    const char* name;  // Function name
} BiosFunctionInfo;
```

### Core API (bios_core.h)

#### Initialization & Loading
```c
// Initialize BIOS state
void bios_init(BiosState* bios);

// Load BIOS from file with verification
bool bios_load(BiosState* bios, const char* path);

// Load BIOS and verify against known hashes
bool bios_load_and_verify(BiosState* bios, const char* path);
```

#### Memory Access (O(1))
```c
// Read operations
uint32_t bios_load32(const BiosState* bios, uint32_t offset);
uint16_t bios_load16(const BiosState* bios, uint32_t offset);
uint8_t bios_load8(const BiosState* bios, uint32_t offset);
```

#### Verification & Info
```c
// Compute MD5 hash of BIOS data
void bios_compute_hash(BiosState* bios);

// Find BIOS info by hash
const BiosImageInfo* bios_find_info_by_hash(const uint8_t hash[BIOS_HASH_SIZE]);

// Get BIOS description string
const char* bios_get_description(const BiosState* bios);

// Check if BIOS is known/verified
bool bios_is_verified(const BiosState* bios);
```

#### BIOS Call Interception
```c
// Decode BIOS function call
const BiosFunctionInfo* bios_decode_function(uint8_t table, uint8_t function);

// Log BIOS function call (replaces CPU logging)
void bios_log_function_call(uint32_t pc, uint8_t table, uint8_t function, uint32_t ra);
```

## Known BIOS Database

### DuckStation Hash Database (Top 10 for each region)
**NTSC-U (North America):**
1. `924e392ed05558ffdb115408c263dccf` - SCPH-1001, 5003 (v2.2 12-04-95 A) ⭐
2. `490f666e1afb15b7362b406ed1cea246` - SCPH-5501, 5503, 7003 (v3.0 11-18-96 A) ⭐
3. `1e68c231d0896b7eadcad1d7d8e76129` - SCPH-7001, 7501, 9001 (v4.1 12-16-97 A) ⭐
4. `6e3735ff4c7dc899ee98981385f6f3d0` - SCPH-101 (v4.5 05-25-00 A)
5. `9a09ab7e49b422c007e6d54d7c49b965` - SCPH-101 (v4.4 03-24-00 A)

**NTSC-J (Japan):**
1. `57a06303dfa9cf9351222dfcbb4a29d9` - SCPH-5000 (v2.2 12-04-95 J) ⭐
2. `8dd7d5296a650fac7319bce665a6a53c` - SCPH-5500 (v3.0 09-09-96 J) ⭐
3. `8e4c14f567745eff2f0408c8129f72a6` - SCPH-7000, 7500, 9000 (v4.0 08-18-97 J)
4. `476d68a94ccec3b9c8303bbd1daf2810` - SCPH-1000R (v4.5 05-25-00 J)

**PAL (Europe):**
1. `32736f17079d0b2b7024407c39bd3050` - SCPH-5502, 5552 (v3.0 01-06-97 E) ⭐
2. `b9d9a0286c33dc6b7237bb13cd46fdee` - SCPH-7002, 7502, 9002 (v4.1 12-16-97 E) ⭐
3. `de93caec13d1a141a40a79f5c86168d6` - SCPH-102 (v4.5 05-25-00 E)

### BIOS Function Tables

**Table A (0xA0): File & System Functions**
- `0x00` - FileOpen
- `0x01` - FileSeek
- `0x02` - FileRead
- `0x03` - FileWrite
- `0x04` - FileClose
- `0x1C` - index()
- `0x30` - std_out_putchar()
- `0xA8` - Unknown_A

**Table B (0xB0): Device & Event Functions**
- `0x32` - Unknown_B (Device check)
- `0xB0` - Unknown_B

**Table C (0xC0): Advanced System Functions**
- `0x60` - Unknown_C
- `0x70` - Unknown_C
- `0xC0` - Unknown_C

## Implementation Strategy

### Phase 1: Type System (250 lines)
- Define all enums, constants, and structures
- Create BIOS image info database (top 20 BIOSes)
- Implement hash comparison helpers

### Phase 2: Core Implementation (800 lines)
- bios_init() - Zero initialization
- bios_load() - File loading with error handling
- bios_compute_hash() - MD5 calculation
- bios_find_info_by_hash() - O(1) hash lookup
- Memory access functions (load32/16/8)

### Phase 3: BIOS Call System (300 lines)
- Create function name tables (A/B/C)
- Implement bios_decode_function()
- Implement bios_log_function_call()
- Move logging from CPU to BIOS module

### Phase 4: Integration
- Update interconnect.h to use new BiosState
- Update cpu_instructions.c to call bios_log_function_call()
- Remove BIOS logging from CPU module
- Update main.c to use bios_load_and_verify()

## Complexity Analysis

### Memory Operations
- `bios_load32()`: O(1) - Direct array access
- `bios_load16()`: O(1) - Direct array access
- `bios_load8()`: O(1) - Direct array access

### Hash Lookup
- `bios_find_info_by_hash()`: O(n) where n = 20 known BIOSes
  - Could optimize to O(1) with hash table, but n is small

### Function Decode
- `bios_decode_function()`: O(1) - Array lookup by index

## Benefits

1. **Separation of Concerns**: BIOS logic separated from CPU
2. **Verification**: Users know which BIOS they're running
3. **Debugging**: Better BIOS call logging
4. **Extensibility**: Easy to add new BIOS versions
5. **Compatibility**: Can detect region mismatches
6. **Professional**: Matches DuckStation's approach

## Testing Plan

1. **Load Test**: Verify SCPH1001.BIN loads correctly
2. **Hash Test**: Confirm MD5 calculation matches DuckStation
3. **Verification Test**: Known BIOS should verify, unknown should warn
4. **Region Test**: Detect NTSC-U region correctly
5. **Call Logging Test**: BIOS calls appear in bios module, not CPU
6. **Memory Test**: load32/16/8 return correct values

## Migration Path

1. Backup old files:
   - `include/bios.h` → `include/bios.h.backup`
   - `src/bios.c` → `src/bios.c.backup`

2. Create new modular structure
3. Update all references
4. Verify compilation
5. Test with existing BIOS file
6. Confirm emulator still boots to menu

## Success Criteria

✅ BIOS module compiles without errors
✅ MD5 hash computed correctly
✅ SCPH-1001 detected and verified
✅ BIOS calls logged from bios module (not CPU)
✅ Emulator boots to menu as before
✅ Log shows: "[BIOS] Loaded: SCPH-1001, 5003 (v2.2 12-04-95 A) - NTSC-U [VERIFIED]"

## File Size Estimates
- `bios_types.h`: ~350 lines (enums, structs, hash database)
- `bios_core.h`: ~150 lines (API declarations, documentation)
- `bios_core.c`: ~800 lines (implementation, MD5, logging)
- **Total: ~1,300 lines**
