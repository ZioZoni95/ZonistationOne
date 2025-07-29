/**
 * @file zoni_spu.h
 * @brief PlayStation SPU (Sound Processing Unit) emulation for ZoniStationOne
 * 
 * This file defines the SPU structure and emulation interface
 * for the PlayStation's Sound Processing Unit.
 */

#ifndef ZONI_SPU_H
#define ZONI_SPU_H

#include "zoni_common.h"

// PlayStation SPU constants
#define PSX_SPU_RAM_SIZE (512 * 1024)  // 512KB SPU RAM
#define PSX_SPU_VOICES 24              // 24 voices
#define PSX_SPU_SAMPLE_RATE 44100      // 44.1kHz sample rate

// SPU status register bits
typedef enum {
    ZONI_SPU_STATUS_READY = 0x00000001,
    ZONI_SPU_STATUS_DMA_READY = 0x00000002,
    ZONI_SPU_STATUS_DMA_BUSY = 0x00000004,
    ZONI_SPU_STATUS_DMA_WRITE = 0x00000008,
    ZONI_SPU_STATUS_DMA_READ = 0x00000010
} zoni_spu_status_t;

// SPU configuration
typedef struct {
    u32 sample_rate;
    u32 buffer_size;
    bool enable_audio;
    bool enable_reverb;
} zoni_spu_config_t;

// SPU voice structure
typedef struct {
    u32 volume_left;
    u32 volume_right;
    u32 sample_rate;
    u32 start_address;
    u32 current_address;
    u32 loop_address;
    u32 adsr_attack;
    u32 adsr_decay;
    u32 adsr_sustain;
    u32 adsr_release;
    bool active;
    bool loop;
} zoni_spu_voice_t;

// SPU state
typedef struct zoni_spu_s {
    // SPU RAM
    u8 ram[PSX_SPU_RAM_SIZE];
    
    // Voices
    zoni_spu_voice_t voices[PSX_SPU_VOICES];
    
    // SPU registers
    u32 status;
    u32 control;
    u32 volume_left;
    u32 volume_right;
    u32 reverb_volume_left;
    u32 reverb_volume_right;
    
    // DMA registers
    u32 dma_address;
    u32 dma_size;
    u32 dma_control;
    
    // Key on/off voices
    u32 key_on_voices;
    u32 key_off_voices;
    
    // Configuration
    zoni_spu_config_t config;
    
    // State
    bool initialized;
    bool dma_busy;
    u32 current_voice;
    
    // Audio buffer
    s16* audio_buffer;
    u32 buffer_size;
    u32 buffer_pos;
    
} zoni_spu_t;

// SPU functions
zoni_error_t zoni_spu_init(zoni_spu_t* spu, const zoni_spu_config_t* config);
void zoni_spu_shutdown(zoni_spu_t* spu);
void zoni_spu_reset(zoni_spu_t* spu);

// SPU control
zoni_error_t zoni_spu_write_register(zoni_spu_t* spu, u32 address, u32 value);
u32 zoni_spu_read_register(zoni_spu_t* spu, u32 address);

// SPU DMA
zoni_error_t zoni_spu_dma_write(zoni_spu_t* spu, u32 address, const void* data, u32 size);
zoni_error_t zoni_spu_dma_read(zoni_spu_t* spu, u32 address, void* data, u32 size);

// SPU audio
zoni_error_t zoni_spu_generate_audio(zoni_spu_t* spu, s16* buffer, u32 samples);
zoni_error_t zoni_spu_update(zoni_spu_t* spu);

// SPU voice control
zoni_error_t zoni_spu_set_voice_volume(zoni_spu_t* spu, u8 voice, u16 left, u16 right);
zoni_error_t zoni_spu_set_voice_sample_rate(zoni_spu_t* spu, u8 voice, u16 sample_rate);
zoni_error_t zoni_spu_set_voice_address(zoni_spu_t* spu, u8 voice, u32 start, u32 loop);
zoni_error_t zoni_spu_key_on(zoni_spu_t* spu, u32 voices);
zoni_error_t zoni_spu_key_off(zoni_spu_t* spu, u32 voices);

// Debug functions
void zoni_spu_dump_registers(zoni_spu_t* spu);
void zoni_spu_dump_voice(zoni_spu_t* spu, u8 voice);

#endif // ZONI_SPU_H 