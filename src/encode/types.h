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

#include "src/util/pixbuf.h"

#include <stdint.h>

// #define C1_ENC_SB_SZ 64
// #define C1_ENCODE_REF_FRAMES_CNT 16
// #define C1_ENCODE_MAX_PART_CNT 4
#define C1_ENC_INTRA_CAND_CNT 2
#define C1_ENC_INTER_CAND_CNT 2

// -- size enums ---

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
    C1_PRED_D45,
    C1_PRED_D135,
    C1_PRED_D37,
    C1_PRED_D113,
    C1_PRED_D157,
    C1_PRED_D203,
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
typedef struct {
    uint8_t mask;
    int32_t r, d;
    int32_t sse, sad;
    float fitness;
} c1enc_rdstat_t;

// --- encoder context ---

struct c1enc_ctx_s {
    c1_pixbuf_t ref_frames[16];
    uint8_t avail_ref_cnt;
    struct {
    } profile;
};

// -- frame, super block, partition and block ---

/** the current frame to encode.
    pixel format c3i16 yuv
*/
struct c1enc_frame_s {
    // buffer
    c1_pixbuf_t pix;

    struct {
        C1_FRAME_TYPE frame_type;

        uint8_t base_q_index;

        uint16_t hgt;
        uint16_t wid;
        uint16_t hgt_per_sb;
        uint16_t wid_per_sb;
    } inf;
    struct c1enc_super_block_s *super_blocks;
};
typedef struct c1enc_frame_s c1enc_frame_t;

/** super block
 super block is a coding unit of 64x64 pixels
*/
struct c1enc_super_block_s {
    // buffer
    int16_t diff_buf[64 * 64 * 3];
    int32_t qcoef_buf[64 * 64 * 3];

    uint16_t sb_y, sb_x;     // super block is at y row and x col in frame
    uint8_t force_all_intra; // ?
    uint8_t is_all_intra;    // ?

    int8_t q_index_delta;

    struct c1enc_partition_s *root;
};
typedef struct c1enc_super_block_s c1enc_super_block_t;

struct c1enc_plane_s {
    // persistence buffers since no compound pred+tx search.
    // pred is measured by sad and tx is measured by ??
    int16_t *diff;   // residual owned by sb. represents the best pred and used by tx
    int32_t *coef;   // coef is the tx result. represents best tx
    int32_t *qcoef;  // ??
    int32_t *dqcoef; // dequantized coef. used for both encoding and reconstruction
};
typedef struct c1enc_plane_s c1enc_plane_t;

// pred/tx mode info and candidates
typedef struct {
    uint8_t use_cfl; // induced from uv mode
    C1_PRED_MODE mode_y;
    C1_PRED_MODE mode_uv;
    int16_t cfl_alpha;
} c1enc_mi_intra_t;
typedef struct {
    C1_PRED_MODE mode;
    int8_t ref_frame;
    c1enc_mv_t mv, mvd;
} c1enc_mi_inter_t;

struct c1enc_block_s {
    C1_PRED_TYPE pred_type;
    C1_2D_SZ size;

    uint16_t sb_y, sb_x; // sb index in frame
    uint8_t yoff, xoff;  // offset in super block
    // now assume tx_largest, tx size is the block size
    // uint8_t wid_per_tx, hgt_per_tx;
    C1_2D_SZ tx_size;
    C1_TX_2D_TYPE tx_type;

    uint8_t intra_cand_cnt;
    uint8_t inter_cand_cnt;

    union {
        // intra mode info
        c1enc_mi_intra_t mi_intra;
        // inter mode info
        c1enc_mi_inter_t mi_inter;
    };

    c1enc_plane_t p[3]; // planes

    c1enc_mi_intra_t intra_cands[C1_ENC_INTRA_CAND_CNT];
    c1enc_mi_inter_t inter_cands[C1_ENC_INTER_CAND_CNT];
    c1enc_rdstat_t intra_cand_stats[C1_ENC_INTRA_CAND_CNT];
    c1enc_rdstat_t inter_cand_stats[C1_ENC_INTER_CAND_CNT];
};
typedef struct c1enc_block_s c1enc_block_t;

struct c1enc_partition_s {
    // buffer

    // attr
    uint8_t is_all_intra; //?
    uint8_t is_partition;
    C1_2D_SZ size;

    union {
        c1enc_block_t *b;
        struct c1enc_partition_s *parts[4];
    };
    c1enc_rdstat_t stats;
};
typedef struct c1enc_partition_s c1enc_partition_t;

// --- helper funcs ---

static uint8_t c1_sz2wid(C1_2D_SZ sz) {
    static const uint8_t lookup[4] = {8, 16, 32, 64};
    return lookup[sz];
}
static uint8_t c1_sz2hgt(C1_2D_SZ sz) {
    static const uint8_t lookup[4] = {8, 16, 32, 64};
    return lookup[sz];
}

#ifdef __cplusplus
}
#endif

#endif