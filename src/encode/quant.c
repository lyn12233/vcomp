#include "quant.h"
#include "types.h"
#include "encoder.h"
#include "math/transform.h"
#include "tables.h"
#include "types.h"
#include "util/log.h"
#include "util/mem.h"
#include "util/pixbuf.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

c1_profile_t c1tx_search_sb_prof = {0};
#define C1TX_SEARCH_SB_ENTER() c1_profile_enter(&c1tx_search_sb_prof)
#define C1TX_SEARCH_SB_EXIT() c1_profile_exit(&c1tx_search_sb_prof)
#define C1TX_SEARCH_SB_STEP(step) c1_profile_step(&c1tx_search_sb_prof, step)

uint8_t c1_lookup_q_inf_inited = 0;
c1_quant_t c1_lookup_q_dc_inf[3][256];
c1_quant_t c1_lookup_q_ac_inf[3][256];

static uint8_t c1tx__get_scan_id(C1_TX_2D_TYPE tx_type) {
    static const uint8_t lookup[C1_TX_TYPE_CNT] = {0 /*dct-dct*/, 1 /*h-dct*/, 2 /*v-dct*/, 0};
    return lookup[tx_type];
}

static uint16_t c1tx__get_max_eob(C1_2D_SZ tx_size) {
    static const uint16_t lookup[C1_SIZE_CNT] = {64, 256, 1024, 1024};
    return lookup[tx_size];
}

static uint16_t c1tx__get_eob(const int32_t *coef, C1_2D_SZ tx_size, uint8_t scan_id, uint16_t qstep) {
    const uint16_t max_eob = c1tx__get_max_eob(tx_size);
    const uint16_t *scan = c1_lookup_scans[tx_size][scan_id];
    uint16_t res = max_eob;
    for (int i = max_eob - 1; i >= 0; i--) {
        if (c1_abs_i32(coef[scan[i]]) < qstep / 2)
            res--;
        else
            break;
    }
    return res;
}

// currently only measures the number of non zeros (for 64x64, exclude implicit 0's)
static int32_t c1tx__measure(c1enc_block_t *b, int32_t(*coef[3]), //
                             c1tx_search_option_t opt, C1_2D_SZ tx_size, uint8_t scan_id) {
    if (opt.measure == C1TX_MEASURE_NOP)
        return 0;
    int32_t res = 0;
    const int stride = c1_sz2hgt(tx_size) * c1_sz2wid(tx_size);
    const int area = c1_sz2hgt(b->size) * c1_sz2wid(b->size);
    for (int ci = 0; ci < 3; ci++) {
        int32_t *coef_ci = coef[ci];
        for (int offs = 0; offs < area; offs += stride) {
            res += area - c1tx__get_eob(coef_ci, tx_size, scan_id, opt.qstep);
            coef_ci += stride;
        }
    }
    return res;
}

/** the core function calling and evaluating transform given tx size and type
 this func first writes coef to temporary buffer "temp_out", evaluate it and conditionally update to block_t's fields.
 in the percieved hierarchical encode decisioning, the prediction mode search and txfm mode search are separated, thus
 best candidate info of these are stored separately. tx search here do not buffer multiple cands because it is less
 likely to conduct redundantly(?)
 @param b block_t
 @param temp_in temporary input for txfm, should be at least the size of txh*txw
 @param temp_out temporary output for txfm, size 3*bh*bw, conditionally copied to coef buf in "b".
 @param opt tx search option
*/
static int c1tx__try_tx(c1enc_block_t *b, int16_t *temp_in, int32_t *temp_out, //
                        c1tx_search_option_t opt, C1_2D_SZ tx_size, C1_TX_2D_TYPE tx_type) {
    const int tx_hgt = c1_sz2hgt(tx_size), tx_wid = c1_sz2wid(tx_size);
    const int bh = c1_sz2hgt(b->size), bw = c1_sz2wid(b->size);
    const int area = c1_sz2hgt(b->size) * c1_sz2wid(b->size);

    if (b->has_tx_cand && b->tx_inf.tx_size == tx_size && b->tx_inf.tx_type == tx_type)
        return 0;

    memset(temp_out, 0, area * 3 * sizeof(int32_t)); // 3*bh*bw

    for (int ci = 0; ci < 3; ci++) {

        int16_t *in = b->p[ci].diff; // bh*bw
        int32_t *tx_out = temp_out + area * ci;

        for (int bi = 0; bi < bh; bi += tx_hgt) {
            for (int bj = 0; bj < bw; bj += tx_wid) {
                // gather compact residuals to temp from in
                for (int i = 0; i < tx_hgt; i++) {
                    for (int j = 0; j < tx_wid; j++) {
                        temp_in[i * tx_wid + j] = in[(bi + i) * bw + bj + j];
                    }
                }
                // transform
                c1tx_option_t tx_opt = {.txsize = tx_size, .txtype = tx_type};
                c1tx_txfm2d(temp_in, tx_out, &tx_opt);
                tx_out += tx_hgt * tx_wid;
            }
        }
    }
    int32_t cur_rate = c1tx__measure(b, (int32_t *[3]){temp_out, temp_out + area, temp_out + area * 2}, //
                                     opt, tx_size, c1tx__get_scan_id(tx_type));
    // update tx info and coef case first or better
    if (!b->has_tx_cand || b->tx_stat.r > cur_rate) {
        b->tx_inf = (c1enc_tx_inf_t){.tx_size = tx_size, .tx_type = tx_type};
        b->tx_stat = (c1enc_rdstat_t){.mask = C1_RD_RATE_BIT, .r = cur_rate};
        for (int ci = 0; ci < 3; ci++) {
            memcpy(b->p[ci].coef, temp_out + area * ci, area * sizeof(int32_t));
        }
        b->has_tx_cand = 1;
    }

    return 0;
}

static int c1tx__search_b(c1enc_block_t *b, c1tx_search_option_t opt) {
    // allocate temp buffers: temp_in+temp_out, together, i32[bh*bw*3]+i16[txh*txw]<=bh*bw*(3*4+2)
    const int area = c1_sz2hgt(b->size) * c1_sz2wid(b->size);
    int32_t *temp = malloc(area * (3 * sizeof(int32_t) + sizeof(int16_t)));
    assert_fatal(temp);

    // traverse. courrently only considered squares
    for (C1_2D_SZ tx_size = C1_SZ_8_8; tx_size <= b->size && tx_size < C1_SZ_8_8 + opt.size_depth; tx_size++) {
        for (C1_TX_2D_TYPE tx_type = TX2TYPE_DCT_DCT; tx_type < opt.tx_rng_max; tx_type++) {
            c1tx__try_tx(b, (void *)temp + area * 3, temp, opt, tx_size, tx_type);
        }
    }
    return 0;
}

static int c1tx__search_p(c1enc_partition_t *p, c1tx_search_option_t opt) {
    if (p->is_partition) {
        for (int i = 0; i < 4; i++) {
            c1tx__search_p(p->parts[i], opt);
        }
    } else {
        c1tx__search_b(p->b, opt);
    }
    return 0;
}

int c1tx_search_sb(c1enc_super_block_t *sb, c1tx_search_option_t opt) {
    C1TX_SEARCH_SB_ENTER();
    c1enc_sb_require_coef_bufs(sb, 3);
    C1TX_SEARCH_SB_STEP(1);
    c1tx__search_p(sb->root, opt);
    C1TX_SEARCH_SB_EXIT();
    return 0;
}

static int c1tx__b_recon(c1enc_block_t *b) {
    assert_fatal(b->has_tx_cand);

    const C1_2D_SZ tx_size = b->tx_inf.tx_size;
    const C1_TX_2D_TYPE tx_type = b->tx_inf.tx_type;
    const int txh = c1_sz2hgt(tx_size), txw = c1_sz2wid(tx_size);
    const int bh = c1_sz2hgt(b->size), bw = c1_sz2wid(b->size);
    const int area = c1_sz2hgt(b->size) * c1_sz2wid(b->size);

    int16_t *temp_out = c1_mpool_alloc_def(txh * txw * sizeof(int16_t));
    assert_fatal(temp_out);

    for (int ci = 0; ci < 3; ci++) {

        int32_t *inv_tx_in = b->p[ci].coef;
        int16_t *out = b->p[ci].diff;

        for (int bi = 0; bi < bh; bi += txh) {
            for (int bj = 0; bj < bw; bj += txw) {
                // transform
                c1tx_option_t tx_opt = {.txsize = tx_size, .txtype = tx_type};
                c1tx_inv_txfm2d(inv_tx_in, temp_out, &tx_opt);
                inv_tx_in += txh * txw;

                for (int i = 0; i < txh; i++) {
                    for (int j = 0; j < txw; j++) {
                        out[(bi + i) * bw + bj + j] = temp_out[i * txw + j];
                    }
                }

            } // bj
        } // bi
    }

    return 0;
}

static int c1tx__part_recon(c1enc_partition_t *p) {
    if (p->is_partition) {
        for (int i = 0; i < 4; i++) {
            c1tx__part_recon(p->parts[i]);
        }
    } else {
        c1tx__b_recon(p->b);
    }
    return 0;
}

int c1tx_reconstruct(c1enc_super_block_t *sb) {
    return c1tx__part_recon(sb->root);
}

int c1enc__part_dqc2c(c1enc_partition_t *p) {
    if (p->is_partition) {
        for (int i = 0; i < 4; i++) {
            c1enc__part_dqc2c(p->parts[i]);
        }
    } else {
        c1enc_block_t *b = p->b;
        for (int ci = 0; ci < 3; ci++) {
            // swap coef and dqcoef ptrs
            int32_t *tmp = b->p[ci].coef;
            b->p[ci].coef = b->p[ci].dqcoef;
            b->p[ci].dqcoef = tmp;
        }
    }
    return 0;
}

int c1enc_sb_dqc2c(c1enc_super_block_t *sb) {
    assert_fatal(sb->coef_bufs);
    return c1enc__part_dqc2c(sb->root);
}

static void c1__invert_quant(uint16_t *quant, uint16_t *shift, uint32_t q) {
    // (1) get l=floorlog2
    uint32_t tmp = q, l = 0;
    while (tmp > 1)
        tmp >>= 1, l++;
    // (2) calc 16 bits mult which is 1.*2**(16) -> 0.*2**16, and avd 0
    uint32_t m = (1 << (16 + l + 1)) / q + 1; // 16+l: fraction bits + shift
    *quant = (uint16_t)(m - (1 << 16));       // multiplier to remnant
    *shift = (uint16_t)l;                     // left shift l
}

void c1_lookup_init_q_inf() {
    for (int ci = 0; ci < 3; ci++) {
        for (int qi = 0; qi < 256; qi++) {
            c1__invert_quant(&c1_lookup_q_dc_inf[ci][qi].mult, &c1_lookup_q_dc_inf[ci][qi].shift,
                             c1_lookup_q_dc[ci][qi]);
            c1__invert_quant(&c1_lookup_q_ac_inf[ci][qi].mult, &c1_lookup_q_ac_inf[ci][qi].shift,
                             c1_lookup_q_ac[ci][qi]);
            c1_lookup_q_dc_inf[ci][qi].qstep = c1_lookup_q_dc[ci][qi];
            c1_lookup_q_ac_inf[ci][qi].qstep = c1_lookup_q_ac[ci][qi];
        }
    }
}

static uint16_t c1enc__est_q_v1(const int32_t *coef, int nbcoef_log2, uint8_t qp) {
    assert_fatal(qp <= 128);
    uint64_t x = 0, xx = 0;
    const int nbcoef = 1 << nbcoef_log2;
    for (int i = 0; i < nbcoef; i++) {
        x += c1_abs_i32(coef[i]);
        xx += coef[i] * coef[i];
    }
    int32_t mean = (int32_t)(x >> nbcoef_log2);
    int32_t dev = ((int32_t)sqrtf((float)(xx - x * x))) >> nbcoef_log2;
    int16_t res = (int16_t)(mean - ((3 * dev * qp) >> 7));
    return res > 0 ? res : 1;
}

// min heap to underpin kth coef qstep estimator
// clang-format off
static void c1enc__est_heapswapi32(int32_t*a, int32_t*b){int32_t tmp=*a;*a=*b;*b=tmp;}
static void c1enc__est_heapup(int32_t *h, int idx){
    while(idx>0){
        int pidx=(idx-1)>>1;
        if(h[pidx]<=h[idx])break;
        c1enc__est_heapswapi32(h+pidx, h+idx);
        idx=pidx;
    }
}
static void c1enc__est_heapdown(int32_t*h, int sz, int idx){
    while(1){
        int l=idx*2+1,r=idx*2+2, i=idx;
        if(l<sz&&h[l]<h[i])i=l;
        if(r<sz&&h[r]<h[i])i=r;
        if(i==idx)break;
        c1enc__est_heapswapi32(h+idx, h+i);
        idx=i;
    }
}
// clang-format on

static uint16_t c1enc__est_q_v2(const int32_t *coef, int nbcoef_log2, uint8_t qp) {
    assert_fatal(qp <= 128);
    const int nbcoef = 1 << nbcoef_log2;
    const int k = nbcoef / 16 * qp / 128;
    if (k <= 0)
        return UINT16_MAX;
    int32_t *heap = c1_mpool_alloc_def(k * sizeof(int32_t));
    int tot = 0;
    for (int i = 0; i < nbcoef / 16; i++) {
        int32_t v = c1_abs_i32(coef[i * 16 + i * 5 % 16]);
        if (tot < k) {
            heap[tot] = v;
            c1enc__est_heapup(heap, tot);
            tot++;
        } else if (v > heap[0]) {
            heap[0] = v;
            c1enc__est_heapdown(heap, tot, 0);
        }
    }
    uint16_t qstep = (uint16_t)c1_clamp32(heap[0], 0, UINT16_MAX);
    c1_mpool_dealloc_def(k * sizeof(int32_t), heap);
    return qstep;
}

static uint8_t c1enc__bisect_qi(const uint16_t lookup[256], uint16_t q, uint8_t qi_min, uint8_t qi_max) {
    if (q <= lookup[qi_min])
        return qi_min;
    if (q >= lookup[qi_max])
        return qi_max;
    uint8_t qi_mid = (uint8_t)(((uint16_t)qi_min + qi_max) / 2);
    if (qi_mid == qi_min)
        return qi_mid;
    if (qi_mid == qi_max)
        return qi_min; //?
    if (q < lookup[qi_mid]) {
        return c1enc__bisect_qi(lookup, q, qi_min, qi_mid);
    } else {
        return c1enc__bisect_qi(lookup, q, qi_mid, qi_max);
    }
}

int c1enc_frame_gather_qi(c1enc_frame_t *frm, uint8_t qp) {
    uint32_t tot_qi = 0;
    const uint32_t nb = frm->hgt_per_sb * frm->wid_per_sb;
    for (int i = 0; i < nb; i++) {
        c1enc_sb_gather_qi(frm->super_blocks + i, qp);
        tot_qi += frm->super_blocks[i].q_index;
    }
    frm->q_index = (uint8_t)((tot_qi + nb / 2) / (nb));
    // gather qi again, dismiss large values
    tot_qi = 0;
    for (int i = 0; i < nb; i++) {
        c1enc_sb_gather_qi(frm->super_blocks + i, qp);
        uint8_t qi = frm->super_blocks[i].q_index;
        if (c1_abs_dif_i16(qi, frm->q_index) < 16)
            tot_qi += qi;
    }
    frm->q_index = (uint8_t)((tot_qi + nb / 2) / (nb));

    for (int i = 0; i < nb; i++) {
        c1enc_super_block_t *sb = frm->super_blocks + i;
        int16_t qdelta = c1_clamp16(sb->q_index - frm->q_index, INT8_MIN, INT8_MAX);
        sb->q_index_delta = (int8_t)qdelta;
        sb->q_index = (uint8_t)(frm->q_index + qdelta);
    }
    return 0;
}
int c1enc_sb_gather_qi(c1enc_super_block_t *sb, uint8_t qp) {
    if (sb->has_q_index)
        return 0;
    sb->has_q_index = 1;
    assert_fatal(sb->coef_bufs);
    // todo: use a better strategy
    uint16_t q = c1enc__est_q_v2(sb->coef_bufs + 0, 12, qp);
    sb->q_index = c1enc__bisect_qi(c1_lookup_q_dc[0], q, 0, 255);
    return 0;
}

/** core quantization and dequant logics.
 see [libaom]/aom_dsp/quantize.c:50 (aom_quantize_b_adaptive_helper_c) standard impl considers specialized zero bound,
 qmatrix weight and eob adjust, which are not considered here for simplicity. variable desc: (1) coef_ptr: input c; (2)
 quant_ptr: mult remnant q.mult; (3) round_ptr: rounding adjustment with 5 bit fraction, for small qi is 1/2; (4)
 quant_shift_ptr: the shift multiplier without log2 op; (5) dequant_ptr: actually q.qstep; (6) qcoef_ptr and dqcoef_ptr:
 output.

 @param c[in] coefficient
 @param q[in] quant info
 @param qc[out] quantized coef
 @param dqc[out] dequantized coef

 todo: add a log_scale?
*/
static void c1__quantize_pix(int32_t c, c1_quant_t q, int32_t *qc, int32_t *dqc) {
    const int sign = c < 0 ? -1 : 0;
    const int abs_c = c < 0 ? -c : c; // also (c^sign)-sign
    // (1) rounding and early exit
    if (abs_c < q.qstep / 2) {
        *qc = *dqc = 0;
        return;
    }
    int64_t tmp64 = c1_clamp32(abs_c + q.qstep / 2, 0, INT16_MAX);
    // (2) divide by qstep
    tmp64 = (tmp64 * q.mult >> 16) + tmp64;
    int32_t tmp32 = (int32_t)(tmp64 >> q.shift);
    *qc = (tmp32 ^ sign) - sign; // reserve the sign, fast alg
    // (3) dequantize
    tmp32 = tmp32 * q.qstep;
    *dqc = (tmp32 ^ sign) - sign;
}

static int c1enc__quantize_b(c1enc_block_t *b, uint8_t qi) {
    const int txh = c1_sz2hgt(b->tx_inf.tx_size), txw = c1_sz2wid(b->tx_inf.tx_size);
    const int bh = c1_sz2hgt(b->size), bw = c1_sz2wid(b->size);

    for (uint8_t ci = 0; ci < 3; ci++) {
        // per channel data: c, qc, dqc and quant info
        int32_t *coef = b->p[ci].coef, *qcoef = b->p[ci].qcoef, *dqcoef = b->p[ci].dqcoef;
        c1_quant_t q_dc = c1q_get_q_inf(0, ci, qi);
        c1_quant_t q_ac = c1q_get_q_inf(1, ci, qi);

        // traverse tx blocks
        for (int bi = 0; bi < bh; bi += txh) {
            for (int bj = 0; bj < bw; bj += txw) {

                // traverse in tx block
                for (int i = 0; i < txh; i++) {
                    for (int j = 0; j < txw; j++) {
                        c1_quant_t q = i == 0 && j == 0 ? q_dc : q_ac;
                        // index in block buf
                        // idx = ((bi/txh)*(bw/txw)+(bj/txw))*txh*txw + i*txw+j
                        int idx = bi * bw + bj * txh + i * txw + j;
                        c1__quantize_pix(coef[idx], q, qcoef + idx, dqcoef + idx);
                    }
                }
            }
        }
    }
    return 0;
}

static int c1enc__quantize_p(c1enc_partition_t *p, uint8_t qi) {
    if (p->is_partition) {
        for (int i = 0; i < 4; i++) {
            c1enc__quantize_p(p->parts[i], qi);
        }
    } else {
        c1enc__quantize_b(p->b, qi);
    }
    return 0;
}

int c1enc_quantize_sb(c1enc_super_block_t *sb) {
    assert_fatal_ex(sb->coef_bufs, "call to quantizer but coef(bufs) do not exist");
    c1enc__quantize_p(sb->root, sb->q_index);
    return 0;
}