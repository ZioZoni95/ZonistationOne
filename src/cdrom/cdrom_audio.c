#include "cdrom_audio.h"
#include "spu.h"
#include "log.h"
#include <string.h>
#include <SDL2/SDL.h>

/* =========================================================================
 * AudioFifo
 * ========================================================================= */

void cdrom_audio_init(AudioFifo *fifo, XaAdpcmState *xa) {
    memset(fifo, 0, sizeof(*fifo));
    memset(xa,   0, sizeof(*xa));
    xa->sixstep   = 6;
    xa->sixstep18 = 6;
}

void cdrom_audio_fifo_push(AudioFifo *fifo, int16_t left, int16_t right) {
    if (fifo->count >= AUDIO_FIFO_CAPACITY) { fifo->total_dropped++; return; }
    /* Keep the queue's standing latency bounded: past the limit, drop the
     * oldest frame rather than the newest, so the delay stops growing while the
     * stream stays continuous. */
    if (fifo->count >= AUDIO_FIFO_MAX_LATENCY) {
        fifo->head = (fifo->head + 1) % AUDIO_FIFO_CAPACITY;
        fifo->count--;
        fifo->total_dropped++;
    }
    fifo->total_pushed++;
    uint32_t packed = (uint32_t)(uint16_t)left | ((uint32_t)(uint16_t)right << 16);
    fifo->data[fifo->tail] = packed;
    fifo->tail = (fifo->tail + 1) % AUDIO_FIFO_CAPACITY;
    fifo->count++;
}

bool cdrom_audio_fifo_pop(AudioFifo *fifo, int16_t *left, int16_t *right) {
    if (fifo->count == 0) return false;
    fifo->total_popped++;
    uint32_t packed = fifo->data[fifo->head];
    fifo->head = (fifo->head + 1) % AUDIO_FIFO_CAPACITY;
    fifo->count--;
    *left  = (int16_t)(packed & 0xFFFF);
    *right = (int16_t)(packed >> 16);
    return true;
}

bool cdrom_audio_fifo_empty(const AudioFifo *fifo) {
    return fifo->count == 0;
}

void cdrom_audio_get_frame(AudioFifo *fifo, int16_t *left, int16_t *right) {
    if (!cdrom_audio_fifo_pop(fifo, left, right)) {
        *left = *right = 0;
    }
}

/* =========================================================================
 * XA-ADPCM Decoder (unchanged)
 * ========================================================================= */

static const int8_t s_filter_pos[5] = {0,  60, 115,  98, 122};
static const int8_t s_filter_neg[5] = {0,   0, -52, -55, -60};

static inline int16_t clamp16_xa(int32_t v) {
    if (v >  32767) return  32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static void decode_xa_chunk(const uint8_t *chunk, bool stereo, bool bits8,
                               int32_t xa_prev[2][2], int16_t *out_samples) {
    const int num_blocks    = bits8 ? 4 : 8;
    const int words_per_blk = 28;
    const uint8_t *headers  = chunk + 4;
    const uint8_t *words    = chunk + 16;

    for (int block = 0; block < num_blocks; block++) {
        uint8_t hdr    = headers[block];
        int     shift  = hdr & 0x0F;
        int     filter = (hdr >> 4) & 0x0F;
        if (filter > 4) filter = 4;
        if (shift  > 12) shift = 9;

        int32_t fpos = s_filter_pos[filter];
        int32_t fneg = s_filter_neg[filter];
        int     ch   = stereo ? (block & 1) : 0;

        int16_t *out;
        int      step;
        if (stereo) {
            out  = &out_samples[(block / 2) * (words_per_blk * 2) + (block & 1)];
            step = 2;
        } else {
            out  = &out_samples[block * words_per_blk];
            step = 1;
        }

        for (int w = 0; w < words_per_blk; w++) {
            uint32_t word;
            memcpy(&word, words + w * 4, 4);

            int32_t nibble;
            if (bits8) {
                uint8_t byte = (word >> (block * 8)) & 0xFF;
                nibble = (int32_t)((int8_t)(byte));
                nibble <<= 8;
            } else {
                uint8_t n4 = (word >> (block * 4)) & 0x0F;
                nibble = (int32_t)((int16_t)(n4 << 12));
            }
            nibble >>= shift;

            int32_t s = nibble
                        + ((xa_prev[ch][0] * fpos + xa_prev[ch][1] * fneg + 32) >> 6);
            xa_prev[ch][1] = xa_prev[ch][0];
            xa_prev[ch][0] = s;

            out[w * step] = clamp16_xa(s);
        }
    }
}

static const int16_t s_zigzag[7][29] = {
    {0, 0x0, 0x0, 0x0, 0x0, -0x0002, 0x000A, -0x0022, 0x0041, -0x0054,
     0x0034, 0x0009, -0x010A, 0x0400, -0x0A78, 0x234C, 0x6794, -0x1780, 0x0BCD, -0x0623,
     0x0350, -0x016D, 0x006B, 0x000A, -0x0010, 0x0011, -0x0008, 0x0003, -0x0001},
    {0, 0x0, 0x0, -0x0002, 0x0, 0x0003, -0x0013, 0x003C, -0x004B, 0x00A2,
     -0x00E3, 0x0132, -0x0043, -0x0267, 0x0C9D, 0x74BB, -0x11B4, 0x09B8, -0x05BF, 0x0372,
     -0x01A8, 0x00A6, -0x001B, 0x0005, 0x0006, -0x0008, 0x0003, -0x0001, 0x0},
    {0, 0x0, -0x0001, 0x0003, -0x0002, -0x0005, 0x001F, -0x004A, 0x00B3, -0x0192,
     0x02B1, -0x039E, 0x04F8, -0x05A6, 0x7939, -0x05A6, 0x04F8, -0x039E, 0x02B1, -0x0192,
     0x00B3, -0x004A, 0x001F, -0x0005, -0x0002, 0x0003, -0x0001, 0x0, 0x0},
    {0, -0x0001, 0x0003, -0x0008, 0x0006, 0x0005, -0x001B, 0x00A6, -0x01A8, 0x0372,
     -0x05BF, 0x09B8, -0x11B4, 0x74BB, 0x0C9D, -0x0267, -0x0043, 0x0132, -0x00E3, 0x00A2,
     -0x004B, 0x003C, -0x0013, 0x0003, 0x0, -0x0002, 0x0, 0x0, 0x0},
    {-0x0001, 0x0003, -0x0008, 0x0011, -0x0010, 0x000A, 0x006B, -0x016D, 0x0350, -0x0623,
     0x0BCD, -0x1780, 0x6794, 0x234C, -0x0A78, 0x0400, -0x010A, 0x0009, 0x0034, -0x0054,
     0x0041, -0x0022, 0x000A, -0x0001, 0x0, 0x0001, 0x0, 0x0, 0x0},
    {0x0002, -0x0008, 0x0010, -0x0023, 0x002B, 0x001A, -0x00EB, 0x027B, -0x0548, 0x0AFA,
     -0x16FA, 0x53E0, 0x3C07, -0x1249, 0x080E, -0x0347, 0x015B, -0x0044, -0x0017, 0x0046,
     -0x0023, 0x0011, -0x0005, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
    {-0x0005, 0x0011, -0x0023, 0x0046, -0x0017, -0x0044, 0x015B, -0x0347, 0x080E, -0x1249,
     0x3C07, 0x53E0, -0x16FA, 0x0AFA, -0x0548, 0x027B, -0x00EB, 0x001A, 0x002B, -0x0023,
     0x0010, -0x0008, 0x0002, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}
};

static const int16_t s_zigzag18[7][25] = {
    {0x0, -0x5, 0x11, -0x23, 0x46, -0x17, -0x44, 0x15b, -0x347, 0x80e, -0x1249, 0x3c07, 0x53e0,
     -0x16fa, 0xafa, -0x548, 0x27b, -0xeb, 0x1a, 0x2b, -0x23, 0x10, -0x8, 0x2, 0x0},
    {0x0, -0x2, 0xa, -0x22, 0x41, -0x54, 0x34, 0x9, -0x10a, 0x400, -0xa78, 0x234c, 0x6794,
     -0x1780, 0xbcd, -0x623, 0x350, -0x16d, 0x6b, 0xa, -0x10, 0x11, -0x8, 0x3, -0x1},
    {-0x2, 0x0, 0x3, -0x13, 0x3c, -0x4b, 0xa2, -0xe3, 0x132, -0x43, -0x267, 0xc9d, 0x74bb,
     -0x11b4, 0x9b8, -0x5bf, 0x372, -0x1a8, 0xa6, -0x1b, 0x5, 0x6, -0x8, 0x3, -0x1},
    {-0x1, 0x3, -0x2, -0x5, 0x1f, -0x4a, 0xb3, -0x192, 0x2b1, -0x39e, 0x4f8, -0x5a6, 0x7939,
     -0x5a6, 0x4f8, -0x39e, 0x2b1, -0x192, 0xb3, -0x4a, 0x1f, -0x5, -0x2, 0x3, -0x1},
    {-0x1, 0x3, -0x8, 0x6, 0x5, -0x1b, 0xa6, -0x1a8, 0x372, -0x5bf, 0x9b8, -0x11b4, 0x74bb,
     0xc9d, -0x267, -0x43, 0x132, -0xe3, 0xa2, -0x4b, 0x3c, -0x13, 0x3, 0x0, -0x2},
    {-0x1, 0x3, -0x8, 0x11, -0x10, 0xa, 0x6b, -0x16d, 0x350, -0x623, 0xbcd, -0x1780, 0x6794,
     0x234c, -0xa78, 0x400, -0x10a, 0x9, 0x34, -0x54, 0x41, -0x22, 0xa, -0x2, 0x0},
    {0x0, 0x2, -0x8, 0x10, -0x23, 0x2b, 0x1a, -0xeb, 0x27b, -0x548, 0xafa, -0x16fa, 0x53e0,
     0x3c07, -0x1249, 0x80e, -0x347, 0x15b, -0x44, -0x17, 0x46, -0x23, 0x11, -0x5, 0x0}
};

static int16_t zigzag_interp(const int16_t *ring, int table_idx, uint8_t p) {
    const int16_t *tbl = s_zigzag[table_idx];
    int32_t sum = 0;
    for (int i = 0; i < 29; i++)
        sum += ((int32_t)ring[(p - i) & 0x1F] * (int32_t)tbl[i]) >> 15;
    return clamp16_xa(sum);
}

static int16_t zigzag_interp18(const int16_t *ring, int table_idx, uint8_t p) {
    const int16_t *tbl = s_zigzag18[table_idx];
    int32_t sum = 0;
    for (int i = 0; i < 25; i++)
        sum += ((int32_t)ring[(p + 32 - 25 + i) & 0x1F] * (int32_t)tbl[i]);
    return clamp16_xa(sum >> 15);
}

static void resample_xa_37800(XaAdpcmState *xa, AudioFifo *fifo,
                               const int16_t *frames, uint32_t num_frames, bool stereo) {
    uint8_t p = xa->ring_p;
    uint8_t sixstep = xa->sixstep;
    for (uint32_t i = 0; i < num_frames; i++) {
        xa->ring[0][p] = frames[stereo ? i*2 : i];
        xa->ring[1][p] = frames[stereo ? i*2+1 : i];
        p = (p + 1) % 32;
        if (--sixstep == 0) {
            sixstep = 6;
            for (int j = 0; j < 7; j++) {
                int16_t l = zigzag_interp(xa->ring[0], j, p);
                int16_t r = stereo ? zigzag_interp(xa->ring[1], j, p) : l;
                cdrom_audio_fifo_push(fifo, l, r);
            }
        }
    }
    xa->ring_p = p; xa->sixstep = sixstep;
}

static void resample_xa_18900(XaAdpcmState *xa, AudioFifo *fifo,
                               const int16_t *frames, uint32_t num_frames, bool stereo) {
    uint8_t p = xa->ring18_p;
    uint8_t sixstep = xa->sixstep18;
    for (uint32_t i = 0; i < num_frames; i++) {
        xa->ring18[0][p] = frames[stereo ? i*2 : i];
        xa->ring18[1][p] = frames[stereo ? i*2+1 : i];
        p = (p + 1) % 32;
        if (--sixstep == 0) {
            sixstep = 6;
            for (int j = 0; j < 7; j++) {
                int16_t l = zigzag_interp18(xa->ring18[0], j, p);
                int16_t r = stereo ? zigzag_interp18(xa->ring18[1], j, p) : l;
                cdrom_audio_fifo_push(fifo, l, r);
            }
        }
    }
    xa->ring18_p = p; xa->sixstep18 = sixstep;
}

void cdrom_audio_decode_xa(XaAdpcmState *xa, AudioFifo *fifo, const uint8_t *xa_data,
                            bool stereo, bool bits8, bool rate_18900, bool muted) {
    /* One 128-byte sound group holds num_blocks blocks of 28 samples: 224 for
     * 4-bit XA, 112 for 8-bit. There is no further multiplier — an extra factor
     * of 8 here made every sector claim 18816 output frames instead of 2352, so
     * seven eighths of what reached the audio FIFO was whatever happened to be
     * left in the decode buffer, i.e. noise, and the surplus also swamped the
     * queue. A whole XA sector is 18 groups: 4032 mono samples, 2016 stereo
     * frames, which at 37800 Hz is exactly one sector's worth of playback. */
    const int num_blocks = bits8 ? 4 : 8;
    const int words_per_block = 28;
    const int samples_per_chunk = num_blocks * words_per_block;
    const int frames_per_chunk = stereo ? samples_per_chunk / 2 : samples_per_chunk;
    static int16_t sample_buf[18 * 8 * 28 * 8];
    int32_t prev[2][2];
    prev[0][0] = xa->prev1[0]; prev[0][1] = xa->prev2[0];
    prev[1][0] = xa->prev1[1]; prev[1][1] = xa->prev2[1];
    int16_t *dst = sample_buf;
    for (int chunk = 0; chunk < 18; chunk++) {
        decode_xa_chunk(xa_data + chunk * 128, stereo, bits8, prev, dst);
        dst += frames_per_chunk * (stereo ? 2 : 1);
    }
    xa->prev1[0] = prev[0][0]; xa->prev2[0] = prev[0][1];
    xa->prev1[1] = prev[1][0]; xa->prev2[1] = prev[1][1];
    if (muted) return;
    uint32_t total_frames = (uint32_t)(18 * frames_per_chunk);
    if (rate_18900) resample_xa_18900(xa, fifo, sample_buf, total_frames, stereo);
    else resample_xa_37800(xa, fifo, sample_buf, total_frames, stereo);
}

/* =========================================================================
 * CDDA
 * ========================================================================= */

void cdrom_audio_process_cdda(AudioFifo *fifo, const uint8_t *raw_sector, bool muted) {
    for (int i = 0; i < 588; i++) {
        int16_t l = (int16_t)((uint16_t)raw_sector[i*4+0] | ((uint16_t)raw_sector[i*4+1] << 8));
        int16_t r = (int16_t)((uint16_t)raw_sector[i*4+2] | ((uint16_t)raw_sector[i*4+3] << 8));
        if (!muted) cdrom_audio_fifo_push(fifo, l, r);
    }
}

/* =========================================================================
 * SDL Audio Output
 * ========================================================================= */

static AudioFifo     *s_sdl_fifo = NULL;
static SDL_AudioDeviceID s_sdl_dev = 0;
static Spu           *s_spu = NULL;

void cdrom_audio_set_spu(void *spu_ptr) {
    s_spu = (Spu*)spu_ptr;
}

static void sdl_audio_callback(void *userdata, uint8_t *stream, int len) {
    (void)userdata;
    int16_t *out = (int16_t *)stream;
    int num_stereo = len / (sizeof(int16_t) * 2);

    if (s_spu) {
        /* Fill from SPU circular buffer (generated during emulation) */
        spu_fill_audio(s_spu, out, num_stereo);
    } else {
        /* No SPU: just output CD audio */
        for (int i = 0; i < num_stereo; i++) {
            int16_t l = 0, r = 0;
            if (s_sdl_fifo) cdrom_audio_fifo_pop(s_sdl_fifo, &l, &r);
            out[i*2] = l;
            out[i*2+1] = r;
        }
    }
}

bool cdrom_audio_sdl_open(AudioFifo *fifo) {
    s_sdl_fifo = fifo;
    SDL_AudioSpec want = {0}, have;
    want.freq     = 44100;
    want.format   = AUDIO_S16LSB;
    want.channels = 2;
    want.samples  = 4096;
    want.callback = sdl_audio_callback;

    s_sdl_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (s_sdl_dev == 0) {
        LOG_CDROM_ERROR("[CDROM] SDL_OpenAudioDevice: %s", SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(s_sdl_dev, 0);
    LOG_CDROM_INFO("[CDROM] SDL audio opened: %u Hz, %u ch", have.freq, have.channels);
    return true;
}

void cdrom_audio_sdl_close(void) {
    if (s_sdl_dev) {
        SDL_CloseAudioDevice(s_sdl_dev);
        s_sdl_dev = 0;
    }
    s_sdl_fifo = NULL;
}
