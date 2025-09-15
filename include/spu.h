#ifndef SPU_H
#define SPU_H

#include <stdint.h>
#include <stdbool.h>

// SPU Memory Map (based on PSX-SPX documentation)
#define SPU_VOICE_COUNT     24
#define SPU_RAM_SIZE        (512 * 1024)  // 512KB SPU RAM
#define SPU_SAMPLE_RATE     44100

// SPU Register Offsets (from 0x1F801C00)
#define SPU_MAIN_VOL_L      0x180  // Main Volume Left
#define SPU_MAIN_VOL_R      0x182  // Main Volume Right
#define SPU_REVERB_VOL_L    0x184  // Reverb Volume Left
#define SPU_REVERB_VOL_R    0x186  // Reverb Volume Right
#define SPU_KEY_ON_LOW      0x188  // Voice Key On (Voices 0-15)
#define SPU_KEY_ON_HIGH     0x18A  // Voice Key On (Voices 16-23)
#define SPU_KEY_OFF_LOW     0x18C  // Voice Key Off (Voices 0-15)
#define SPU_KEY_OFF_HIGH    0x18E  // Voice Key Off (Voices 16-23)
#define SPU_NOISE_MODE_LOW  0x190  // Noise Mode (Voices 0-15)
#define SPU_NOISE_MODE_HIGH 0x192  // Noise Mode (Voices 16-23)
#define SPU_REVERB_ON_LOW   0x194  // Reverb On (Voices 0-15)
#define SPU_REVERB_ON_HIGH  0x196  // Reverb On (Voices 16-23)
#define SPU_VOICE_STATUS    0x198  // Voice Status (read-only)
#define SPU_REVERB_WORK_ADDR 0x19A // Reverb Work Area Start Address
#define SPU_IRQ_ADDR        0x19C  // IRQ Address
#define SPU_DATA_TRANSFER_ADDR 0x19E // Data Transfer Address
#define SPU_DATA_TRANSFER_FIFO 0x1A0 // Data Transfer FIFO
#define SPU_CONTROL         0x1AA  // SPU Control Register
#define SPU_DATA_TRANSFER_CTRL 0x1AC // Data Transfer Control
#define SPU_STATUS          0x1AE  // SPU Status Register
#define SPU_CD_VOL_L        0x1B0  // CD Volume Left
#define SPU_CD_VOL_R        0x1B2  // CD Volume Right
#define SPU_EXT_VOL_L       0x1B4  // External Volume Left
#define SPU_EXT_VOL_R       0x1B6  // External Volume Right

// SPU Voice Register Structure (per voice, 16 bytes each)
typedef struct {
    uint16_t vol_left;          // Voice Volume Left
    uint16_t vol_right;         // Voice Volume Right
    uint16_t sample_rate;       // Voice Sample Rate (pitch)
    uint16_t start_addr;        // Voice Start Address
    uint16_t adsr_settings;     // ADSR Settings (Attack/Decay/Sustain/Release)
    uint16_t adsr_volume;       // Current ADSR Volume
    uint16_t repeat_addr;       // Voice Repeat Address
    uint16_t reserved;          // Reserved/unused
} SpuVoice;

// Main SPU State Structure
typedef struct {
    // Voice Channels (24 voices)
    SpuVoice voices[SPU_VOICE_COUNT];
    
    // SPU RAM (512KB for samples and reverb)
    uint8_t ram[SPU_RAM_SIZE];
    
    // Control Registers
    uint16_t main_vol_left;     // Main Volume Left
    uint16_t main_vol_right;    // Main Volume Right
    uint16_t reverb_vol_left;   // Reverb Volume Left
    uint16_t reverb_vol_right;  // Reverb Volume Right
    uint16_t cd_vol_left;       // CD Audio Volume Left
    uint16_t cd_vol_right;      // CD Audio Volume Right
    uint16_t ext_vol_left;      // External Audio Volume Left
    uint16_t ext_vol_right;     // External Audio Volume Right
    
    // Status and Control
    uint32_t key_on;            // Voice Key On flags (24 bits)
    uint32_t key_off;           // Voice Key Off flags (24 bits)
    uint32_t noise_mode;        // Noise Mode flags (24 bits)
    uint32_t reverb_on;         // Reverb On flags (24 bits)
    uint16_t control;           // SPU Control Register
    uint16_t status;            // SPU Status Register
    uint16_t transfer_control;  // Data Transfer Control
    
    // DMA and Transfer
    uint16_t transfer_addr;     // Current data transfer address
    uint16_t irq_addr;          // IRQ trigger address
    uint16_t reverb_work_addr;  // Reverb work area start
    
    // Internal State
    bool enabled;               // SPU enabled flag
    bool irq_enabled;           // IRQ enabled flag
    bool dma_busy;              // DMA transfer in progress
} Spu;

// Function Prototypes
void spu_init(Spu* spu);
void spu_reset(Spu* spu);

// Register Access Functions
uint16_t spu_load16(Spu* spu, uint32_t offset);
uint32_t spu_load32(Spu* spu, uint32_t offset);
void spu_store16(Spu* spu, uint32_t offset, uint16_t value);
void spu_store32(Spu* spu, uint32_t offset, uint32_t value);

// Voice Control Functions
void spu_key_on_voice(Spu* spu, int voice_num);
void spu_key_off_voice(Spu* spu, int voice_num);
void spu_set_voice_volume(Spu* spu, int voice_num, uint16_t left, uint16_t right);
void spu_set_voice_sample_rate(Spu* spu, int voice_num, uint16_t rate);

// DMA Functions (stubs for now)
void spu_dma_write(Spu* spu, uint16_t* data, uint32_t size);
void spu_dma_read(Spu* spu, uint16_t* data, uint32_t size);

// Audio Processing (stubs for now)
void spu_update(Spu* spu);
void spu_generate_samples(Spu* spu, int16_t* output, int sample_count);

#endif // SPU_H