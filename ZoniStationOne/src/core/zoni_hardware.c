#include "zoni_hardware.h"
#include "zoni_endian.h"

zoni_error_t zoni_hardware_init(zoni_hardware_t* hw) {
    if (!hw) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Initialize hardware to zero
    memset(hw->scratchpad, 0, PSX_HW_SCRATCH_SIZE);
    memset(hw->hw_regs, 0, PSX_HW_SIZE);
    
    // Set some default values for common registers
    // These are based on PCSX-ReARMed's psxHwReset()
    
    // DMA PCR - default value (offset 0x10F0 - 0x1000 = 0xF0)
    zoni_write_le32(&hw->hw_regs[0xF0], 0x00009099);
    
    // DMA ICR - default value (offset 0x10F4 - 0x1000 = 0xF4)
    zoni_write_le32(&hw->hw_regs[0xF4], 0x8c8c0000);
    
    // Interrupt mask - default value (offset 0x1074 - 0x1000 = 0x74)
    zoni_write_le16(&hw->hw_regs[0x74], 0x000C);
    
    // Interrupt status - default value (offset 0x1070 - 0x1000 = 0x70)
    zoni_write_le16(&hw->hw_regs[0x70], 0x0001);
    
    return ZONI_SUCCESS;
}

void zoni_hardware_shutdown(zoni_hardware_t* hw) {
    // Nothing to do for basic implementation
    ZONI_UNUSED(hw);
}

void zoni_hardware_reset(zoni_hardware_t* hw) {
    if (hw) {
        zoni_hardware_init(hw);
    }
}

// Helper function to check if address is in hardware range
static bool is_hw_address(u32 address) {
    return (address >= PSX_HW_BASE && address < PSX_HW_BASE + PSX_HW_SIZE) ||
           (address >= PSX_HW_SCRATCHPAD && address < PSX_HW_SCRATCHPAD + PSX_HW_SCRATCH_SIZE);
}

// Helper function to get register offset
static u32 get_hw_offset(u32 address) {
    if (address >= PSX_HW_SCRATCHPAD && address < PSX_HW_SCRATCHPAD + PSX_HW_SCRATCH_SIZE) {
        return address - PSX_HW_SCRATCHPAD;
    }
    // For hardware registers, calculate offset from base
    // 0x1F801000 -> 0x0000, 0x1F801001 -> 0x0001, etc.
    return address & 0xFFFF;  // Take only the lower 16 bits as offset
}

u8 zoni_hw_read8(zoni_hardware_t* hw, u32 address) {
    if (!hw || !is_hw_address(address)) {
        return 0xFF;
    }
    
    u32 offset = get_hw_offset(address);
    
    // Handle specific registers
    switch (address & 0xFFFF) {
        case 0x1040: // SIO Data
            return 0xFF; // Default value for SIO data
            
        case 0x1044: // SIO Status
            return 0x26; // Default SIO status
            
        case 0x1800: // CDROM Data
            return 0x00; // Default CDROM data
            
        case 0x1801: // CDROM Status
            return 0x18; // Default CDROM status
            
        default:
            if (offset < PSX_HW_SIZE) {
                return hw->hw_regs[offset];
            }
            break;
    }
    
    return 0xFF;
}

u16 zoni_hw_read16(zoni_hardware_t* hw, u32 address) {
    if (!hw || !is_hw_address(address)) {
        return 0xFFFF;
    }
    
    u32 offset = get_hw_offset(address);
    
    // Handle specific registers
    switch (address & 0xFFFF) {
        case 0x1044: // SIO Status
            return 0x26; // Default SIO status
            
        case 0x1070: // Interrupt Status
            return zoni_read_le16(&hw->hw_regs[offset]);
            
        case 0x1074: // Interrupt Mask
            return zoni_read_le16(&hw->hw_regs[offset]);
            
        case 0x1100: // Timer 0 Count
            return 0x0000; // Default timer count
            
        case 0x1104: // Timer 0 Mode
            return 0x0000; // Default timer mode
            
        case 0x1110: // Timer 1 Count
            return 0x0000; // Default timer count
            
        case 0x1114: // Timer 1 Mode
            return 0x0000; // Default timer mode
            
        case 0x1120: // Timer 2 Count
            return 0x0000; // Default timer count
            
        case 0x1124: // Timer 2 Mode
            return 0x0000; // Default timer mode
            
        default:
            if (offset < PSX_HW_SIZE - 1) {
                return zoni_read_le16(&hw->hw_regs[offset]);
            }
            break;
    }
    
    return 0xFFFF;
}

u32 zoni_hw_read32(zoni_hardware_t* hw, u32 address) {
    if (!hw || !is_hw_address(address)) {
        return 0xFFFFFFFF;
    }
    
    u32 offset = get_hw_offset(address);
    
    // Handle specific registers
    switch (address & 0xFFFF) {
        case 0x1040: // SIO Data (32-bit read)
            return 0xFF; // Default SIO data
            
        case 0x1070: // Interrupt Status
            return zoni_read_le32(&hw->hw_regs[offset]);
            
        case 0x1074: // Interrupt Mask
            return zoni_read_le32(&hw->hw_regs[offset]);
            
        case 0x10F0: // DMA PCR
            return zoni_read_le32(&hw->hw_regs[offset]);
            
        case 0x10F4: // DMA ICR
            return zoni_read_le32(&hw->hw_regs[offset]);
            
        case 0x1100: // Timer 0 Count
            return 0x00000000; // Default timer count
            
        case 0x1104: // Timer 0 Mode
            return 0x00000000; // Default timer mode
            
        case 0x1108: // Timer 0 Target
            return 0x00000000; // Default timer target
            
        case 0x1110: // Timer 1 Count
            return 0x00000000; // Default timer count
            
        case 0x1114: // Timer 1 Mode
            return 0x00000000; // Default timer mode
            
        case 0x1118: // Timer 1 Target
            return 0x00000000; // Default timer target
            
        case 0x1120: // Timer 2 Count
            return 0x00000000; // Default timer count
            
        case 0x1124: // Timer 2 Mode
            return 0x00000000; // Default timer mode
            
        case 0x1128: // Timer 2 Target
            return 0x00000000; // Default timer target
            
        case 0x1810: // GPU Data
            return 0x00000000; // Default GPU data
            
        case 0x1814: // GPU Status
            return 0x14802000; // Default GPU status (based on PCSX-ReARMed)
            
        default:
            if (offset < PSX_HW_SIZE - 3) {
                return zoni_read_le32(&hw->hw_regs[offset]);
            }
            break;
    }
    
    return 0xFFFFFFFF;
}

zoni_error_t zoni_hw_write8(zoni_hardware_t* hw, u32 address, u8 value) {
    if (!hw || !is_hw_address(address)) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    u32 offset = get_hw_offset(address);
    
    // Handle specific registers
    switch (address & 0xFFFF) {
        case 0x1040: // SIO Data
            // Accept the write but don't store it
            return ZONI_SUCCESS;
            
        case 0x1800: // CDROM Data
        case 0x1801: // CDROM Status
        case 0x1802: // CDROM Mode
        case 0x1803: // CDROM Control
            // Accept CDROM writes
            return ZONI_SUCCESS;
            
        default:
            if (offset < PSX_HW_SIZE) {
                hw->hw_regs[offset] = value;
                return ZONI_SUCCESS;
            }
            break;
    }
    
    return ZONI_ERROR_INVALID_PARAMETER;
}

zoni_error_t zoni_hw_write16(zoni_hardware_t* hw, u32 address, u16 value) {
    if (!hw || !is_hw_address(address)) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    u32 offset = get_hw_offset(address);
    
    // Handle specific registers
    switch (address & 0xFFFF) {
        case 0x1044: // SIO Status
        case 0x1048: // SIO Mode
        case 0x104A: // SIO Control
        case 0x104E: // SIO Baud
            // Accept SIO writes
            return ZONI_SUCCESS;
            
        case 0x1070: // Interrupt Status
            if (offset < PSX_HW_SIZE - 1) {
                zoni_write_le16(&hw->hw_regs[offset], value);
                return ZONI_SUCCESS;
            }
            break;
            
        case 0x1074: // Interrupt Mask
            if (offset < PSX_HW_SIZE - 1) {
                zoni_write_le16(&hw->hw_regs[offset], value);
                return ZONI_SUCCESS;
            }
            break;
            
        case 0x1100: // Timer 0 Count
        case 0x1104: // Timer 0 Mode
        case 0x1108: // Timer 0 Target
        case 0x1110: // Timer 1 Count
        case 0x1114: // Timer 1 Mode
        case 0x1118: // Timer 1 Target
        case 0x1120: // Timer 2 Count
        case 0x1124: // Timer 2 Mode
        case 0x1128: // Timer 2 Target
            // Accept timer writes
            return ZONI_SUCCESS;
            
        default:
            if (offset < PSX_HW_SIZE - 1) {
                zoni_write_le16(&hw->hw_regs[offset], value);
                return ZONI_SUCCESS;
            }
            break;
    }
    
    return ZONI_ERROR_INVALID_PARAMETER;
}

zoni_error_t zoni_hw_write32(zoni_hardware_t* hw, u32 address, u32 value) {
    if (!hw || !is_hw_address(address)) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    u32 offset = get_hw_offset(address);
    
    // Handle specific registers
    switch (address & 0xFFFF) {
        case 0x1040: // SIO Data (32-bit write)
            // Accept SIO writes
            return ZONI_SUCCESS;
            
        case 0x1070: // Interrupt Status
            if (offset < PSX_HW_SIZE - 3) {
                zoni_write_le32(&hw->hw_regs[offset], value);
                return ZONI_SUCCESS;
            }
            break;
            
        case 0x1074: // Interrupt Mask
            if (offset < PSX_HW_SIZE - 3) {
                zoni_write_le32(&hw->hw_regs[offset], value);
                return ZONI_SUCCESS;
            }
            break;
            
        case 0x10F0: // DMA PCR
            if (offset < PSX_HW_SIZE - 3) {
                zoni_write_le32(&hw->hw_regs[offset], value);
                return ZONI_SUCCESS;
            }
            break;
            
        case 0x10F4: // DMA ICR
            if (offset < PSX_HW_SIZE - 3) {
                zoni_write_le32(&hw->hw_regs[offset], value);
                return ZONI_SUCCESS;
            }
            break;
            
        case 0x1100: // Timer 0 Count
        case 0x1104: // Timer 0 Mode
        case 0x1108: // Timer 0 Target
        case 0x1110: // Timer 1 Count
        case 0x1114: // Timer 1 Mode
        case 0x1118: // Timer 1 Target
        case 0x1120: // Timer 2 Count
        case 0x1124: // Timer 2 Mode
        case 0x1128: // Timer 2 Target
            // Accept timer writes
            return ZONI_SUCCESS;
            
        case 0x1810: // GPU Data
            // Accept GPU data writes
            return ZONI_SUCCESS;
            
        case 0x1814: // GPU Status
            // Accept GPU status writes
            return ZONI_SUCCESS;
            
        default:
            if (offset < PSX_HW_SIZE - 3) {
                zoni_write_le32(&hw->hw_regs[offset], value);
                return ZONI_SUCCESS;
            }
            break;
    }
    
    return ZONI_ERROR_INVALID_PARAMETER;
} 