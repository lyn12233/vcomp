#include "quant.h"
#include "math/transform.h"
#include "tables.h"
#include "types.h"
#include "util/log.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>


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
    const int max_eob = c1tx__get_max_eob(tx_size);
    memset(temp_out, 0, area * 3 * sizeof(int32_t)); // 3*bh*bw
    for (int ci = 0; ci < 3; ci++) {
        int16_t *in = b->p[ci].diff; // bh*bw
        for (int bi = 0; bi < bh; bi += tx_hgt) {
            for (int bj = 0; bj < bw; bj += tx_wid) {
                // gather compact residuals to temp from in
                for (int i = 0; i < tx_hgt; i++) {
                    for (int j = 0; j < tx_wid; j++) {
                        temp_in[i * tx_wid + j] = in[(bi + i) * bw + bj + j];
                    }
                }
                // transform
                c1tx_option_t tx_opt = {tx_size, tx_type};
                c1tx_txfm2d(temp_in, temp_out + area * ci, &tx_opt);
            }
        }
    }
    int32_t cur_rate = c1tx__measure(b, (int32_t *[3]){temp_out, temp_out + area, temp_out + area * 2}, //
                                     opt, tx_size, c1tx__get_scan_id(tx_type));
    // update tx info and coef case first or better
    if (!b->has_tx_cand || b->tx_stat.r < cur_rate) {
        b->tx_inf = (c1enc_tx_inf_t){tx_size, tx_type};
        b->tx_stat = (c1enc_rdstat_t){C1_RD_RATE_BIT, .r = cur_rate};
        for (int ci = 0; ci < 3; ci++) {
            memcpy(b->p[ci].coef, temp_out + area * ci, area * sizeof(int32_t));
        }
    }

    return 0;
}

int c1tx_search_b(c1enc_block_t *b, c1tx_search_option_t opt) {
    // allocate temp buffers: temp_in+temp_out, together, i32[bh*bw*3]+i16[txh*txw]<=...
    const int area = c1_sz2hgt(b->size) * c1_sz2wid(b->size);
    int32_t *temp = malloc(area * (3 * sizeof(int32_t) + sizeof(int16_t)));
    assert_fatal(temp);

    // traverse. courrently only considered squares
    for (C1_2D_SZ tx_size = b->size; tx_size + opt.size_depth >= b->size;) {
        for (C1_TX_2D_TYPE tx_type = TX2TYPE_DCT_DCT; tx_type < opt.tx_rng_max; tx_type++) {
            c1tx__try_tx(b, (void *)temp + area * 3, temp, opt, tx_size, tx_type);
        }
        if (tx_size == C1_SZ_8_8)
            break;
        else
            tx_size--;
    }
    return 0;
}

static void c1__invert_quant(uint16_t *quant, uint16_t *shift, uint32_t q) {
    // (1) get l=floorlog2
    uint32_t tmp = q, l = 0;
    while (tmp > 1)
        tmp >>= 1, l++;
    // (2) calc mult 16 bits renmant which is 1.*2**(16) -> 0.*2**16, and avd 0
    uint32_t m = (1 << (16 + l)) / q + 1;
    *quant = (uint16_t)(m - (1 << 16));
    *shift = (uint16_t)(16 - l);
}

void c1_lookup_init_q_inf() {
    for (int ci = 0; ci < 3; ci++) {
        for (int qi = 0; qi < 256; qi++) {
            c1__invert_quant(&c1_lookup_q_dc_inf[ci][qi].mult, &c1_lookup_q_dc_inf[ci][qi].shift,
                             c1_lookup_q_dc[ci][qi]);
            c1__invert_quant(&c1_lookup_q_ac_inf[ci][qi].mult, &c1_lookup_q_ac_inf[ci][qi].shift,
                             c1_lookup_q_ac[ci][qi]);
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
    int32_t dev = (int32_t)sqrtf((float)(xx - x * x));
    int16_t res = (int16_t)(mean - ((3 * dev * qp) >> 7));
    return res > 0 ? res : 1;
}

static uint16_t c1enc__est_q_v2(const int32_t *coef, int nbcoef_log2, uint8_t qp) {
    assert_fatal(qp <= 128);
    const int nbcoef = 1 << nbcoef_log2;
    int eob = (nbcoef * qp) >> 7;
    eob -= (eob > 0); // clamp to 0..nbcoef-1
    return (uint16_t)c1_clamp32(c1_abs_i32(coef[eob]) * 2, 0, UINT16_MAX);
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
    frm->q_index = (uint8_t)(tot_qi + nb / 2) / (nb);
    for (int i = 0; i < nb; i++) {
        c1enc_super_block_t *sb = frm->super_blocks + i;
        int16_t qdelta = c1_clamp16(sb->q_index - frm->q_index, INT8_MIN, INT8_MAX);
        sb->q_index_delta = (int8_t)qdelta;
        sb->q_index = (uint8_t)(frm->q_index + qdelta);
    }
    return 0;
}
int c1enc_sb_gather_qi(c1enc_super_block_t *sb, uint8_t qp) {
    uint16_t q = c1enc__est_q_v1(sb->coef_buf, 12, qp);
    sb->q_index = c1enc__bisect_qi(c1_lookup_q_dc[0], q, 0, 255);
    return 0;
}
