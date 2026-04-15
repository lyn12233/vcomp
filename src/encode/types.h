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

#include "src/math/transform.h"
#include "src/util/pixbuf.h"

#include <stdint.h>

// #define C1_ENC_SB_SZ 64
// #define C1_ENCODE_REF_FRAMES_CNT 16
// #define C1_ENCODE_MB_MAX_PART_CNT 4

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
enum {
    C1_FRAME_I,
    C1_FRAME_P,
};
typedef uint8_t C1_FRAME_TYPE;

/** encoder context
 */
struct c1enc_ctx_s {
    c1_pixbuf_t ref_frames[16];
    uint8_t avail_ref_cnt;
    struct {
    } profile;
};
struct c1enc_frame_s {
    /** the current frame to encode.
        pixel format c3i16 yuv
    */
    struct {
        c1_pixbuf_t pix;
    } buf;
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
    struct {
        c1_pixbuf_t pix;
    } buf;
    struct {
        uint8_t force_all_intra;
        uint8_t is_all_intra;

        int8_t q_index_delta;
    } inf;
    struct c1enc_macro_block_s *root_mb;
};
typedef struct c1enc_super_block_s c1enc_super_block_t;

struct c1enc_macro_block_s {
    struct {
        c1_pixbuf_t pix;
    } buf;
    struct {
        uint8_t is_all_intra;
        uint8_t is_cb;
        C1_TX_2D_SZ size;

        union {
            struct {
                C1_PRED_TYPE pred_type;
                C1_PRED_TYPE pred_mode;
                union {
                    struct {
                    } inter_inf;
                    struct {
                        uint8_t use_cfl;
                    } intra_inf;
                };

                C1_TX_2D_SZ tx_size;
                uint8_t tx_cnt;
                struct c1enc_tx_block_s *tx_blocks;
            } cb_inf;
            struct c1enc_macro_block_s *parts[4];
        };
    } inf;
    struct {
        int32_t sse;
        // ...
    } est;
};
typedef struct c1enc_macro_block_s c1enc_macro_block_t;
struct c1enc_tx_block_s {
    struct {
        c1_pixbuf_t pix;
    } buf;
    struct {
        C1_TX_2D_SZ tx_size;
        C1_TX_2D_TYPE tx_type;
    } inf;
    struct {
        int32_t sse;
    } est;
};

#ifdef __cplusplus
}
#endif

#endif