/** @file writer.h this file contains bit stream writing contexts and structs.
 note: most of the bitstream dumping process are in encoder.c.

 content in bitstream is a composition symbols and fixed length bits. symbols are coded by ent coder given cdf
 contexts. symbols and fixed length bits are mixed together since the behavior of enccoder reading/writing bitstream is
 determined(?).

 to mix symbols and raw bits into a single bitstream, pre-carry mechanism in arithmetic coder should be considered.
 therefore, case a read for fixed length bits is required, the decoder shoudl gather and reset the currently symbol
 context. to relieve overhead of this, fixed bits are positioned after symbols for given struct, also align to byte
 boundary after fixed length bit sequence.

 # description on symbols and semantics
 > extistence of some symbols is conditional, but now make them always exist allowing for fut interface chg.

 ## frame header
 contents:
    - frame_has_sz_inf(S)->width,height: size of frame
        - for the first frame, frame_has_sz_inf is set to 1
        - to read width, first read n=l(4)+1 indicating n<=16 bits, then width=l(n)+1
    - frame_has_bqi->bqi:base q index
        - for the first frame, frame_has_bqi is set to 1
        - n=l(3)+1,bqi=l(n)
    - frame_type: i or p frame, ctx=prev_frame==p||no_prev_frame
    - ref_id: reference frame id, 1-16, only for p frame

 ## super block
 order: diagonal traversal defined in encoder.c c1enc_encode.

 contents:
    - qid_type!->qid: q index delta
        - qid_type not for first sb
        - qid: qid_sign_bit(-1,0,1), l(3)(1,8), l(n)(1,256)

 ## partition
 order: tl, tr, bl, br

 contents:
    - is_partition:determines next stept to code 4 parts or a block
        - ctx=bsize, for 8x8, is_partition=0

 ## block
 contents:
    - is_intra->(intra_inf|inter_inf)
        - for i frame, is_intra=1;
        - intra_inf: ymode; has_cfl->(cfl_inf|has_uv->(uvmode)) currently unimpl cfl?
            - cfl_inf: cfl_sign_bit;[cfl_u];[cfl_v];(-16~16)
            - ctx for uvmode is direction of ymode: z1,z2,z3 and none
        - inter_inf: mode->(mode==newmv)mv; otherwise mv=refmv;
            - currently the first mv ref is a temporal ref, the second is 0
            - mv: sign bit and data bit
        - tx_size, tx_type, not ctx considered.

 ## txb(transform block)
 order: for(bi=0,i=0;bi<bh;bi+=txh,i++)...->[i,j]
 contents: skip!->(eobpt, coefficients:(qcoef_sign, qcoef, qcoef_br, ...))
    - eobpt<2**n> in (0,3), eob_br in [0,3], eob<2**n>=2**(n-4+eobpt)+(eob_br+1)*2*(n-6+eobpt) (n>=6)
    - context for eobpt and eob_br is [n][eobpt]
    - qcoef sign, then qcoef in [0,3], qcoef_br in [0,3], codes [0..12], ozerwis read n non zero golomb_length, then n+1
        golomb_bits.
    - context for qcoef, qcoef_br is [mag][region] where mag in [0,3] sums min(abs(qcoef[idx]),3)/3, where idx is coef
        above, left and tl, oob treated as 0. region looks up a table [tx_size][min(row,4)][min(col,4)] in [0,3], for
        non 2d tx, a pos offset [4,6]

 ## coefficient
 order: for txh*txw bits, scan[txh*txw-1]..scan[0], where scan=scans[tx_size][ctx(tx_type)]

 # example writer process:
 ```
 c1enc_ctx_t ctx={0};
 ctx.enc=...
 ctx.cdfs=...
 ```

 */
#ifndef C1_ENCODE_WRITER_H
#define C1_ENCODE_WRITER_H
#ifdef __cplusplus
extern "C" {
#endif

#include "math/entcoder.h"
#include "types.h"
#include "util/mem.h"

#include <stdint.h>

// macros that define number of symbols.

#define C1_CDFCNT_FRAME_HAS_SZ 2
#define C1_CDFCNT_FRAME_HAS_BQI 2
#define C1_CDFCNT_FRAME_TYPE 2
#define C1_CDFCNT_FRAME_REF_ID 16

#define C1_CDFCNT_SB_QID_TYPE 3 // last, none, new
#define C1_CDFCNT_SB_QID_SIGN_BIT 3
#define C1_CDFCNT_SB_QID_DATA_BIT 16

#define C1_CDFCNT_PART_IS_PART 2
#define C1_CDFCNT_B_IS_INTRA 2
#define C1_CDFCNT_B_YMODE 10 // dc to paeth
// #define C1_CDFCNT_B_HAS_CFL 2
#define C1_CDFCNT_B_HAS_UVMODE 2
#define C1_CDFCNT_B_UVMODE 10
#define C1_CDFCNT_B_MV_TYPE 3 // nearest(eq the temporal one), near(0), and new
#define C1_CDFCNT_B_MV_SIGN_BIT 3
#define C1_CDFCNT_B_MV_DATA_BIT 16
#define C1_CDFCNT_B_TX_SZ 4   // /1~/8
#define C1_CDFCNT_B_TX_TYPE 7 // dct-dct ~ iden

#define C1_CDFCNT_TXB_SKIP_COEF 2
#define C1_CDFCNT_TXB_EOBPT 4
#define C1_CDFCNT_TXB_EOBBR 4
#define C1_CDFCNT_TXB_QCOEF_SIGN 2
#define C1_CDFCNT_TXB_QCOEF 4
#define C1_CDFCNT_TXB_QCOEF_BR 4
#define C1_CDFCNT_TXB_GOLOMB_LEN 2
#define C1_CDFCNT_TXB_GOLOMB_BIT 2

#define C1_CDF_IS_PART_SZ_CTX_CNT 3 // 16x16, 32x32, 64x64
#define C1_CDF_UVMODE_DIR_CTX_CNT 4 // z1,z2,z3,none
#define C1_CDF_EOB_SZ_CTX_CNT 3     // 64, 256, 1024
#define C1_CDF_QCOEF_MAG_CTX_CNT 4
#define C1_CDF_QCOEF_REG_CTX_CNT 7

// --- cdf context struct ---

struct c1_cdf_ctx_s {
    // -- frame ---
    // frame contains size info
    uint16_t frame_has_sz_inf_cdf[C1_CDFCNT_FRAME_HAS_SZ + 1];
    uint16_t frame_has_bqi_cdf[C1_CDFCNT_FRAME_HAS_BQI + 1];
    uint16_t frame_type[C1_CDFCNT_FRAME_TYPE + 1];
    uint16_t frame_ref_id[C1_CDFCNT_FRAME_REF_ID + 1];

    // super block
    uint16_t sb_qid_type[C1_CDFCNT_SB_QID_TYPE + 1];
    uint16_t sb_qid_sign_bit[C1_CDFCNT_SB_QID_SIGN_BIT + 1];
    uint16_t sb_qid_data_bit[C1_CDFCNT_SB_QID_DATA_BIT + 1];

    // partition
    uint16_t part_is_partition[C1_CDF_IS_PART_SZ_CTX_CNT][C1_CDFCNT_PART_IS_PART + 1];

    // block
    uint16_t b_is_intra[C1_CDFCNT_B_IS_INTRA + 1];
    uint16_t b_ymode[C1_CDFCNT_B_YMODE + 1];
    // uint16_t b_has_cfl[C1_CDFCNT_B_HAS_CFL+1];
    uint16_t b_has_uvmode[C1_CDFCNT_B_HAS_UVMODE + 1];
    uint16_t b_uvmode[C1_CDF_UVMODE_DIR_CTX_CNT][C1_CDFCNT_B_UVMODE + 1];
    uint16_t b_mv_type[C1_CDFCNT_B_MV_TYPE + 1];
    uint16_t b_mv_sign_bit[C1_CDFCNT_B_MV_SIGN_BIT + 1];
    uint16_t b_mv_data_bit[C1_CDFCNT_B_MV_DATA_BIT + 1];
    uint16_t b_tx_sz[C1_CDFCNT_B_TX_SZ + 1];
    uint16_t b_tx_type[C1_CDFCNT_B_TX_TYPE + 1];

    // txb
    uint16_t txb_skip_coef[C1_CDFCNT_TXB_SKIP_COEF + 1];
    uint16_t txb_eobpt[C1_CDF_EOB_SZ_CTX_CNT][C1_CDFCNT_TXB_EOBPT + 1];
    uint16_t txb_eobbr[C1_CDF_EOB_SZ_CTX_CNT][C1_CDFCNT_TXB_EOBPT][C1_CDFCNT_TXB_EOBBR + 1];
    uint16_t txb_qcoef_sign[C1_CDFCNT_TXB_QCOEF_SIGN + 1];
    uint16_t txb_qcoef[C1_CDF_QCOEF_MAG_CTX_CNT][C1_CDF_QCOEF_REG_CTX_CNT][C1_CDFCNT_TXB_QCOEF + 1];
    uint16_t txb_qcoef_br[C1_CDF_QCOEF_MAG_CTX_CNT][C1_CDF_QCOEF_REG_CTX_CNT][C1_CDFCNT_TXB_QCOEF_BR + 1];
    uint16_t txb_golomb_len[C1_CDFCNT_TXB_GOLOMB_LEN + 1];
    uint16_t txb_golomb_bit[C1_CDFCNT_TXB_GOLOMB_BIT + 1];
};
typedef struct c1_cdf_ctx_s c1_cdf_ctx_t;

// default cdf context
extern const c1_cdf_ctx_t c1_cdf_ctx_def;

enum {
    C1_BITSTRM_ENC,
    C1_BITSTRM_DEC,
};

// compound bitstream(encoder)
struct c1_encstrm_s {
    uint8_t prev_is_symbol; // if prev is symbol and reading raw, call enc done and align; if prev is raw and reading
                            // symbol, align
    uint8_t bit_offs;       // measure non byte aligned raw bits
    uint32_t buf_sz;        // avail size to output
    uint32_t buf_sz_max;    // when buf sz exceeds this, try write callback.
    uint32_t buf_resv_sz;   // buffer reserved size
    uint8_t *buf;           // cache output of entropy encoder.
    c1ent_enc_t enc;        // entropy encoder instance
    int (*write_callback)(void *buf, uint32_t sz, void *ctx);
    void *write_ctx; // context for the write callback, should be a reference;
};
typedef struct c1_encstrm_s c1_encstrm_t;

struct c1_decstrm_s {
    uint8_t prev_is_symbol;
    // uint32_t buf_sz;
    // uint32_t offs; // per bits offset for reading from sbuf
    c1_sptr_t *sbuf;
    c1ent_strstrm_t *strstrm;
    c1ent_dec_t dec;
};
typedef struct c1_decstrm_s c1_decstrm_t;

// -- wrappers for entropy coder

c1_encstrm_t c1_es_create(int (*write_callback)(void *, uint32_t, void *), void *write_ctx, uint32_t buf_sz_max);
int c1_es_align(c1_encstrm_t *es);
int c1_es_write_raw(c1_encstrm_t *es, uint32_t len, uint64_t bits);
int c1_es_write_sym(c1_encstrm_t *es, int sym, uint16_t *cdf, int nbsym, int update_cdf);
int c1_es_flush(c1_encstrm_t *es);
int c1_es_clear(c1_encstrm_t *es);

c1_decstrm_t c1_ds_create(c1_sptr_t *sbuf, uint32_t buf_sz);
int c1_ds_align(c1_decstrm_t *ds);
uint64_t c1_ds_read_raw(c1_decstrm_t *ds, uint32_t len);
int c1_ds_read_sym(c1_decstrm_t *ds, uint16_t *cdf, int nbsym, int update_cdf);
int c1_ds_clear(c1_decstrm_t *ds);

int c1enc_wr_frame_header(const c1enc_frame_t *frm, const c1enc_ctx_t *ctx, int update_cdf, //
                          int ref_id);
int c1enc_wr_super_block(const c1enc_frame_t *frm, const c1enc_ctx_t *ctx, int update_cdf);

#ifdef __cplusplus
}
#endif

#endif