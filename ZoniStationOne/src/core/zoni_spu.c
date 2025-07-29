/**
 * @file zoni_spu.c
 * @brief PlayStation SPU (Sound Processing Unit) emulation implementation
 * 
 * This file implements the SPU emulation for the PlayStation's Sound Processing Unit.
 */

#include "zoni_spu.h"
#include "zoni_common.h"
#include <string.h>

// SPU register addresses
#define SPU_BASE_ADDR 0x1F801C00
#define SPU_STATUS 0x1F801C00
#define SPU_CONTROL 0x1F801C02
#define SPU_VOLUME_LEFT 0x1F801C04
#define SPU_VOLUME_RIGHT 0x1F801C06
#define SPU_REVERB_VOLUME_LEFT 0x1F801C08
#define SPU_REVERB_VOLUME_RIGHT 0x1F801C0A
#define SPU_VOICE_VOLUME_LEFT_BASE 0x1F801C80
#define SPU_VOICE_VOLUME_RIGHT_BASE 0x1F801C82
#define SPU_VOICE_SAMPLE_RATE_BASE 0x1F801C84
#define SPU_VOICE_START_ADDR_BASE 0x1F801C86
#define SPU_VOICE_LOOP_ADDR_BASE 0x1F801C88
#define SPU_VOICE_ADSR_BASE 0x1F801C8A
#define SPU_KEY_ON 0x1F801C8C
#define SPU_KEY_OFF 0x1F801C8E
#define SPU_DMA_ADDR 0x1F801C90
#define SPU_DMA_SIZE 0x1F801C92
#define SPU_DMA_CONTROL 0x1F801C94

zoni_error_t zoni_spu_init(zoni_spu_t* spu, const zoni_spu_config_t* config) {
    if (!spu) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Clear SPU structure
    memset(spu, 0, sizeof(zoni_spu_t));
    
    // Set configuration
    if (config) {
        spu->config = *config;
    } else {
        // Default configuration
        spu->config.sample_rate = PSX_SPU_SAMPLE_RATE;
        spu->config.buffer_size = 1024;
        spu->config.enable_audio = true;
        spu->config.enable_reverb = false;
    }
    
    // Initialize SPU RAM
    memset(spu->ram, 0, PSX_SPU_RAM_SIZE);
    
    // Initialize voices
    for (int i = 0; i < PSX_SPU_VOICES; i++) {
        spu->voices[i].volume_left = 0x3FFF;
        spu->voices[i].volume_right = 0x3FFF;
        spu->voices[i].sample_rate = 0x1000;
        spu->voices[i].start_address = 0;
        spu->voices[i].current_address = 0;
        spu->voices[i].loop_address = 0;
        spu->voices[i].adsr_attack = 0x007F;
        spu->voices[i].adsr_decay = 0x007F;
        spu->voices[i].adsr_sustain = 0x007F;
        spu->voices[i].adsr_release = 0x007F;
        spu->voices[i].active = false;
        spu->voices[i].loop = false;
    }
    
    // Initialize registers
    spu->status = ZONI_SPU_STATUS_READY | ZONI_SPU_STATUS_DMA_READY;
    spu->control = 0;
    spu->volume_left = 0x3FFF;
    spu->volume_right = 0x3FFF;
    spu->reverb_volume_left = 0;
    spu->reverb_volume_right = 0;
    
    // Initialize DMA
    spu->dma_address = 0;
    spu->dma_size = 0;
    spu->dma_control = 0;
    spu->dma_busy = false;
    
    // Initialize key on/off voices
    spu->key_on_voices = 0;
    spu->key_off_voices = 0;
    
    // Initialize audio buffer
    spu->buffer_size = spu->config.buffer_size;
    spu->audio_buffer = malloc(spu->buffer_size * sizeof(s16));
    if (!spu->audio_buffer) {
        return ZONI_ERROR_OUT_OF_MEMORY;
    }
    memset(spu->audio_buffer, 0, spu->buffer_size * sizeof(s16));
    spu->buffer_pos = 0;
    
    spu->initialized = true;
    spu->current_voice = 0;
    
    zoni_log(ZONI_LOG_INFO, "SPU initialized successfully");
    
    return ZONI_SUCCESS;
}

void zoni_spu_shutdown(zoni_spu_t* spu) {
    if (!spu) {
        return;
    }
    
    if (spu->audio_buffer) {
        free(spu->audio_buffer);
        spu->audio_buffer = NULL;
    }
    
    spu->initialized = false;
    
    zoni_log(ZONI_LOG_INFO, "SPU shutdown");
}

void zoni_spu_reset(zoni_spu_t* spu) {
    if (!spu || !spu->initialized) {
        return;
    }
    
    // Reset all voices
    for (int i = 0; i < PSX_SPU_VOICES; i++) {
        spu->voices[i].active = false;
        spu->voices[i].current_address = spu->voices[i].start_address;
    }
    
    // Reset registers
    spu->status = ZONI_SPU_STATUS_READY | ZONI_SPU_STATUS_DMA_READY;
    spu->control = 0;
    spu->dma_busy = false;
    
    // Clear audio buffer
    if (spu->audio_buffer) {
        memset(spu->audio_buffer, 0, spu->buffer_size * sizeof(s16));
    }
    spu->buffer_pos = 0;
    
    zoni_log(ZONI_LOG_DEBUG, "SPU reset");
}

zoni_error_t zoni_spu_write_register(zoni_spu_t* spu, u32 address, u32 value) {
    if (!spu || !spu->initialized) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    u32 offset = address - SPU_BASE_ADDR;
    
            zoni_log(ZONI_LOG_INFO, "SPU write: 0x%08X = 0x%08X", address, value);
    
    switch (offset) {
        case 0x00: // Status register
            // Status is read-only, ignore writes
            break;
            
        case 0x02: // Control register
            spu->control = value & 0xFFFF;
            break;
            
        case 0x04: // Volume Left
            spu->volume_left = value & 0xFFFF;
            break;
            
        case 0x06: // Volume Right
            spu->volume_right = value & 0xFFFF;
            break;
            
        case 0x08: // Reverb Volume Left
            spu->reverb_volume_left = value & 0xFFFF;
            break;
            
        case 0x0A: // Reverb Volume Right
            spu->reverb_volume_right = value & 0xFFFF;
            break;
            
        case 0x8C: // Key On
            spu->key_on_voices = value;
            for (int i = 0; i < PSX_SPU_VOICES; i++) {
                if (value & (1 << i)) {
                    spu->voices[i].active = true;
                    spu->voices[i].current_address = spu->voices[i].start_address;
                }
            }
            break;
            
        case 0x8E: // Key Off
            spu->key_off_voices = value;
            for (int i = 0; i < PSX_SPU_VOICES; i++) {
                if (value & (1 << i)) {
                    spu->voices[i].active = false;
                }
            }
            break;
            
        case 0x90: // DMA Address
            spu->dma_address = value;
            break;
            
        case 0x92: // DMA Size
            spu->dma_size = value;
            break;
            
        case 0x94: // DMA Control
            spu->dma_control = value;
            if (value & 0x0100) { // DMA write
                spu->dma_busy = true;
                spu->status |= ZONI_SPU_STATUS_DMA_BUSY;
                spu->status &= ~ZONI_SPU_STATUS_DMA_READY;
            } else if (value & 0x0200) { // DMA read
                spu->dma_busy = true;
                spu->status |= ZONI_SPU_STATUS_DMA_BUSY;
                spu->status &= ~ZONI_SPU_STATUS_DMA_READY;
            } else {
                spu->dma_busy = false;
                spu->status &= ~ZONI_SPU_STATUS_DMA_BUSY;
                spu->status |= ZONI_SPU_STATUS_DMA_READY;
            }
            break;
            
        default:
            // Handle voice-specific registers
            if (offset >= 0x80 && offset < 0xC0) {
                u8 voice = (offset - 0x80) / 8;
                u8 reg = (offset - 0x80) % 8;
                
                if (voice < PSX_SPU_VOICES) {
                    switch (reg) {
                        case 0: // Volume Left
                            spu->voices[voice].volume_left = value & 0xFFFF;
                            break;
                        case 2: // Volume Right
                            spu->voices[voice].volume_right = value & 0xFFFF;
                            break;
                        case 4: // Sample Rate
                            spu->voices[voice].sample_rate = value & 0xFFFF;
                            break;
                        case 6: // Start Address
                            spu->voices[voice].start_address = value & 0xFFFF;
                            break;
                    }
                }
            }
            break;
    }
    
    return ZONI_SUCCESS;
}

u32 zoni_spu_read_register(zoni_spu_t* spu, u32 address) {
    if (!spu || !spu->initialized) {
        return 0;
    }
    
    u32 offset = address - SPU_BASE_ADDR;
    
    switch (offset) {
        case 0x00: // Status register
            return spu->status;
            
        case 0x02: // Control register
            return spu->control;
            
        case 0x04: // Volume Left
            return spu->volume_left;
            
        case 0x06: // Volume Right
            return spu->volume_right;
            
        case 0x08: // Reverb Volume Left
            return spu->reverb_volume_left;
            
        case 0x0A: // Reverb Volume Right
            return spu->reverb_volume_right;
            
        case 0x8C: // Key On
            return spu->key_on_voices;
            
        case 0x8E: // Key Off
            return spu->key_off_voices;
            
        case 0x90: // DMA Address
            return spu->dma_address;
            
        case 0x92: // DMA Size
            return spu->dma_size;
            
        case 0x94: // DMA Control
            return spu->dma_control;
            
        default:
            // Handle voice-specific registers
            if (offset >= 0x80 && offset < 0xC0) {
                u8 voice = (offset - 0x80) / 8;
                u8 reg = (offset - 0x80) % 8;
                
                if (voice < PSX_SPU_VOICES) {
                    switch (reg) {
                        case 0: // Volume Left
                            return spu->voices[voice].volume_left;
                        case 2: // Volume Right
                            return spu->voices[voice].volume_right;
                        case 4: // Sample Rate
                            return spu->voices[voice].sample_rate;
                        case 6: // Start Address
                            return spu->voices[voice].start_address;
                    }
                }
            }
            break;
    }
    
    return 0;
}

zoni_error_t zoni_spu_dma_write(zoni_spu_t* spu, u32 address, const void* data, u32 size) {
    if (!spu || !spu->initialized || !data) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    if (address + size > PSX_SPU_RAM_SIZE) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    memcpy(&spu->ram[address], data, size);
    
    // Clear DMA busy flag
    spu->dma_busy = false;
    spu->status &= ~ZONI_SPU_STATUS_DMA_BUSY;
    spu->status |= ZONI_SPU_STATUS_DMA_READY;
    
    zoni_log(ZONI_LOG_DEBUG, "SPU DMA write: 0x%08X, %u bytes", address, size);
    
    return ZONI_SUCCESS;
}

zoni_error_t zoni_spu_dma_read(zoni_spu_t* spu, u32 address, void* data, u32 size) {
    if (!spu || !spu->initialized || !data) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    if (address + size > PSX_SPU_RAM_SIZE) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    memcpy(data, &spu->ram[address], size);
    
    // Clear DMA busy flag
    spu->dma_busy = false;
    spu->status &= ~ZONI_SPU_STATUS_DMA_BUSY;
    spu->status |= ZONI_SPU_STATUS_DMA_READY;
    
    zoni_log(ZONI_LOG_DEBUG, "SPU DMA read: 0x%08X, %u bytes", address, size);
    
    return ZONI_SUCCESS;
}

zoni_error_t zoni_spu_generate_audio(zoni_spu_t* spu, s16* buffer, u32 samples) {
    if (!spu || !spu->initialized || !buffer) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // For now, just generate silence
    memset(buffer, 0, samples * sizeof(s16));
    
    return ZONI_SUCCESS;
}

zoni_error_t zoni_spu_update(zoni_spu_t* spu) {
    if (!spu || !spu->initialized) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    // Update SPU state (for now, just a placeholder)
    
    return ZONI_SUCCESS;
}

// Voice control functions (stubs for now)
zoni_error_t zoni_spu_set_voice_volume(zoni_spu_t* spu, u8 voice, u16 left, u16 right) {
    if (!spu || !spu->initialized || voice >= PSX_SPU_VOICES) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    spu->voices[voice].volume_left = left;
    spu->voices[voice].volume_right = right;
    
    return ZONI_SUCCESS;
}

zoni_error_t zoni_spu_set_voice_sample_rate(zoni_spu_t* spu, u8 voice, u16 sample_rate) {
    if (!spu || !spu->initialized || voice >= PSX_SPU_VOICES) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    spu->voices[voice].sample_rate = sample_rate;
    
    return ZONI_SUCCESS;
}

zoni_error_t zoni_spu_set_voice_address(zoni_spu_t* spu, u8 voice, u32 start, u32 loop) {
    if (!spu || !spu->initialized || voice >= PSX_SPU_VOICES) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    spu->voices[voice].start_address = start;
    spu->voices[voice].loop_address = loop;
    
    return ZONI_SUCCESS;
}

zoni_error_t zoni_spu_key_on(zoni_spu_t* spu, u32 voices) {
    if (!spu || !spu->initialized) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    for (int i = 0; i < PSX_SPU_VOICES; i++) {
        if (voices & (1 << i)) {
            spu->voices[i].active = true;
            spu->voices[i].current_address = spu->voices[i].start_address;
        }
    }
    
    return ZONI_SUCCESS;
}

zoni_error_t zoni_spu_key_off(zoni_spu_t* spu, u32 voices) {
    if (!spu || !spu->initialized) {
        return ZONI_ERROR_INVALID_PARAMETER;
    }
    
    for (int i = 0; i < PSX_SPU_VOICES; i++) {
        if (voices & (1 << i)) {
            spu->voices[i].active = false;
        }
    }
    
    return ZONI_SUCCESS;
}

// Debug functions
void zoni_spu_dump_registers(zoni_spu_t* spu) {
    if (!spu || !spu->initialized) {
        return;
    }
    
    zoni_log(ZONI_LOG_INFO, "SPU Registers:");
    zoni_log(ZONI_LOG_INFO, "  Status: 0x%08X", spu->status);
    zoni_log(ZONI_LOG_INFO, "  Control: 0x%08X", spu->control);
    zoni_log(ZONI_LOG_INFO, "  Volume L/R: 0x%04X/0x%04X", spu->volume_left, spu->volume_right);
    zoni_log(ZONI_LOG_INFO, "  Reverb L/R: 0x%04X/0x%04X", spu->reverb_volume_left, spu->reverb_volume_right);
    zoni_log(ZONI_LOG_INFO, "  DMA Addr/Size/Control: 0x%08X/0x%08X/0x%08X", 
             spu->dma_address, spu->dma_size, spu->dma_control);
}

void zoni_spu_dump_voice(zoni_spu_t* spu, u8 voice) {
    if (!spu || !spu->initialized || voice >= PSX_SPU_VOICES) {
        return;
    }
    
    zoni_spu_voice_t* v = &spu->voices[voice];
    zoni_log(ZONI_LOG_INFO, "SPU Voice %d:", voice);
    zoni_log(ZONI_LOG_INFO, "  Volume L/R: 0x%04X/0x%04X", v->volume_left, v->volume_right);
    zoni_log(ZONI_LOG_INFO, "  Sample Rate: 0x%04X", v->sample_rate);
    zoni_log(ZONI_LOG_INFO, "  Start/Loop Addr: 0x%08X/0x%08X", v->start_address, v->loop_address);
    zoni_log(ZONI_LOG_INFO, "  Active: %s", v->active ? "Yes" : "No");
} 