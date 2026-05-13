/** @file encoder/types.h
internal frame encode structures, search option structures.
key definitions:
 - super_block: block of size 64x64
 - macro_block: block between super block and coding block(inc).
*/

#ifndef C1_ENCODE_TYPES_H
    #define C1_ENCODE_TYPES_H
    #ifdef __cplusplus
extern "C" {
    #endif

    #include "util/log.h"
    #include "util/pixbuf.h"

    #include <stdint.h>

    /* table of contents
        - size enums:       45
        - pred mode enums:  55
        - tx enums:         80
        - frame type:       105
        - motion vector:    115
        - rdstat:           125
        - context:          140
        - frame_t:          170
        - super_block_t:    190
        - plane_t:          200
        - mode info:        210
        - block_t:          225
        - partition_t:      260
        - ref_t:            280
        - helper funcs:     300
    */

    // #define C1_ENC_SB_SZ 64
    // #define C1_ENCODE_MAX_PART_CNT 4
    // #define C1_ENC_TX_CAND_CNT 4
    #define C1_ENC_REF_FRAME_CNT 16
    #define C1_ENC_INTRA_CAND_CNT 4
    #define C1_ENC_INTER_CAND_CNT 2
    #define C1_ENC_CACHED_PRED_STAT_CNT 16
    #define C1_SIZE_CNT 4    // 8x8 ... 64x64
    #define C1_TX_TYPE_CNT 4 // dct-dct ...

// --- 2d size enums ---

enum {
    C1_SZ_8_8,
    C1_SZ_16_16,
    C1_SZ_32_32,
    C1_SZ_64_64,
};
typedef uint8_t C1_2D_SZ;

// --- prediction modes ---

enum {
    C1_PRED_INTER,
    C1_PRED_INTRA,
    C1_PRED_SKIP,
};
typedef uint8_t C1_PRED_TYPE;
enum {
    C1_PRED_MVNEAREST,
    C1_PRED_MVNEAR,
    C1_PRED_MVNEW,

    C1_PRED_DC,
    C1_PRED_H,
    C1_PRED_V,
    C1_PRED_D45,  // 45-z1; todo: add d225?
    C1_PRED_D135, // z2
    C1_PRED_D67,  // z1
    C1_PRED_D113, // z2
    C1_PRED_D157, // z2
    C1_PRED_D203, // z3
    C1_PRED_PAETH,
};
typedef uint8_t C1_PRED_MODE;

// --- transform sizes and types ---

enum {
    TX1TYPE_DCT_8,
    TX1TYPE_DCT_16,
    TX1TYPE_DCT_32,
    TX1TYPE_DCT_64,
    TX1TYPE_IDEN_8,
    TX1TYPE_IDEN_16,
    TX1TYPE_IDEN_32,
    // TXTYPE_ADST_8,
    // TXTYPE_ADST_16,
};
typedef uint8_t C1_TX_1D_TYPE;

enum {
    TX2TYPE_DCT_DCT,
    TX2TYPE_H_DCT,
    TX2TYPE_V_DCT,
    TX2TYPE_IDEN,
    // TXTYPE_ADST_ADST,
    // TXTYPE_H_ADST,
    // TXTYPE_V_ADST,
};
typedef uint8_t C1_TX_2D_TYPE;

// --- frame type ---

enum {
    C1_FRAME_I,
    C1_FRAME_P,
};
typedef uint8_t C1_FRAME_TYPE;

// --- motion vector ---

struct c1enc_mv_s {
    int16_t y, x;
};
typedef struct c1enc_mv_s c1enc_mv_t;

// --- rate-distortion statistics ---

    #define C1_RD_RATE_BIT (1 << 0)
    #define C1_RD_DIS_BIT (1 << 1)
    #define C1_RD_SSE_BIT (1 << 2)
    #define C1_RD_SAD_BIT (1 << 3)
    #define C1_RD_FIT_BIT (1 << 4)
/** compound rdstat information. existing statistics are indicated by "mask"
 currently only sad(sum of absolute difference) is considered, as it is the simplest.
*/
typedef struct {
    uint8_t mask;
    union {
        struct {
            uint32_t r, d;
        };
        float sse;
        uint32_t sad;
        float fitness;
    };
} c1enc_rdstat_t;

// --- encoder context ---

/** encoder context.
 mainly used for inter prediciton mode, which requires to refer to previous frames.
 after encoding a frame, essential frame data are pushed back to the ctx's fields.
 init with {0}.
*/
struct c1enc_ctx_s {
    /** inversed transformed frames, with wid and hgt implied.
        the frame is a duplication of frm->pix which should hold the inverse transform result for some time.
    */
    c1_pixbuf_t ref_frames[C1_ENC_REF_FRAME_CNT];
    /** number of available reference frames.
        new frames are stored at the back to reduce memmove's.
    */
    uint8_t avail_ref_cnt;
    /** last n p-frames, to determine if a new i frame is necessary
     */
    uint8_t consecutive_p_cnt;
    /** estimate qstep and qindex, qstep=0 for undefined. currently unused?
     */
    uint16_t est_qstep;
    uint8_t est_qi;
    /** referenced data stored at per super block level.
        REF_FRAME_CNT slots aligned to REF_FRAME_CNT possible ref frames.
        each slot points to a h*w style array of ref_t, the same size as that of the sb's in corresponding frame.
    */
    struct c1enc_ref_s *sb_refs[C1_ENC_REF_FRAME_CNT]; // super block level reference info
};
typedef struct c1enc_ctx_s c1enc_ctx_t;

// -- frame, super block, partition and block ---

/** the current frame to encode.
    pixel format c3i16 yuv
*/
struct c1enc_frame_s {
    // buffer
    c1_pixbuf_t pix;

    C1_FRAME_TYPE frame_type; // also a early stage frame type indicator

    uint8_t q_index;
    int8_t q_index_delta;

    uint16_t hgt;
    uint16_t wid;
    uint16_t hgt_per_sb;
    uint16_t wid_per_sb;

    struct c1enc_super_block_s *super_blocks;
};
typedef struct c1enc_frame_s c1enc_frame_t;

/** super block
 super block is a coding unit of 64x64 pixels
*/
struct c1enc_super_block_s {
    // buffer
    /** buf for residuals, first assigned to each partition, then assign 3 planes. for example for 4 32x32 blocks, it is
     [[y1u1v1]...[y4u4v4]]. offest indicated by buf_offs in partitions and blocks.
     */
    int16_t diff_buf[64 * 64 * 3];
    int32_t *coef_bufs; // coef, qcoef and dqcoef 3*3*64*64

    uint16_t sb_y, sb_x; // super block is at y row and x col in frame

    uint8_t has_q_index;
    uint8_t q_index;
    int8_t q_index_delta;

    struct c1enc_partition_s *root;
};
typedef struct c1enc_super_block_s c1enc_super_block_t;

struct c1enc_plane_s {
    // persistence buffers since no compound pred+tx search.
    // pred is measured by sad and tx is measured by ??
    int16_t *diff;   // residual owned by sb. represents the best pred and used by tx
    int32_t *coef;   // coef is the tx result. represents best tx, used for qi refinement
    int32_t *qcoef;  // quantized coef. used for both encoding(eob calc) and reconstruction
    int32_t *dqcoef; // dequantized coef. used for reconstruction.
    // border cache owned by each plane. allocated by malloc, size bh+bw
    int16_t *border_above; // above border cache
    int16_t *border_left;  // left border cache
};
typedef struct c1enc_plane_s c1enc_plane_t;

// pred/tx mode info and candidates
typedef struct {
    uint8_t use_cfl; // induced from uv mode
    C1_PRED_MODE mode_y;
    C1_PRED_MODE mode_uv;
    int8_t cfl_alpha_u, cfl_alpha_v; // -16 ~ 16 (/64)
} c1enc_mi_intra_t;
typedef struct {
    C1_PRED_MODE mode;
    int8_t ref_frame;
    c1enc_mv_t mv; // store the whole mv instead of mvd to reduce reference overhead
} c1enc_mi_inter_t;
typedef struct {
    C1_2D_SZ tx_size;
    C1_TX_2D_TYPE tx_type;
} c1enc_tx_inf_t;
typedef struct {
    // C1_2D_SZ size; unused. this is block size
    C1_PRED_MODE mode;
    uint8_t ci; // color idx: yuv
    uint8_t use_cfl;
    uint8_t has_cfl_alpha;
    c1enc_mv_t mv;
} c1pd_option_t;

struct c1enc_block_s {
    C1_2D_SZ size;

    // attrs necessary to collect above and left pixels

    uint16_t sb_y, sb_x; // sb index in frame
    uint8_t yoff, xoff;  // offset in super block

    // attrs for cached mv
    uint8_t has_ref_mv;
    c1enc_mv_t ref_mv;

    // attrs for search result

    // transform info
    uint8_t has_tx_cand;
    c1enc_tx_inf_t tx_inf;
    c1enc_rdstat_t tx_stat;

    // predict mode search candidates and cached stats

    uint8_t intra_cand_cnt; // count in intra_cand
    uint8_t inter_cand_cnt; // count in inter_cand
    uint8_t cached_pred_stat_cnt;

    uint8_t pred_type_determined; // init as 0
    C1_PRED_TYPE pred_type;       // only gathered by some func to avd redundant cand check
    // best cand is stored in intra_cands or inter_cands

    c1enc_plane_t p[3]; // planes

    c1enc_mi_intra_t intra_cands[C1_ENC_INTRA_CAND_CNT + 1]; // the extra slot is to ease bobble sort
    c1enc_mi_inter_t inter_cands[C1_ENC_INTER_CAND_CNT + 1];
    c1enc_rdstat_t intra_cand_stats[C1_ENC_INTRA_CAND_CNT + 1];
    c1enc_rdstat_t inter_cand_stats[C1_ENC_INTER_CAND_CNT + 1];
    c1pd_option_t cached_preds[C1_ENC_CACHED_PRED_STAT_CNT];
    uint32_t cached_pred_stats[C1_ENC_CACHED_PRED_STAT_CNT];
};
typedef struct c1enc_block_s c1enc_block_t;

struct c1enc_partition_s {
    // buffer

    // attr
    uint8_t is_all_intra; //?
    uint8_t is_partition;
    C1_2D_SZ size;
    // attr neccessary to re-create a block
    uint8_t y, x;
    uint16_t sb_y, sb_x;
    uint32_t buf_offs; // offset in buf provided by super block, per int16_t
    c1enc_super_block_t *sb;

    c1enc_block_t *b;
    struct c1enc_partition_s *parts[4];

    c1enc_rdstat_t stats;
};
typedef struct c1enc_partition_s c1enc_partition_t;

struct c1enc_ref_s {
    uint8_t is_partition;
    uint8_t y, x; // maybe useful?
    union {
        struct c1enc_ref_s *refs[4];
        struct {
            C1_2D_SZ size;
            c1enc_mv_t mv;
            // other fields?
        };
    };
};
typedef struct c1enc_ref_s c1enc_ref_t;

// --- helper funcs ---

static uint8_t c1_sz2wid(C1_2D_SZ sz) {
    static const uint8_t lookup[4] = {8, 16, 32, 64};
    return lookup[sz];
}
static uint8_t c1_sz2hgt(C1_2D_SZ sz) {
    static const uint8_t lookup[4] = {8, 16, 32, 64};
    return lookup[sz];
}
static int c1_pred_is_inter(C1_PRED_MODE mode) {
    return mode >= C1_PRED_MVNEAREST && mode <= C1_PRED_MVNEW;
}
static int c1_pred_is_intra(C1_PRED_MODE mode) {
    return mode >= C1_PRED_DC && mode <= C1_PRED_PAETH;
}
static int16_t c1_abs_i16(int16_t a) {
    return a > 0 ? a : 0 - a;
}
static int32_t c1_abs_i32(int32_t a) {
    return a > 0 ? a : 0 - a;
}
static int16_t c1_abs_dif_i16(int16_t a, int16_t b) {
    return a > b ? a - b : b - a;
}
static int64_t c1_clamp64(int64_t a, int64_t min_, int64_t max_) {
    return a < min_ ? min_ : a > max_ ? max_ : a;
}
static int64_t c1_clamp32(int32_t a, int32_t min_, int32_t max_) {
    return a < min_ ? min_ : a > max_ ? max_ : a;
}
static int16_t c1_clamp16(int16_t a, int16_t min_, int16_t max_) {
    return a < min_ ? min_ : a > max_ ? max_ : a;
}
static int16_t c1_rgb2y(uint8_t r, uint8_t g, uint8_t b) {
    return (int16_t)(77 * r + 150 * g + 29 * b + 0x80) >> 8;
}
static int16_t c1_rgb2u(uint8_t r, uint8_t g, uint8_t b) {
    return (int16_t)(127 * r - 84 * g - 43 * b + 0x8080) >> 8;
}
static int16_t c1_rgb2v(uint8_t r, uint8_t g, uint8_t b) {
    return (int16_t)(127 * r - 107 * g - 20 * b + 0x8080) >> 8;
}
/** ref id to array index */
static const c1_pixbuf_t *c1enc_ctx_frame_at(const c1enc_ctx_t *ctx, const c1_pixbuf_t *pix, uint8_t ref_id) {
    int idx = (int)ctx->avail_ref_cnt - ref_id;
    assert_fatal(idx >= 0 && idx <= ctx->avail_ref_cnt);
    return idx == ctx->avail_ref_cnt ? pix : ctx->ref_frames + idx;
}

    #define C1_ROUND_UP(val, div) (((val) + (div) - 1) / (div))
    #define C1_ROUND_MID(val, div) (((val) + ((div) >> 1)) / (div))
    #define C1_ROUND_BITS(val, bits) (((val) + (1 << ((bits) - 1))) >> (bits))

    #ifdef __cplusplus
}
    #endif

#endif

/*
history:
2026.5.9: todo: encoder structs should be used for decoder, causing 2 overheads: diff buf and cands.
*/