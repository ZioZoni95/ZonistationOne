# Endianness Analysis: MIPS R3000A vs PCSX-ReARMed

## Overview

This document explains the endianness approach used in ZoniStationOne and analyzes why PCSX-ReARMed uses big-endian in certain contexts.

## MIPS R3000A (PlayStation 1) Endianness

### Native Architecture
- **Processor**: MIPS R3000A
- **Native Mode**: **Little-endian**
- **Memory Access**: Most memory operations are little-endian
- **Instruction Fetch**: Little-endian

### Why Little-Endian?
The MIPS R3000A processor in the PlayStation 1 operates in little-endian mode by default. This means:
- The least significant byte is stored at the lowest address
- Multi-byte values are stored with the LSB first
- This is the natural byte order for the MIPS architecture

## PCSX-ReARMed's Big-Endian Approach

### Why PCSX-ReARMed Uses Big-Endian

PCSX-ReARMed uses big-endian for specific reasons, not because the MIPS R3000A is big-endian:

#### 1. Graphics Data
```c
// From pcsx_rearmed_reference/plugins/dfxvideo/gpulib_if.c
#define SWAP16(x) __builtin_bswap16(x)
#define SWAP32(x) __builtin_bswap32(x)

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
// big endian config
#define HOST2LE32(x) SWAP32(x)
#define HOST2BE32(x) (x)
#else
// little endian config
#define HOST2LE32(x) (x)
#define HOST2BE32(x) SWAP32(x)
#endif
```

**Reason**: Texture and framebuffer data are often stored in big-endian format for graphics processing efficiency.

#### 2. Hardware Registers
Some hardware registers expect big-endian values, particularly:
- Graphics processing unit (GPU) registers
- Sound processing unit (SPU) registers
- CD-ROM controller registers

#### 3. Legacy Compatibility
Older emulation code was written assuming big-endian for compatibility with:
- Network protocols (traditionally big-endian)
- Cross-platform data formats
- Legacy hardware interfaces

#### 4. CD Audio Data
```c
// From pcsx_rearmed_reference/libpcsxcore/cdriso.c
cddaBigEndian = TRUE; // cdrdao uses big-endian for CD Audio
```

**Reason**: CD audio data is traditionally stored in big-endian format.

## ZoniStationOne's Correct Approach

### Memory System Implementation

Our emulator correctly implements **little-endian** memory access for the MIPS R3000A:

```c
// Correct little-endian read16
*value = reg->data[offset] | (reg->data[offset + 1] << 8);

// Correct little-endian read32
*value = reg->data[offset] | (reg->data[offset + 1] << 8) |
         (reg->data[offset + 2] << 16) | (reg->data[offset + 3] << 24);

// Correct little-endian write16
reg->data[offset] = value & 0xFF;
reg->data[offset + 1] = (value >> 8) & 0xFF;

// Correct little-endian write32
reg->data[offset] = value & 0xFF;
reg->data[offset + 1] = (value >> 8) & 0xFF;
reg->data[offset + 2] = (value >> 16) & 0xFF;
reg->data[offset + 3] = (value >> 24) & 0xFF;
```

### When to Use Big-Endian

We should use big-endian only for specific hardware components:

1. **GPU Texture Data**: When processing texture data
2. **SPU Audio Data**: When handling audio samples
3. **CD-ROM Audio**: When reading CD audio data
4. **Network Protocols**: If implementing network functionality
5. **Hardware Registers**: Specific registers that expect big-endian values

### Implementation Strategy

```c
// Example: GPU texture processing (big-endian)
void process_texture_data(u8* texture_data, u32 size) {
    for (u32 i = 0; i < size; i += 4) {
        // Convert from little-endian memory to big-endian for GPU
        u32 le_value = *(u32*)(texture_data + i);
        u32 be_value = __builtin_bswap32(le_value);
        // Process big-endian value...
    }
}

// Example: CD audio processing (big-endian)
void process_cd_audio(u8* audio_data, u32 size) {
    for (u32 i = 0; i < size; i += 2) {
        // CD audio is big-endian
        u16 be_sample = (audio_data[i] << 8) | audio_data[i + 1];
        // Process big-endian sample...
    }
}
```

## Key Differences Summary

| Component | MIPS R3000A | PCSX-ReARMed | ZoniStationOne |
|-----------|-------------|--------------|----------------|
| **Main Memory** | Little-endian | Little-endian | Little-endian ✅ |
| **GPU Textures** | Little-endian | Big-endian | Little-endian (convert when needed) |
| **CD Audio** | Little-endian | Big-endian | Little-endian (convert when needed) |
| **Hardware Regs** | Little-endian | Mixed | Little-endian (convert when needed) |

## Best Practices

1. **Default to Little-Endian**: All memory operations should default to little-endian
2. **Convert When Needed**: Only convert to big-endian for specific hardware components
3. **Document Conversions**: Clearly document when endianness conversion occurs
4. **Use Helper Functions**: Create helper functions for endianness conversion
5. **Test Both Formats**: Test with both little-endian and big-endian data

## Conclusion

ZoniStationOne correctly implements little-endian memory access for the MIPS R3000A. PCSX-ReARMed's use of big-endian is for specific hardware components and legacy compatibility, not because the MIPS R3000A is big-endian.

Our approach is more accurate to the actual hardware while still allowing for the flexibility to handle big-endian data when needed for specific components. 