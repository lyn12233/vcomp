#include "search.h"
#include "predictor.h"
#include "types.h"

#include "util/log.h"
#include "util/pixbuf.h"

#include <limits.h>
#include <stdint.h>

#define C1__UVMODE_SING_SRCH_CNT 4

static int c1enc__cmp(int a, int b) {
    return (a > b) - (a < b);
}

/** calculate sum of asbolute difference of a plane(p)
 @param pix c3i16
*/
static int c1enc__calc_p_sad(const c1enc_block_t *b, const c1_pixbuf_t *pix, const c1pd_option_t *opt) {
    const int bw = c1_sz2wid(b->size), bh = c1_sz2hgt(b->size);
    const int by = (int)b->sb_y * 64 + b->yoff;
    const int bx = (int)b->sb_x * 64 + b->xoff;
    int res = 0;
    c1_pixbuf_t ci_pix = c1_pixbuf_fromchnl(pix, opt->ci);
    for (int i = 0; i < bh; i++) {
        for (int j = 0; j < bw; j++) {
            res += c1_abs_dif_i16(                           //
                *c1_pixbuf_geti16c(&ci_pix, by + i, bx + j), //
                b->p[opt->ci].diff[i * bw + j]               //
            );
        }
    }
    c1_pixbuf_clear(&ci_pix);
    return res;
}

int c1enc_rdstat_cmp(const c1enc_rdstat_t *a, const c1enc_rdstat_t *b) {
    uint8_t mask = a->mask & b->mask;
    if (mask & C1_RD_FIT_BIT) {
        return c1enc__cmp(a->fitness, b->fitness);
    } else if (mask & C1_RD_DIS_BIT) {
        return -c1enc__cmp(a->d, b->d);
    } else if (mask & C1_RD_RATE_BIT) {
        return -c1enc__cmp(a->r, b->r);
    } else if (mask & C1_RD_SSE_BIT) {
        return -c1enc__cmp(a->sse, b->sse);
    } else if (mask & C1_RD_SAD_BIT) {
        return -c1enc__cmp(a->sad, b->sad);
    }
    fatal2("invalid mask %02x & %02x", a->mask, b->mask);
}

int c1enc_block_add_intra_cand(c1enc_block_t *b, const c1enc_mi_intra_t *mi, const c1enc_rdstat_t *stat) {
    c1enc_rdstat_t *stats = b->intra_cand_stats;
    c1enc_mi_intra_t *cands = b->intra_cands;
    const int nbcand = b->intra_cand_cnt;

    // nbcand<=CAND_CNT and the arrays contain CAND_CNT+1 slots
    stats[nbcand] = *stat, cands[nbcand] = *mi;
    for (int i = nbcand; i >= 1; i--) {
        // new cand is now at index i
        if (c1enc_rdstat_cmp(stat, stats + i - 1) > 0) {
            stats[i] = stats[i - 1], cands[i] = cands[i - 1];
            stats[i - 1] = *stat, cands[i - 1] = *mi;
        } else
            break;
    }
    // update candidate count
    b->intra_cand_cnt = nbcand < C1_ENC_INTRA_CAND_CNT ? nbcand + 1 : C1_ENC_INTRA_CAND_CNT;
    return 0;
}
int c1enc_block_add_inter_cand(c1enc_block_t *b, const c1enc_mi_inter_t *mi, const c1enc_rdstat_t *stat) {
    c1enc_rdstat_t *stats = b->inter_cand_stats;
    c1enc_mi_inter_t *cands = b->inter_cands;
    const int nbcand = b->inter_cand_cnt;

    // nbcand<=CAND_CNT and the arrays contain CAND_CNT+1 slots
    stats[nbcand] = *stat, cands[nbcand] = *mi;
    for (int i = nbcand; i >= 1; i--) {
        if (c1enc_rdstat_cmp(stat, stats + i - 1) > 0) {
            stats[i] = stats[i - 1], cands[i] = cands[i - 1];
            stats[i - 1] = *stat, cands[i - 1] = *mi;
        } else
            break;
    }
    // update candidate count
    b->inter_cand_cnt = nbcand < C1_ENC_INTER_CAND_CNT ? nbcand + 1 : C1_ENC_INTER_CAND_CNT;
    return 0;
}

int c1enc_search_intra_b(c1enc_block_t *b, const c1_pixbuf_t *pix, const c1enc_search_option_t *opt) {
    if (opt->intra_try_cfl || !opt->intra_try_uv) {
        // only 1 mode dimension is searched
        c1pd_option_t pred_opt;
        pred_opt.size = b->size;
        pred_opt.use_cfl = 0; // no cfl first
        c1enc_mi_intra_t mi;
        mi.use_cfl = 0;

        for (C1_PRED_MODE mode = C1_PRED_DC; mode < opt->intra_rng_max; mode++) {
            pred_opt.mode = mi.mode_y = mi.mode_uv = mode;
            c1enc_rdstat_t stat = {C1_RD_SAD_BIT};
            for (int ci = 0; ci < 3; ci++) {
                pred_opt.ci = ci;
                c1pd_predict(b, b->p[ci].diff, pix, &pred_opt, NULL);
                stat.sad += c1enc__calc_p_sad(b, pix, &pred_opt);
            }
            c1enc_block_add_intra_cand(b, &mi, &stat);
        } // mode search loop

        // then search cfl
        if (opt->intra_try_cfl) {

            pred_opt.use_cfl = mi.use_cfl = 1;

            for (C1_PRED_MODE mode = C1_PRED_DC; mode < opt->intra_rng_max; mode++) {
                pred_opt.mode = mi.mode_y = mode;
                c1enc_rdstat_t stat = {C1_RD_SAD_BIT};
                int8_t *const cfl_outputs[3] = {NULL, &mi.cfl_alpha_u, &mi.cfl_alpha_v};
                for (int ci = 0; ci < 3; ci++) {
                    pred_opt.ci = ci;
                    c1pd_predict(b, b->p[ci].diff, pix, &pred_opt, cfl_outputs[ci]);
                    stat.sad += c1enc__calc_p_sad(b, pix, &pred_opt);
                }
                c1enc_block_add_intra_cand(b, &mi, &stat);
            } // mode search loop
        }
    } else {
        // no cfl and try different uv: search best modes for y and uv, then search on the axes
        C1_PRED_MODE best_ymode[C1__UVMODE_SING_SRCH_CNT + 1], best_uvmode[C1__UVMODE_SING_SRCH_CNT + 1];
        int best_ysad[C1__UVMODE_SING_SRCH_CNT + 1], best_uvsad[C1__UVMODE_SING_SRCH_CNT + 1];
        int best_cnt = 0;
        for (int i = 0; i < C1__UVMODE_SING_SRCH_CNT; i++) {
            best_ysad[i] = best_uvsad[i] = INT32_MAX;
        }

        c1pd_option_t pred_opt;
        pred_opt.size = b->size;
        pred_opt.use_cfl = 0;
        c1enc_mi_intra_t mi;
        mi.use_cfl = 0;

        for (C1_PRED_MODE mode = C1_PRED_DC; mode < opt->intra_rng_max; mode++) {
            pred_opt.mode = mode; // not setting mi
            int ysad = 0, uvsad = 0;
            int *const sad_targ[3] = {&ysad, &uvsad, &uvsad};
            for (int ci = 0; ci < 3; ci++) {
                pred_opt.ci = ci;
                c1pd_predict(b, b->p[ci].diff, pix, &pred_opt, NULL);
                *(sad_targ[ci]) += c1enc__calc_p_sad(b, pix, &pred_opt);
            }
            best_ymode[best_cnt] = best_uvmode[best_cnt] = mode;
            best_ysad[best_cnt] = ysad, best_uvsad[best_cnt] = uvsad;
            // add single mode cand
            for (int i = best_cnt; i >= 1; i--) {
                // new cand is at index i
                if (ysad < best_ysad[i - 1]) {
                    best_ysad[i] = best_ysad[i - 1], best_ymode[i] = best_ymode[i - 1];
                    best_ysad[i - 1] = ysad, best_ymode[i - 1] = mode;
                } else
                    break;
            }
            for (int i = best_cnt; i >= 1; i--) {
                if (uvsad < best_uvsad[i - 1]) {
                    best_uvsad[i] = best_uvsad[i - 1], best_uvmode[i] = best_uvmode[i - 1];
                    best_uvsad[i - 1] = uvsad, best_uvmode[i - 1] = mode;
                } else
                    break;
            }
            best_cnt = best_cnt < C1__UVMODE_SING_SRCH_CNT ? best_cnt + 1 : C1__UVMODE_SING_SRCH_CNT;
        } // sing mode search loop

        // traverse part of mode combinations
        for (int yidx = 0; yidx < C1__UVMODE_SING_SRCH_CNT; yidx++) {
            for (int uvidx = 0; uvidx + yidx < C1__UVMODE_SING_SRCH_CNT; uvidx++) {
                mi.mode_y = best_ymode[yidx], mi.mode_uv = best_uvmode[uvidx];
                c1enc_rdstat_t stat = {C1_RD_SAD_BIT};
                stat.sad = best_ysad[yidx] + best_uvsad[uvidx];
                c1enc_block_add_intra_cand(b, &mi, &stat);
            }
        } // y+uv mode search loop
    } // try different uv mode
    return 0;
}