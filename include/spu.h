#ifndef SPU_H
#define SPU_H

#include <stdint.h>
#include <stdbool.h>

struct Interconnect;

/* --- SPU Constants --- */
#define SPU_RAM_SIZE            524288      /* 512KB SPU RAM */
#define NUM_VOICES              24
#define NUM_ADPCM_SAMPLES       28          /* samples per ADPCM block */
#define NUM_ADPCM_TRAILING      3           /* trailing samples for interpolation */
#define ADPCM_BLOCK_SIZE        16          /* bytes per ADPCM block */
#define SAMPLE_RATE             44100       /* output sample rate */
#define CPU_TICKS_PER_SPU_TICK  768         /* 0x300 at 33.8688MHz */
#define TRANSFER_TICKS_PER_HALFWORD 16
#define CAPTURE_BUFFER_SIZE     0x400       /* halfwords per channel */
#define NUM_CAPTURE_CHANNELS    4
#define NUM_REVERB_REGS         32
#define GAUSS_TABLE_SIZE        512
#define SPU_SAMPLE_BUFFER_SIZE  4096        /* circular buffer for SDL */

/* --- SPU MMIO Base --- */
#define SPU_START 0x1F801C00
#define SPU_SIZE  640
#define SPU_END   (SPU_START + SPU_SIZE - 1)

/* --- Voice register offsets (per voice, stride = 0x10) --- */
#define VOICE_REG_VOLUME_L      0x00
#define VOICE_REG_VOLUME_R      0x02
#define VOICE_REG_PITCH         0x04
#define VOICE_REG_START_ADDR    0x06
#define VOICE_REG_ADSR_LOW      0x08
#define VOICE_REG_ADSR_HIGH     0x0A
#define VOICE_REG_ADSR_VOL      0x0C
#define VOICE_REG_REPEAT_ADDR   0x0E

/* --- Control register offsets --- */
#define SPU_REG_MVOL_L          0x180  /* 0x1F801D80 */
#define SPU_REG_MVOL_R          0x182  /* 0x1F801D82 */
#define SPU_REG_RVOL_L          0x184  /* 0x1F801D84 */
#define SPU_REG_RVOL_R          0x186  /* 0x1F801D86 */
#define SPU_REG_KEY_ON_L        0x188  /* 0x1F801D88 */
#define SPU_REG_KEY_ON_H        0x18A  /* 0x1F801D8A */
#define SPU_REG_KEY_OFF_L       0x18C  /* 0x1F801D8C */
#define SPU_REG_KEY_OFF_H       0x18E  /* 0x1F801D8E */
#define SPU_REG_FMOD_L          0x190  /* 0x1F801D90 */
#define SPU_REG_FMOD_H          0x192  /* 0x1F801D92 */
#define SPU_REG_NOISE_L         0x194  /* 0x1F801D94 */
#define SPU_REG_NOISE_H         0x196  /* 0x1F801D96 */
#define SPU_REG_REVERB_L        0x198  /* 0x1F801D98 */
#define SPU_REG_REVERB_H        0x19A  /* 0x1F801D9A */
#define SPU_REG_ENDX_L          0x19C  /* 0x1F801D9C */
#define SPU_REG_ENDX_H          0x19E  /* 0x1F801D9E */
#define SPU_REG_REVERB_BASE     0x1A2  /* 0x1F801DA2 */
#define SPU_REG_IRQ_ADDR        0x1A4  /* 0x1F801DA4 */
#define SPU_REG_TRANSFER_ADDR   0x1A6  /* 0x1F801DA6 */
#define SPU_REG_TRANSFER_DATA   0x1A8  /* 0x1F801DA8 */
#define SPU_REG_CONTROL         0x1AA  /* 0x1F801DAA */
#define SPU_REG_STATUS          0x1AE  /* 0x1F801DAE */
#define SPU_REG_CD_VOL_L        0x1B0  /* 0x1F801DB0 */
#define SPU_REG_CD_VOL_R        0x1B2  /* 0x1F801DB2 */
#define SPU_REG_EXT_VOL_L       0x1B4  /* 0x1F801DB4 */
#define SPU_REG_EXT_VOL_R       0x1B6  /* 0x1F801DB6 */
#define SPU_REG_MVOL_CUR_L      0x1B8  /* 0x1F801DB8 */
#define SPU_REG_MVOL_CUR_R      0x1BA  /* 0x1F801DBA */

/* --- Reverb register offsets (relative to 0x1DC0) --- */
#define REVERB_REG_FB_SRC_A     0x00
#define REVERB_REG_FB_SRC_B     0x02
#define REVERB_REG_IIR_ALPHA    0x04
#define REVERB_REG_ACC_COEF_A   0x06
#define REVERB_REG_ACC_COEF_B   0x08
#define REVERB_REG_ACC_COEF_C   0x0A
#define REVERB_REG_ACC_COEF_D   0x0C
#define REVERB_REG_IIR_COEF     0x0E
#define REVERB_REG_FB_ALPHA     0x10
#define REVERB_REG_FB_X         0x12
#define REVERB_REG_IIR_DEST_A0  0x14
#define REVERB_REG_IIR_DEST_A1  0x16
#define REVERB_REG_ACC_SRC_A0   0x18
#define REVERB_REG_ACC_SRC_A1   0x1A
#define REVERB_REG_ACC_SRC_B0   0x1C
#define REVERB_REG_ACC_SRC_B1   0x1E
#define REVERB_REG_IIR_SRC_A0   0x20
#define REVERB_REG_IIR_SRC_A1   0x22
#define REVERB_REG_IIR_DEST_B0  0x24
#define REVERB_REG_IIR_DEST_B1  0x26
#define REVERB_REG_ACC_SRC_C0   0x28
#define REVERB_REG_ACC_SRC_C1   0x2A
#define REVERB_REG_ACC_SRC_D0   0x2C
#define REVERB_REG_ACC_SRC_D1   0x2E
#define REVERB_REG_IIR_SRC_B1   0x30  /* note: swapped order vs docs */
#define REVERB_REG_IIR_SRC_B0   0x32
#define REVERB_REG_MIX_DEST_A0  0x34
#define REVERB_REG_MIX_DEST_A1  0x36
#define REVERB_REG_MIX_DEST_B0  0x38
#define REVERB_REG_MIX_DEST_B1  0x3A
#define REVERB_REG_IN_COEF_L    0x3C
#define REVERB_REG_IN_COEF_R    0x3E

/* --- SPUCNT bit fields (0x1F801DAA) --- */
#define SPU_CTRL_CD_AUDIO_EN    (1 << 0)
#define SPU_CTRL_EXT_AUDIO_EN   (1 << 1)
#define SPU_CTRL_CD_REVERB      (1 << 2)
#define SPU_CTRL_EXT_REVERB     (1 << 3)
#define SPU_CTRL_TRANSFER_MODE  (3 << 4)
#define SPU_CTRL_IRQ9_ENABLE    (1 << 6)
#define SPU_CTRL_REVERB_ENABLE  (1 << 7)
#define SPU_CTRL_NOISE_CLOCK    (0x3F << 8)
#define SPU_CTRL_MUTE           (1 << 14)
#define SPU_CTRL_ENABLE         (1 << 15)

/* --- SPUSTAT bit fields (0x1F801DAE) --- */
#define SPU_STATUS_MODE         (0x3F << 0)
#define SPU_STATUS_IRQ9_FLAG    (1 << 6)
#define SPU_STATUS_DMA_REQUEST  (1 << 7)
#define SPU_STATUS_DMA_READ_REQ (1 << 8)
#define SPU_STATUS_DMA_WRITE_REQ (1 << 9)
#define SPU_STATUS_TRANSFER_BUSY (1 << 10)
#define SPU_STATUS_CB_HALF      (1 << 11)

/* --- Transfer modes --- */
typedef enum {
    TRANSFER_STOPPED = 0,
    TRANSFER_MANUAL_WRITE = 1,
    TRANSFER_DMA_WRITE = 2,
    TRANSFER_DMA_READ = 3
} SpuTransferMode;

/* --- ADSR Phases --- */
typedef enum {
    ADSR_PHASE_OFF = 0,
    ADSR_PHASE_ATTACK,
    ADSR_PHASE_DECAY,
    ADSR_PHASE_SUSTAIN,
    ADSR_PHASE_RELEASE
} SpuAdsrPhase;

/* --- Voice state --- */
typedef struct SpuVoice {
    uint16_t current_address;        /* current ADPCM block pointer (halfword units) */
    uint16_t volume_left;            /* register 0x00 */
    uint16_t volume_right;           /* register 0x02 */
    uint16_t pitch;                  /* register 0x04 (VxPitch) */
    uint16_t start_address;          /* register 0x06 (×8 for byte addr) */
    uint16_t adsr_low;               /* register 0x08 */
    uint16_t adsr_high;              /* register 0x0A */
    int16_t  adsr_volume;            /* register 0x0C (current envelope) */
    uint16_t repeat_address;         /* register 0x0E (loop point, ×8) */

    /* Runtime state */
    uint8_t  counter_index;          /* 8-bit interpolation index */
    uint8_t  counter_sample;         /* 5-bit sample index within block (0-27) */
    bool     is_first_block;
    bool     has_samples;
    bool     ignore_loop_address;

    int16_t  block_samples[28 + NUM_ADPCM_TRAILING]; /* 3 trailing + 28 decoded */
    int16_t  adpcm_last_samples[2];  /* last 2 decoded samples */
    int32_t  last_volume;            /* last output for pitch modulation */

    /* ADSR runtime */
    SpuAdsrPhase adsr_phase;
    int16_t  adsr_target;
    uint32_t adsr_counter;

    /* Volume sweep */
    int32_t  left_sweep_level;
    int32_t  right_sweep_level;
    uint32_t left_sweep_counter;
    uint32_t right_sweep_counter;
    bool     left_sweep_active;
    bool     right_sweep_active;
    bool     endx_mask;            /* set when block has loop_end flag */
} SpuVoice;

/* --- SPU global state --- */
typedef struct Spu {
    /* 512KB SPU RAM */
    uint16_t ram[SPU_RAM_SIZE / 2];

    /* Voices */
    SpuVoice voices[NUM_VOICES];

    /* Global registers (shadow) */
    uint16_t main_vol_left;
    uint16_t main_vol_right;
    int16_t  reverb_vol_left;
    int16_t  reverb_vol_right;
    int16_t  cd_vol_left;
    int16_t  cd_vol_right;
    int16_t  ext_vol_left;
    int16_t  ext_vol_right;

    /* Control / Status */
    uint16_t control;    /* SPUCNT */
    uint16_t status;     /* SPUSTAT */

    /* Bitmasks */
    uint32_t key_on;
    uint32_t key_off;
    uint32_t endx;
    uint32_t pitch_mod;
    uint32_t noise_mode;
    uint32_t reverb_on;

    /* Transfer */
    uint16_t transfer_addr_reg;
    uint32_t transfer_addr;         /* byte address */

    /* IRQ */
    uint16_t irq_addr;              /* halfword offset (×8 for byte) */

    /* Reverb */
    uint16_t reverb_base;           /* word-aligned (×4) */
    uint16_t reverb_regs[NUM_REVERB_REGS];
    uint32_t reverb_current_addr;

    /* Noise generator */
    uint32_t noise_clock;
    uint32_t noise_count;
    uint32_t noise_level;

    /* Capture buffer */
    int16_t  capture_buffer[NUM_CAPTURE_CHANNELS][CAPTURE_BUFFER_SIZE];
    uint32_t capture_pos;

    /* Reverb resample buffers */
    int16_t  reverb_ds_buf[2][128];  /* downsample input */
    int16_t  reverb_us_buf[2][64];   /* upsample output */
    int32_t  reverb_resample_pos;
    int16_t  last_reverb_input[2];
    int32_t  last_reverb_output[2];

    /* Audio output (main sweep) */
    int32_t  main_vol_left_cur;
    int32_t  main_vol_right_cur;
    bool     muted;

    /* CD audio frame mixing */
    int16_t  cd_audio_left;
    int16_t  cd_audio_right;

    /* IRQ state */
    bool     irq9_flag;

    /* Sample generation state */
    int16_t  sample_buffer[SPU_SAMPLE_BUFFER_SIZE * 2]; /* interleaved L,R */
    int      sample_buf_head;   /* read position (SDL callback) */
    int      sample_buf_tail;   /* write position (emulation) */
    int      sample_buf_count;  /* number of stereo frames in buffer */
    uint64_t spu_tick_counter;  /* accumulated CPU cycles for SPU timing */

    /* Debug/logging */
    uint32_t total_samples_generated;
    uint32_t total_key_on_events;
    int32_t  peak_level_left;   /* peak level for audio meter */
    int32_t  peak_level_right;
} Spu;

/* --- Public API --- */
void spu_init(Spu* spu);
void spu_reset(Spu* spu);

/* MMIO */
uint16_t spu_read16(struct Interconnect* inter, uint32_t addr);
uint32_t spu_read32(struct Interconnect* inter, uint32_t addr);
void     spu_write16(struct Interconnect* inter, uint32_t addr, uint16_t val);
void     spu_write32(struct Interconnect* inter, uint32_t addr, uint32_t val);
void     spu_write8(struct Interconnect* inter, uint32_t addr, uint8_t val);

/* Audio generation */
void     spu_generate_samples(Spu* spu, int16_t* buffer, int num_samples);

/* DMA */
void     spu_dma_write_halfwords(Spu* spu, struct Interconnect* inter, const uint16_t* data, int count);
void     spu_dma_read_halfwords(Spu* spu, struct Interconnect* inter, uint16_t* data, int count);
bool     spu_dma_write_request(Spu* spu);
bool     spu_dma_read_request(Spu* spu);

/* IRQ */
bool     spu_check_irq(Spu* spu, struct Interconnect* inter, uint32_t address);
void     spu_update_irq_addr(Spu* spu, struct Interconnect* inter);

/* Key on/off processing (called once per sample batch) */
void     spu_process_key_on_off(Spu* spu);

/* SPU RAM transfer (manual mode) */
void     spu_transfer_write(Spu* spu, struct Interconnect* inter, uint16_t value);
uint16_t spu_transfer_read(Spu* spu, struct Interconnect* inter);

/* SPU control register handling */
void     spu_set_control(Spu* spu, uint16_t value);

/* Voice sample generation (called from spu_mixing.c) */
void     spu_voice_generate_sample(Spu* spu, struct Interconnect* inter,
                                   int voice_idx, int16_t* left_out, int16_t* right_out);

/* SPU sample buffer management (driven by EVQ_SPU event scheduler) */
void     spu_step(struct Interconnect* inter, uint32_t cpu_cycles);
int      spu_get_samples(Spu* spu, int16_t* buffer, int max_samples);

/* SDL audio callback helper (fills output buffer from SPU sample buffer) */
void     spu_fill_audio(Spu* spu, int16_t* stream, int num_stereo_samples);

#endif /* SPU_H */
