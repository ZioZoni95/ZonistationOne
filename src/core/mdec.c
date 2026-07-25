/*
 * MDEC — Macroblock Decoder
 * Algorithms ported from DuckStation (mdec.cpp): IDCT_Old, DecodeRLE_Old,
 * YUVToRGB_Old, YUVToMono, CopyOutBlock, HandleSetQuantTable, HandleSetScale.
 * State machine mirrors DuckStation's Execute() loop.
 */

#include "mdec.h"
#include "log.h"
#include "lua_debug.h"
#include <string.h>

/* -------------------------------------------------------------------------
 * FIFO helpers
 * ---------------------------------------------------------------------- */

static bool in_empty(const Mdec* m)    { return m->in_count == 0; }
static bool in_full(const Mdec* m)     { return m->in_count == MDEC_IN_FIFO_HW; }
static uint32_t in_space(const Mdec* m){ return MDEC_IN_FIFO_HW - m->in_count; }

static uint16_t in_pop(Mdec* m) {
    uint16_t v = m->in_buf[m->in_head];
    m->in_head = (m->in_head + 1) % MDEC_IN_FIFO_HW;
    m->in_count--;
    return v;
}
static void in_push(Mdec* m, uint16_t v) {
    m->in_buf[m->in_tail] = v;
    m->in_tail = (m->in_tail + 1) % MDEC_IN_FIFO_HW;
    m->in_count++;
}
static bool out_empty(const Mdec* m)   { return m->out_count == 0; }
static void out_push(Mdec* m, uint32_t v) {
    m->out_buf[m->out_tail] = v;
    m->out_tail = (m->out_tail + 1) % MDEC_OUT_FIFO_W;
    m->out_count++;
}
static uint32_t out_pop(Mdec* m) {
    uint32_t v = m->out_buf[m->out_head];
    m->out_head = (m->out_head + 1) % MDEC_OUT_FIFO_W;
    m->out_count--;
    return v;
}

/* -------------------------------------------------------------------------
 * Status register
 * ---------------------------------------------------------------------- */
static uint32_t mdec_get_status(const Mdec* m) {
    uint32_t s = 0;
    if (out_empty(m))   s |= (1u << 31);  /* data_out_fifo_empty */
    if (in_full(m))     s |= (1u << 30);  /* data_in_fifo_full */
    if (m->decode_state != MDEC_ST_IDLE)
                        s |= (1u << 29);  /* command_busy */
    bool in_req  = m->enable_dma_in  && (in_space(m) >= 64);
    bool out_req = m->enable_dma_out && !out_empty(m);
    if (in_req)  s |= (1u << 28);         /* data_in_request  (DMA0) */
    if (out_req) s |= (1u << 27);         /* data_out_request (DMA1) */
    s |= ((uint32_t)m->output_depth & 3u) << 25;
    if (m->output_signed)  s |= (1u << 24);
    s |= ((uint32_t)m->output_bit15 & 1u) << 23;
    s |= ((m->current_block + 4u) % (uint32_t)MDEC_NUM_BLOCKS) << 16;
    if (m->decode_state != MDEC_ST_IDLE && m->remaining_halfwords >= 2)
        s |= (uint16_t)((m->remaining_halfwords / 2u) - 1u);
    return s;
}

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */
static inline int32_t sign_extend10(int32_t v) { return (v << 22) >> 22; }
static inline int32_t sign_extend9 (int32_t v) { return (v << 23) >> 23; }
static inline int32_t clamp_s32(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

/* -------------------------------------------------------------------------
 * RLE decode (DuckStation DecodeRLE_Old)
 * Fills blk[64] using the zigzag->linear reorder table.
 * Returns true when one 8×8 block is complete.
 * ---------------------------------------------------------------------- */
static const uint8_t zagzig[64] = {
     0, 1, 8,16, 9, 2, 3,10,17,24,32,25,18,11, 4, 5,
    12,19,26,33,40,48,41,34,27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63
};

static bool mdec_decode_rle(Mdec* m, int16_t* blk, const uint8_t* qt) {
    if (m->current_coefficient == 64) {
        memset(blk, 0, 64 * sizeof(int16_t));

        /* Skip 0xFE00 padding halfwords */
        uint16_t n;
        for (;;) {
            if (in_empty(m) || m->remaining_halfwords == 0)
                return false;
            n = in_pop(m);
            m->remaining_halfwords--;
            if (n != 0xFE00) break;
        }

        m->current_coefficient = 0;
        m->current_q_scale = (n >> 10) & 0x3F;
        int32_t val = sign_extend10(n & 0x3FF) * (int32_t)qt[0];
        if (m->current_q_scale == 0)
            val = sign_extend10(n & 0x3FF) * 2;
        val = clamp_s32(val, -0x400, 0x3FF);
        if (m->current_q_scale > 0)
            blk[zagzig[0]] = (int16_t)val;
        else
            blk[0] = (int16_t)val;
    }

    while (!in_empty(m) && m->remaining_halfwords > 0) {
        uint16_t n = in_pop(m);
        m->remaining_halfwords--;

        m->current_coefficient += ((n >> 10) & 0x3Fu) + 1u;
        if (m->current_coefficient < 64) {
            int32_t val = (sign_extend10(n & 0x3FF) *
                           (int32_t)qt[m->current_coefficient] *
                           (int32_t)m->current_q_scale + 4) / 8;
            if (m->current_q_scale == 0)
                val = sign_extend10(n & 0x3FF) * 2;
            val = clamp_s32(val, -0x400, 0x3FF);
            if (m->current_q_scale > 0)
                blk[zagzig[m->current_coefficient]] = (int16_t)val;
            else
                blk[m->current_coefficient] = (int16_t)val;
        }

        if (m->current_coefficient >= 63) {
            m->current_coefficient = 64;
            return true;
        }
    }
    return false;
}

/* -------------------------------------------------------------------------
 * IDCT (DuckStation IDCT_Old)
 * Two-pass 8×8 matrix multiply using scale_table.
 * ---------------------------------------------------------------------- */
static void mdec_idct(Mdec* m, int16_t* blk) {
    int64_t tmp[64];
    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            int64_t sum = 0;
            for (int u = 0; u < 8; u++)
                sum += (int32_t)blk[u*8 + x] * (int32_t)m->scale_table[y*8 + u];
            tmp[x + y*8] = sum;
        }
    }
    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            int64_t sum = 0;
            for (int u = 0; u < 8; u++)
                sum += tmp[u + y*8] * (int32_t)m->scale_table[x*8 + u];
            int32_t v = sign_extend9((int32_t)(sum >> 32) + (int32_t)((sum >> 31) & 1));
            blk[x + y*8] = (int16_t)clamp_s32(v, -128, 127);
        }
    }
}

/* -------------------------------------------------------------------------
 * YUV → RGB conversion (DuckStation YUVToRGB_Old)
 * Writes one 8×8 quadrant into block_rgb[0..255] (16×16 layout).
 * ---------------------------------------------------------------------- */
static void mdec_yuv_to_rgb(Mdec* m, uint32_t xx, uint32_t yy,
                             const int16_t* Crblk,
                             const int16_t* Cbblk,
                             const int16_t* Yblk) {
    int16_t addval = m->output_signed ? 0 : 0x80;
    for (uint32_t y = 0; y < 8; y++) {
        for (uint32_t x = 0; x < 8; x++) {
            int16_t R  = Crblk[((x+xx)/2) + ((y+yy)/2)*8];
            int16_t B  = Cbblk[((x+xx)/2) + ((y+yy)/2)*8];
            int16_t G  = (int16_t)(-0.3437f*(float)B + -0.7143f*(float)R);
            R = (int16_t)(1.402f*(float)R);
            B = (int16_t)(1.772f*(float)B);
            int16_t Y  = Yblk[x + y*8];
            R = (int16_t)clamp_s32((int32_t)Y + R, -128, 127) + addval;
            G = (int16_t)clamp_s32((int32_t)Y + G, -128, 127) + addval;
            B = (int16_t)clamp_s32((int32_t)Y + B, -128, 127) + addval;
            m->block_rgb[(x+xx) + (y+yy)*16] =
                (uint32_t)(uint16_t)R |
                ((uint32_t)(uint16_t)G << 8) |
                ((uint32_t)(uint16_t)B << 16);
        }
    }
}

/* Mono: writes 64 entries from blocks[0] into block_rgb[0..63] */
static void mdec_yuv_to_mono(Mdec* m) {
    int32_t addval = m->output_signed ? 0 : 0x80;
    for (int i = 0; i < 64; i++) {
        int32_t v = clamp_s32(sign_extend9((int32_t)m->blocks[0][i]), -128, 127);
        m->block_rgb[i] = (uint32_t)(v + addval);
    }
}

/* -------------------------------------------------------------------------
 * Copy decoded block_rgb to output FIFO (DuckStation CopyOutBlock)
 * ---------------------------------------------------------------------- */
static void mdec_copy_out_block(Mdec* m) {
    switch (m->output_depth) {
        case 0: { /* 4-bit mono: 64 pixels → 8 words */
            const uint32_t* p = m->block_rgb;
            for (int i = 0; i < 64/8; i++) {
                uint32_t v = (p[0]>>4) | ((p[1]>>4)<<4) | ((p[2]>>4)<<8) | ((p[3]>>4)<<12)
                           | ((p[4]>>4)<<16) | ((p[5]>>4)<<20) | ((p[6]>>4)<<24) | ((p[7]>>4)<<28);
                p += 8;
                out_push(m, v);
            }
            break;
        }
        case 1: { /* 8-bit mono: 64 pixels → 16 words */
            const uint32_t* p = m->block_rgb;
            for (int i = 0; i < 64/4; i++) {
                uint32_t v = p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24);
                p += 4;
                out_push(m, v);
            }
            break;
        }
        case 2: { /* 24-bit color: 256 pixels → 192 words, packed tightly */
            uint32_t idx = 0, state = 0, rgb = 0;
            while (idx < 256) {
                switch (state) {
                    case 0: rgb = m->block_rgb[idx++]; state = 1; break;
                    case 1:
                        rgb |= (m->block_rgb[idx] & 0xFFu) << 24;
                        out_push(m, rgb);
                        rgb = m->block_rgb[idx] >> 8;
                        idx++; state = 2; break;
                    case 2:
                        rgb |= m->block_rgb[idx] << 16;
                        out_push(m, rgb);
                        rgb = m->block_rgb[idx] >> 16;
                        idx++; state = 3; break;
                    case 3:
                        rgb |= m->block_rgb[idx] << 8;
                        out_push(m, rgb);
                        idx++; state = 0; break;
                }
            }
            break;
        }
        case 3: { /* 15-bit color: 256 pixels → 128 words (2 pixels/word) */
            uint16_t a = (uint16_t)m->output_bit15 << 15;
            for (int i = 0; i < 256; i += 2) {
                uint32_t c0 = m->block_rgb[i];
                uint16_t r0 = (c0 >>  3) & 0x1Fu;
                uint16_t g0 = (c0 >> 11) & 0x1Fu;
                uint16_t b0 = (c0 >> 19) & 0x1Fu;
                uint16_t col0 = r0 | (uint16_t)(g0<<5) | (uint16_t)(b0<<10) | a;

                uint32_t c1 = m->block_rgb[i+1];
                uint16_t r1 = (c1 >>  3) & 0x1Fu;
                uint16_t g1 = (c1 >> 11) & 0x1Fu;
                uint16_t b1 = (c1 >> 19) & 0x1Fu;
                uint16_t col1 = r1 | (uint16_t)(g1<<5) | (uint16_t)(b1<<10) | a;

                out_push(m, (uint32_t)col0 | ((uint32_t)col1 << 16));
            }
            break;
        }
        default: break;
    }
    LOG_MDEC_DEBUG("[MDEC] Block copy out done, out_count=%u", m->out_count);
    /* State: idle if no more data, else loop for next macroblock */
    m->decode_state = (m->remaining_halfwords == 0) ? MDEC_ST_IDLE : MDEC_ST_DECODING;
}

/* -------------------------------------------------------------------------
 * Decode one macroblock (mono or color)
 * Returns true if macroblock fully decoded and pushed to output FIFO.
 * ---------------------------------------------------------------------- */
static bool mdec_decode_macroblock(Mdec* m) {
    if (m->output_depth <= 1) {
        /* Mono path: one 8×8 block */
        if (!out_empty(m)) return false;
        if (!mdec_decode_rle(m, m->blocks[0], m->iq_y)) return false;
        mdec_idct(m, m->blocks[0]);
        m->current_block = 0;
        m->current_coefficient = 64;
        m->current_q_scale = 0;
        LOG_MDEC_DEBUG("[MDEC] Decoded mono, %u hw remain", m->remaining_halfwords);
        mdec_yuv_to_mono(m);
        mdec_copy_out_block(m);
        return true;
    } else {
        /* Color path: 6 blocks (Cr, Cb, Y1..Y4) */
        for (; m->current_block < (uint32_t)MDEC_NUM_BLOCKS; m->current_block++) {
            const uint8_t* qt = (m->current_block >= 2) ? m->iq_y : m->iq_uv;
            if (!mdec_decode_rle(m, m->blocks[m->current_block], qt)) return false;
            mdec_idct(m, m->blocks[m->current_block]);
        }
        /* All 6 blocks decoded. Wait for previous output to drain. */
        if (!out_empty(m)) return false;
        m->current_block = 0;
        m->current_coefficient = 64;
        m->current_q_scale = 0;
        LOG_MDEC_DEBUG("[MDEC] Decoded color macroblock, %u hw remain", m->remaining_halfwords);
        mdec_yuv_to_rgb(m, 0, 0, m->blocks[0], m->blocks[1], m->blocks[2]);
        mdec_yuv_to_rgb(m, 8, 0, m->blocks[0], m->blocks[1], m->blocks[3]);
        mdec_yuv_to_rgb(m, 0, 8, m->blocks[0], m->blocks[1], m->blocks[4]);
        mdec_yuv_to_rgb(m, 8, 8, m->blocks[0], m->blocks[1], m->blocks[5]);
        lua_debug_notify("mdec_macroblock");
        mdec_copy_out_block(m);
        return true;
    }
}

/* -------------------------------------------------------------------------
 * Command handlers
 * ---------------------------------------------------------------------- */
static void mdec_handle_set_qtable(Mdec* m) {
    /* 32 halfwords = 64 bytes = luma quantization table */
    for (int i = 0; i < 64; i += 2) {
        uint16_t hw = in_pop(m);
        m->iq_y[i]   = (uint8_t)(hw & 0xFF);
        m->iq_y[i+1] = (uint8_t)(hw >> 8);
    }
    m->remaining_halfwords -= 32;
    if (m->remaining_halfwords >= 32) {
        /* Optionally followed by chroma table */
        for (int i = 0; i < 64; i += 2) {
            uint16_t hw = in_pop(m);
            m->iq_uv[i]   = (uint8_t)(hw & 0xFF);
            m->iq_uv[i+1] = (uint8_t)(hw >> 8);
        }
        m->remaining_halfwords -= 32;
    }
    LOG_MDEC_DEBUG("[MDEC] SetQuantTable done");
}

static void mdec_handle_set_scale(Mdec* m) {
    /* 64 halfwords = 64 × int16_t, stored TRANSPOSED.
     *
     * The IDCT reads the matrix as scale_table[y*8 + u] (frequency u -> output
     * position y), but the table arrives from the game in the opposite
     * orientation, so it has to be flipped on the way in — DuckStation does
     * exactly this in SetScaleMatrix() (mdec.cpp), which the rest of this file
     * was ported from while this one step was missed.
     *
     * Without the transpose the IDCT basis is wrong for every block: a DC-only
     * macroblock, which must decode to a flat patch of colour, instead comes
     * out as a smooth blob that fades to the block edges — so FMV frames
     * rendered as a regular grid of blobs, one per macroblock. */
    uint16_t packed[64];
    for (int i = 0; i < 64; i++)
        packed[i] = in_pop(m);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            m->scale_table[y * 8 + x] = (int16_t)packed[x * 8 + y];
    m->remaining_halfwords -= 64;
    LOG_MDEC_DEBUG("[MDEC] SetScale done");
}

/* -------------------------------------------------------------------------
 * Main execute loop (mirrors DuckStation's Execute())
 * ---------------------------------------------------------------------- */
void mdec_execute(Mdec* m) {
    for (;;) {
        switch (m->decode_state) {

            case MDEC_ST_IDLE: {
                if (m->in_count < 2) goto finished;
                uint32_t lo = in_pop(m);
                uint32_t hi = in_pop(m);
                uint32_t cw = lo | (hi << 16);
                uint8_t cmd = (uint8_t)((cw >> 29) & 7u);
                m->output_depth   = (uint8_t)((cw >> 27) & 3u);
                m->output_signed  =           (cw >> 26) & 1u;
                m->output_bit15   = (uint8_t)((cw >> 25) & 1u);
                /* Clear output FIFO on new command */
                m->out_head = m->out_tail = m->out_count = 0;
                uint32_t num_words;
                MdecDecodeState new_state;
                switch (cmd) {
                    case 1: /* DecodeMacroblock */
                        num_words = cw & 0xFFFFu;
                        new_state = MDEC_ST_DECODING;
                        break;
                    case 2: /* SetIqTable */
                        num_words = 16u + ((cw & 1u) ? 16u : 0u);
                        new_state = MDEC_ST_SET_QTABLE;
                        break;
                    case 3: /* SetScale */
                        num_words = 32u;
                        new_state = MDEC_ST_SET_SCALE;
                        break;
                    default:
                        LOG_MDEC_WARN("[MDEC] Invalid command 0x%08x (cmd=%u)", cw, cmd);
                        num_words = cw & 0xFFFFu;
                        new_state = MDEC_ST_NOCOMMAND;
                        break;
                }
                m->remaining_halfwords = num_words * 2u;
                m->decode_state = new_state;
                LOG_MDEC_DEBUG("[MDEC] Command: cmd=%u depth=%u signed=%d nwords=%u",
                               cmd, m->output_depth, m->output_signed, num_words);
                continue;
            }

            case MDEC_ST_DECODING: {
                bool decoded = mdec_decode_macroblock(m);
                if (!decoded) {
                    if (m->remaining_halfwords == 0 &&
                        m->current_block < (uint32_t)MDEC_NUM_BLOCKS) {
                        /* Stream ended before all blocks decoded — abort */
                        m->current_block = 0;
                        m->current_coefficient = 64;
                        m->current_q_scale = 0;
                        m->decode_state = MDEC_ST_IDLE;
                        continue;
                    }
                    goto finished;
                }
                /* copy_out_block() already set decode_state. Let DMA drain output. */
                goto finished;
            }

            case MDEC_ST_WRITING:
                goto finished;

            case MDEC_ST_SET_QTABLE: {
                if (m->in_count < m->remaining_halfwords) goto finished;
                mdec_handle_set_qtable(m);
                m->decode_state = MDEC_ST_IDLE;
                continue;
            }

            case MDEC_ST_SET_SCALE: {
                if (m->in_count < m->remaining_halfwords) goto finished;
                mdec_handle_set_scale(m);
                m->decode_state = MDEC_ST_IDLE;
                continue;
            }

            case MDEC_ST_NOCOMMAND: {
                uint32_t consume = m->remaining_halfwords < m->in_count
                                   ? m->remaining_halfwords : m->in_count;
                for (uint32_t i = 0; i < consume; i++) in_pop(m);
                m->remaining_halfwords -= consume;
                if (m->remaining_halfwords > 0) goto finished;
                m->decode_state = MDEC_ST_IDLE;
                continue;
            }
        }
    }
finished:;
}

/* =========================================================================
 * Public API
 * ====================================================================== */

void mdec_init(Mdec* m) {
    memset(m, 0, sizeof(Mdec));
    m->current_coefficient = 64;  /* 64 = start-of-block sentinel */
    LOG_MDEC_INFO("[MDEC] Initialized");
}

uint32_t mdec_read(Mdec* m, uint32_t addr) {
    if (addr == 0x1F801824) {
        uint32_t s = mdec_get_status(m);
        LOG_MDEC_DEBUG("[MDEC] Status read -> 0x%08x", s);
        return s;
    }
    if (addr == 0x1F801820) {
        return mdec_dma_out(m);
    }
    return 0;
}

void mdec_write(Mdec* m, uint32_t addr, uint32_t value) {
    if (addr == 0x1F801824) {
        LOG_MDEC_DEBUG("[MDEC] Control <- 0x%08x", value);
        if (value & (1u << 31)) {
            mdec_init(m);
            LOG_MDEC_INFO("[MDEC] Software reset");
            return;
        }
        m->enable_dma_in  = (value >> 30) & 1u;
        m->enable_dma_out = (value >> 29) & 1u;
        mdec_execute(m);
        return;
    }
    if (addr == 0x1F801820) {
        in_push(m, (uint16_t)value);
        in_push(m, (uint16_t)(value >> 16));
        mdec_execute(m);
        return;
    }
}

void mdec_dma_in(Mdec* m, uint32_t word) {
    if (m->in_count + 2 > MDEC_IN_FIFO_HW) {
        LOG_MDEC_WARN("[MDEC] Input FIFO overflow");
        return;
    }
    in_push(m, (uint16_t)word);
    in_push(m, (uint16_t)(word >> 16));
    mdec_execute(m);
}

uint32_t mdec_dma_out(Mdec* m) {
    if (out_empty(m)) {
        LOG_MDEC_WARN("[MDEC] Output FIFO empty on read");
        return 0xFFFFFFFF;
    }
    uint32_t v = out_pop(m);
    if (out_empty(m))
        mdec_execute(m);
    return v;
}

bool mdec_input_has_space(const Mdec* m) {
    return in_space(m) >= 2;
}

bool mdec_output_has_data(const Mdec* m) {
    return !out_empty(m);
}
