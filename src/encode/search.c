#include "search.h"
#include "encode/types.h"
#include "encoder.h"
#include "predictor.h"
#include "types.h"

#include "util/log.h"
#include "util/pixbuf.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

/* table of contents
    - helpers:
        - matrix calc helper:       50
        - matrix evaluation helper: 110
        - rdstat ops:               150
        - mode inf ops:             180
        - block pred candidate ops: 190
    - info collection:
        - gather rdstat:            250
        - gather pred type:         270
        - gather residual:          280
    - prediction mode search:
        - intra:                    320
        - inter with step0:         420
        - inter:                    500
        - merge:                    580
        - divide:                   670
    - prediction mode search all-in-one:
        - option validate:          720
        - for super block:          740
*/

#define C1__UVMODE_SING_SRCH_CNT 4
#define C1__INTER_STEP_0_MAX 64
#define C1__FRAME_SAD_MAX_DEV_SCALE 8

// --- profile support ---

c1_profile_t c1enc_search_sb_prof = {0};
c1_profile_t c1enc_search_is_divide_prof = {0};
#define C1ENC_SEARCH_SB_ENTER() c1_profile_enter(&c1enc_search_sb_prof)
#define C1ENC_SEARCH_SB_EXIT() c1_profile_exit(&c1enc_search_sb_prof)
#define C1ENC_SEARCH_SB_STEP(step) c1_profile_step(&c1enc_search_sb_prof, step)
#define C1ENC_SEARCH_IS_DIVIDE_ENTER() c1_profile_enter(&c1enc_search_is_divide_prof)
#define C1ENC_SEARCH_IS_DIVIDE_EXIT() c1_profile_exit(&c1enc_search_is_divide_prof)
#define C1ENC_SEARCH_IS_DIVIDE_STEP(step) c1_profile_step(&c1enc_search_is_divide_prof, step)

static int c1enc__cmp(uint32_t a, uint32_t b) {
    return (a > b) - (a < b);
}
static int c1enc__cmpf(float a, float b) {
    return (a > b) - (a < b);
}

/** calculate sum of asbolute difference of a plane(p).
 suppose prediction result is stored in b->p[ci].diff buffer
 @param pix c3i16
*/
static uint32_t c1enc__calc_p_sad(const c1enc_block_t *b, const c1_pixbuf_t *pix, const c1pd_option_t *opt) {
    const int bw = c1_sz2wid(b->size), bh = c1_sz2hgt(b->size);
    const int by = (int)b->sb_y * 64 + b->yoff;
    const int bx = (int)b->sb_x * 64 + b->xoff;
    uint32_t res = 0;
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
/** prediction to difference.
 similar to @ref c1enc__calc_p_sad except core calculation.
*/
static void c1enc__calc_p_pred2dif(const c1enc_block_t *b, const c1_pixbuf_t *pix, const c1pd_option_t *opt) {
    const int bw = c1_sz2wid(b->size), bh = c1_sz2hgt(b->size);
    const int by = (int)b->sb_y * 64 + b->yoff;
    const int bx = (int)b->sb_x * 64 + b->xoff;
    c1_pixbuf_t ci_pix = c1_pixbuf_fromchnl(pix, opt->ci);
    for (int i = 0; i < bh; i++) {
        for (int j = 0; j < bw; j++) {
            int16_t *diff = &b->p[opt->ci].diff[i * bw + j];
            *diff = *c1_pixbuf_geti16c(&ci_pix, by + i, bx + j) - *diff;
        }
    }
    c1_pixbuf_clear(&ci_pix);
}

/** calculate sum of asbolute difference of a plane(p) with 2x2 sub sampling
 assumes pix hgt and wid are even, which always holds bec frame is 64 pix aligned.
 @param pix c3i16
*/
static uint32_t c1enc__calc_p_sad_subsamp(const c1enc_block_t *b, const c1_pixbuf_t *pix, const c1pd_option_t *opt) {
    const int bw = c1_sz2wid(b->size), bh = c1_sz2hgt(b->size);
    const int by = (int)b->sb_y * 64 + b->yoff;
    const int bx = (int)b->sb_x * 64 + b->xoff;
    uint32_t res = 0;
    c1_pixbuf_t ci_pix = c1_pixbuf_fromchnl(pix, opt->ci);
    for (int i = 0; i < bh - 1; i += 2) {
        for (int j = 0; j < bw - 1; j += 2) {
            res += c1_abs_dif_i16(                                    //
                *c1_pixbuf_geti16c(&ci_pix, by + i, bx + j)           //
                    + *c1_pixbuf_geti16c(&ci_pix, by + i, bx + j + 1) //
                    + *c1_pixbuf_geti16c(&ci_pix, by + i + 1, bx + j)
                    + *c1_pixbuf_geti16c(&ci_pix, by + i + 1, bx + j + 1), //
                b->p[opt->ci].diff[i * bw + j]                             //
                    + b->p[opt->ci].diff[i * bw + j + 1]                   //
                    + b->p[opt->ci].diff[(i + 1) * bw + j]                 //
                    + b->p[opt->ci].diff[(i + 1) * bw + j + 1]             //
            );
        }
    }
    c1_pixbuf_clear(&ci_pix);
    return res;
}

/** decide whether mode a is much better than mode b
 @return true if a is much better than b
*/
static int c1enc__is_mode_better(const c1enc_rdstat_t *a, const c1enc_rdstat_t *b, int a_cnt, int b_cnt, //
                                 const c1enc_search_option_t *opt) {
    if (b_cnt == 0)
        return 1;
    if (a_cnt == 0)
        return 0;
    if (a[0].mask & b[0].mask & C1_RD_SAD_BIT) {
        return a[0].sad < ((b[0].sad * opt->thre_mode_better_mult) >> opt->thre_mode_better_shift);
    } else {
        warning("no overlapping matrices");
        return 0;
    }
}
/** for merge check if sad of 4 parts are smooth
 cuurent strategy is to check if max sad and min sad diff at scale x
 @return bool if so
*/
static void c1enc__swap(int32_t *a, int32_t *b) {
    int32_t tmp = *a;
    *a = *b;
    *b = tmp;
}
static int c1enc__is_sad_smooth(int32_t a0, int32_t a1, int32_t a2, int32_t a3, //
                                const c1enc_search_option_t *opt) {
    int32_t min_0 = a0 < a1 ? a0 : a1;
    int32_t max_0 = a0 > a1 ? a0 : a1;
    int32_t min_1 = a2 < a3 ? a2 : a3;
    int32_t max_1 = a2 > a3 ? a2 : a3;
    int32_t min_ = min_0 < min_1 ? min_0 : min_1;
    int32_t max_ = max_0 > max_1 ? max_0 : max_1;
    // debug("min=%u, max=%u", min_, max_);
    return min_ + opt->thre_mat_is_dif_delta //
        >= ((max_ * opt->thre_mat_is_dif_mult) >> opt->thre_mat_is_dif_shift);
}

int c1enc_rdstat_cmp(const c1enc_rdstat_t *a, const c1enc_rdstat_t *b) {
    uint8_t mask = a->mask & b->mask;
    if (mask & C1_RD_FIT_BIT) {
        return c1enc__cmpf(a->fitness, b->fitness);
    } else if (mask & C1_RD_DIS_BIT) {
        return -c1enc__cmp(a->d, b->d);
    } else if (mask & C1_RD_RATE_BIT) {
        return -c1enc__cmp(a->r, b->r);
    } else if (mask & C1_RD_SSE_BIT) {
        return -c1enc__cmpf(a->sse, b->sse);
    } else if (mask & C1_RD_SAD_BIT) {
        return -c1enc__cmp(a->sad, b->sad);
    }
    fatal2("invalid mask %02x & %02x", a->mask, b->mask);
}

c1enc_rdstat_t c1enc_rdstat_merge(const c1enc_rdstat_t *a, const c1enc_rdstat_t *b) {
    uint8_t mask = a->mask & b->mask;
    c1enc_rdstat_t res = {.mask = mask};
    if (mask & C1_RD_RATE_BIT)
        res.r = a->r + b->r;
    if (mask & C1_RD_DIS_BIT)
        res.d = a->d + b->d;
    if (mask & C1_RD_SSE_BIT)
        res.sse = a->sse + b->sse;
    if (mask & C1_RD_SAD_BIT)
        res.sad = a->sad + b->sad;
    if (mask & C1_RD_FIT_BIT)
        res.fitness = a->fitness + b->fitness;
    return res;
}

int c1enc_mi_intra_eq(const c1enc_mi_intra_t *a, const c1enc_mi_intra_t *b) {
    if (a->mode_y != b->mode_y || (a->use_cfl != b->use_cfl))
        return 0;
    if (a->use_cfl) {
        // return a->cfl_alpha_u == b->cfl_alpha_u && a->cfl_alpha_v == b->cfl_alpha_v;
        // do not consider the difference of alphas
        return 1;
    } else {
        return a->mode_uv == b->mode_uv;
    }
}
int c1enc_mi_inter_eq(const c1enc_mi_inter_t *a, const c1enc_mi_inter_t *b) {
    if (a->mode != b->mode || a->ref_frame != b->ref_frame)
        return 0;
    return a->mv.y == b->mv.y && a->mv.x == b->mv.x;
}

int c1enc_block_has_intra_cand(const c1enc_block_t *b, const c1enc_mi_intra_t *mi) {
    for (int i = 0; i < b->intra_cand_cnt; i++) {
        if (c1enc_mi_intra_eq(mi, b->intra_cands + i))
            return 1;
    }
    return 0;
}
int c1enc_block_has_inter_cand(const c1enc_block_t *b, const c1enc_mi_inter_t *mi) {
    for (int i = 0; i < b->inter_cand_cnt; i++) {
        if (c1enc_mi_inter_eq(mi, b->inter_cands + i))
            return 1;
    }
    return 0;
}
int c1enc__block_cached_pred_idx(const c1enc_block_t *b, const c1pd_option_t *opt) {
    for (int i = 0; i < C1_ENC_CACHED_PRED_STAT_CNT && i < b->cached_pred_stat_cnt; i++) {
        if (c1pd_opt_eq(b->cached_preds + i, opt))
            return i;
    }
    return -1;
}
int c1enc__block_cache_pred(c1enc_block_t *b, const c1pd_option_t *opt, uint32_t stat) {
    if (c1enc__block_cached_pred_idx(b, opt) >= 0)
        return 0;
    if (b->cached_pred_stat_cnt >= C1_ENC_CACHED_PRED_STAT_CNT)
        return 0;
    b->cached_preds[b->cached_pred_stat_cnt] = *opt;
    b->cached_pred_stats[b->cached_pred_stat_cnt] = stat;
    b->cached_pred_stat_cnt++;
    return 0;
}
uint32_t c1enc__cached_pred_sad(c1enc_block_t *b, const c1pd_option_t *opt, const c1_pixbuf_t *pix, int8_t *cfl) {
    int idx = c1enc__block_cached_pred_idx(b, opt);
    uint32_t res;
    if (idx < 0) {
        c1pd_predict(b, b->p[opt->ci].diff, pix, opt, cfl);
        res = c1enc__calc_p_sad(b, pix, opt);
        c1enc__block_cache_pred(b, opt, res);
    } else {
        res = b->cached_pred_stats[idx];
    }
    return res;
}

int c1enc_block_add_intra_cand(c1enc_block_t *b, const c1enc_mi_intra_t *mi, const c1enc_rdstat_t *stat) {
    c1enc_rdstat_t *stats = b->intra_cand_stats;
    c1enc_mi_intra_t *cands = b->intra_cands;
    const uint8_t nbcand = b->intra_cand_cnt;

    if (c1enc_block_has_intra_cand(b, mi))
        return 0;

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

    if (c1enc_block_has_inter_cand(b, mi)) {
        return 0;
    }

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
    b->inter_cand_cnt = (uint8_t)(nbcand < C1_ENC_INTER_CAND_CNT ? nbcand + 1 : C1_ENC_INTER_CAND_CNT);
    return 0;
}

// --- info collection ---

int c1enc_part_gather_rdstat(c1enc_partition_t *p) {
    if (p->is_partition) {
        p->stats = (c1enc_rdstat_t){C1_RD_SAD_BIT};
        for (int i = 0; i < 4; i++) {
            c1enc_part_gather_rdstat(p->parts[i]);
            p->stats = c1enc_rdstat_merge(&p->stats, &p->parts[i]->stats);
        }
        // debug("merged sad: %u", p->stats.sad);
    } else {
        if (c1enc_block_gather_pred_type(p->b) < 0)
            return -1;
        p->stats = p->b->pred_type == C1_PRED_INTER ? p->b->inter_cand_stats[0] : p->b->intra_cand_stats[0];
        // debug("current sad: %u", p->stats.sad);
    }
    return 0;
}

int c1enc_block_gather_pred_type(c1enc_block_t *b) {
    if (b->intra_cand_cnt == 0 && b->inter_cand_cnt == 0) {
        warning("block do not have dicision candidates");
        return -1;
    }
    b->pred_type_determined = 1;
    if (b->intra_cand_cnt == 0) {
        b->pred_type = C1_PRED_INTER;
    } else if (b->inter_cand_cnt == 0) {
        b->pred_type = C1_PRED_INTRA;
    } else {
        b->pred_type = c1enc_rdstat_cmp(b->intra_cand_stats, b->inter_cand_stats) > 0 ? C1_PRED_INTRA : C1_PRED_INTER;
    }
    return 0;
}

int c1enc_block_gather_residual(c1enc_block_t *b, const c1_pixbuf_t *pix, const c1enc_ctx_t *ctx, uint8_t ref_id) {
    assert_fatal(b->pred_type_determined);
    c1pd_option_t pred_opt = {0};

    if (b->pred_type == C1_PRED_INTRA) {
        const c1enc_mi_intra_t *mi = b->intra_cands;
        if (mi->use_cfl) {
            pred_opt.mode = mi->mode_y;
            pred_opt.use_cfl = 1;
            pred_opt.has_cfl_alpha = 1;
            int8_t cfl_alphas[3] = {0, mi->cfl_alpha_u, mi->cfl_alpha_v};
            for (uint8_t ci = 0; ci < 3; ci++) {
                pred_opt.ci = ci;
                c1pd_predict(b, b->p[ci].diff, pix, &pred_opt, cfl_alphas + ci);
                c1enc__calc_p_pred2dif(b, pix, &pred_opt);
            }
        } else {
            const C1_PRED_MODE modes[3] = {mi->mode_y, mi->mode_uv, mi->mode_uv};
            for (uint8_t ci = 0; ci < 3; ci++) {
                pred_opt.mode = modes[ci];
                pred_opt.ci = ci;
                c1pd_predict(b, b->p[ci].diff, pix, &pred_opt, NULL);
                c1enc__calc_p_pred2dif(b, pix, &pred_opt);
            }
        }
    } else {
        const c1_pixbuf_t *ref_pix = c1enc_ctx_frame_at(ctx, pix, ref_id);
        const c1enc_mi_inter_t *mi = b->inter_cands;
        pred_opt.mode = C1_PRED_MVNEW;
        pred_opt.mv = mi->mv;
        for (uint8_t ci = 0; ci < 3; ci++) {
            pred_opt.ci = ci;
            c1pd_predict(b, b->p[ci].diff, ref_pix, &pred_opt, NULL);
            c1enc__calc_p_pred2dif(b, pix, &pred_opt);
        }
    }
    return 0;
}

// --- pred mode search ---

static uint32_t c1enc__intra_not_adj_modes[C1_PRED_PAETH + 1 - C1_PRED_DC] = {
    0,                                                                             // dc
    0,                                                                             // h
    0,                                                                             // v
    1 << C1_PRED_D135 | 1 << C1_PRED_D113 | 1 << C1_PRED_D157,                     // d45
    1 << C1_PRED_D45 | 1 << C1_PRED_D67 | 1 << C1_PRED_D203,                       // 135
    1 << C1_PRED_D135 | 1 << C1_PRED_D113 | 1 << C1_PRED_D157 | 1 << C1_PRED_D203, // d67
    1 << C1_PRED_D45 | 1 << C1_PRED_D67 | 1 << C1_PRED_D157 | 1 << C1_PRED_D203,   // d113
    1 << C1_PRED_D45 | 1 << C1_PRED_D67 | 1 << C1_PRED_D113,                       // d157
    1 << C1_PRED_D67 | 1 << C1_PRED_D135 | 1 << C1_PRED_D113,                      // d203
};

int c1enc_search_intra_b(c1enc_block_t *b, const c1_pixbuf_t *pix, const c1enc_search_option_t *opt) {
    static const int sad_sig[3] = {0, 1, 1};
    const int bh = c1_sz2hgt(b->size), bw = c1_sz2wid(b->size);

    // dir mode in z1/z3 trimmed for the edges may not be avail for recon/decode
    // set to 1 means skip
    uint32_t mode_skip_mask = 0, mode_adj_mask = 0;
    if (b->yoff > 0) {
        mode_skip_mask |= (1 << C1_PRED_D45) | (1 << C1_PRED_D67);
    }
    if (b->xoff > 0) {
        mode_skip_mask |= (1 << C1_PRED_D203);
    }

    // (1) default search
    // // only 1 mode dimension is searched
    c1pd_option_t pred_opt = {0};
    pred_opt.use_cfl = 0; // no cfl first
    c1enc_mi_intra_t mi;
    mi.use_cfl = 0;

    for (C1_PRED_MODE mode = C1_PRED_DC; mode < opt->intra_rng_max; mode++) {
        if ((mode_skip_mask | mode_adj_mask) & (1 << mode))
            continue;
        pred_opt.mode = mi.mode_y = mi.mode_uv = mode;
        c1enc_rdstat_t stat = {.mask = C1_RD_SAD_BIT};
        if (opt->intra_only_y) {
            pred_opt.ci = 0;
            stat.sad = c1enc__cached_pred_sad(b, &pred_opt, pix, NULL) * 2;
        } else {
            for (uint8_t ci = 0; ci < 3; ci++) {
                pred_opt.ci = ci;
                int idx = c1enc__block_cached_pred_idx(b, &pred_opt);
                stat.sad += c1enc__cached_pred_sad(b, &pred_opt, pix, NULL) >> sad_sig[ci];
            }
        }
        c1enc_block_add_intra_cand(b, &mi, &stat);

        // update mode skip mask according to the best mode
        if (b->intra_cand_cnt > 0) {
            mode_adj_mask = c1enc__intra_not_adj_modes[b->intra_cands[0].mode_y - C1_PRED_DC];
            if (b->intra_cand_stats[0].sad <= opt->thre_intra_efficient_sad * bh * bw * 3) {
                // debug("early exit");
                goto dtor;
            }
        }

    } // mode search loop

    // (2) then search cfl
    if (opt->intra_try_cfl) {

        pred_opt.use_cfl = mi.use_cfl = 1;

        for (C1_PRED_MODE mode = C1_PRED_DC; mode < opt->intra_rng_max; mode++) {
            if (mode_skip_mask & (1 << mode))
                continue;
            pred_opt.mode = mi.mode_y = mode;
            c1enc_rdstat_t stat = {.mask = C1_RD_SAD_BIT};
            int8_t *const cfl_outputs[3] = {NULL, &mi.cfl_alpha_u, &mi.cfl_alpha_v};
            for (uint8_t ci = 0; ci < 3; ci++) {
                pred_opt.ci = ci;
                c1pd_predict(b, b->p[ci].diff, pix, &pred_opt, cfl_outputs[ci]);
                stat.sad += c1enc__calc_p_sad(b, pix, &pred_opt) >> sad_sig[ci];
                // todo: at recon, this refers to undefined pixels.
                // have no time to fix
            }
            c1enc_block_add_intra_cand(b, &mi, &stat);
        } // mode search loop

        pred_opt.use_cfl = mi.use_cfl = 0;
    }

    // (3) search different uv mode
    if (opt->intra_try_uv && !pred_opt.use_cfl) {
        assert_fatal(b->intra_cand_cnt > 0);
        const C1_PRED_MODE mode_y = b->intra_cands[0].mode_y;
        mi.mode_y = mode_y;

        for (C1_PRED_MODE mode = C1_PRED_DC; mode < opt->intra_rng_max; mode++) {
            if ((mode_skip_mask | mode_adj_mask) & (1 << mode))
                continue;
            c1enc_rdstat_t stat = {.mask = C1_RD_SAD_BIT};
            const C1_PRED_MODE modes[3] = {mode_y, mode, mode};
            mi.mode_uv = mode;
            for (uint8_t ci = 0; ci < 3; ci++) {
                pred_opt.mode = modes[ci];
                pred_opt.ci = ci;
                stat.sad += c1enc__cached_pred_sad(b, &pred_opt, pix, NULL) >> sad_sig[ci];
            }
            c1enc_block_add_intra_cand(b, &mi, &stat);
        }

    } // try different uv mode

dtor:
    // currently nothing to destruct;
    return 0;
}
/** search inter mode per block per step in a diamond search pattern
 @param step_0 the biggest step, is power of 2
 @param step_cur the current diamond search initial step, power of 2
 @param matrices persistent assessment of fitness of each mvs relative to y0,x0.
 of logical size (step_0*4+1)^2, with y0,x0 at index (step_0,step_0). matrices is measured by sad but
 is not true sad. it may add a distance smoothing and may be sub sampled sad.
 matrices are initialized with 0xff bytes, which is UINT32_MAX
*/
static int c1enc_search_inter_b_step(c1enc_block_t *b, const c1_pixbuf_t *pix, const c1enc_ctx_t *ctx, //
                                     const c1enc_search_option_t *opt,                                 //
                                     uint8_t step_0, uint8_t step_cur, uint32_t *matrices,             //
                                     const uint8_t y0, const uint8_t x0,                               //
                                     uint32_t *best_m, c1enc_mv_t *best_mv) {
    // short alias of vars
    uint8_t step = step_cur;
    const c1_pixbuf_t *ref_pix = c1enc_ctx_frame_at(ctx, pix, opt->inter_ref_idx);

    // init search info
    c1enc_mv_t c = {y0, x0};                          // mv search center
    c1enc_mv_t d1 = {step, 0};                        // d1 d2 the 2 directions to search, 4 points
    c1pd_option_t pred_opt = {.mode = C1_PRED_MVNEW}; // common predict option

    while (1) {
        c1enc_mv_t d2 = {0 - d1.x, d1.y}; // d2 is always ortho to d1. this step may not be opt by compiler?
        const c1enc_mv_t mvs[4] = {
            // 4 motion vectors to search in a diamond, relative to 0,0
            {c.y + d1.y, c.x + d1.x},
            {c.y - d1.y, c.x - d1.x},
            {c.y + d2.y, c.x + d2.x},
            {c.y - d2.y, c.x - d2.x},
        };
        uint16_t idxs[4];       // candidate indices in "matrices"
        uint8_t has_new_mv = 0; // tells that if all mvs are searched twice, no need to further

        for (int cand = 0; cand < 4; cand++) {
            // debug("case[%u] %d,%d", cand, mvs[cand].y, mvs[cand].x);
            // deduce corresponding index in matrices
            const uint16_t m_i = mvs[cand].y - y0 + step_0 * 2, m_j = mvs[cand].x - x0 + step_0 * 2;
            idxs[cand] = m_i * (step_0 * 4 + 1) + m_j;
            assert(idxs[cand] < (step_0 * 4 + 1) * (step_0 * 4 + 1));

            if (matrices[idxs[cand]] != 0xffffffff) { // searched twice, skip
                // debug("skip");
                continue;
            }

            // cartesian distance to 0,0 used for subsampling and smoothing
            const uint8_t dist = (uint8_t)c1_clamp16(c1_abs_i16(mvs[cand].y) + c1_abs_i16(mvs[cand].x), 0, 255);

            // prepare prediction
            has_new_mv = 1;
            matrices[idxs[cand]] = 0;
            pred_opt.mv = mvs[cand]; // out-of-bound is handled by c1pd_predict

            for (uint8_t ci = 0; ci < 3; ci++) {
                pred_opt.ci = ci;
                c1pd_predict(b, b->p[ci].diff, ref_pix, &pred_opt, NULL);
                // cumulate sad of yuv planes. considering sub sampling.
                if (opt->inter_sad_subsamp_mask & dist) {
                    matrices[idxs[cand]] += c1enc__calc_p_sad(b, pix, &pred_opt);
                } else {
                    matrices[idxs[cand]] += c1enc__calc_p_sad_subsamp(b, pix, &pred_opt);
                }
                if (opt->inter_only_y) {
                    matrices[idxs[cand]] *= 3;
                    break;
                }
            }
            // apply distance smoothing
            matrices[idxs[cand]] += opt->inter_smooth_lambda * dist / 4;
            // debug("got %u", matrices[idxs[cand]]);
            if (matrices[idxs[cand]] < *best_m) {
                *best_m = matrices[idxs[cand]];
                *best_mv = mvs[cand];
            }
        }
        if (!has_new_mv)
            break;
        // determine new search diamond
        // try flip d1 and d2, make +d1 +d2 best direction (less abs diff)
        if (matrices[idxs[1]] <= matrices[idxs[0]]) {
            d1 = (c1enc_mv_t){0 - d1.y, 0 - d1.x};
        }
        if (matrices[idxs[3]] <= matrices[idxs[2]]) {
            d2 = (c1enc_mv_t){0 - d2.y, 0 - d2.x};
        }
        // update search inf
        c = (c1enc_mv_t){c.y + (d1.y + d2.y) / 2, c.x + (d1.x + d2.x) / 2};
        d1 = (c1enc_mv_t){(d1.y - d2.y) / 2, (d1.x - d2.x) / 2};
        d2 = (c1enc_mv_t){(d1.y + d2.y) / 2, (d1.x + d2.x) / 2};
        if (step <= 0)
            break;
        step /= 2;
    }
    return 0;
}

int c1enc_search_inter_b(c1enc_block_t *b, const c1_pixbuf_t *pix, const c1enc_ctx_t *ctx, //
                         const c1enc_search_option_t *opt) {
    // alias
    const uint8_t nbcand = opt->inter_newcand_cnt;
    const c1_pixbuf_t *ref_pix = c1enc_ctx_frame_at(ctx, pix, opt->inter_ref_idx);
    const int bh = c1_sz2hgt(b->size), bw = c1_sz2wid(b->size);
    // attrs about ref mv
    c1enc_mv_t ref_mv;
    if (b->has_ref_mv) {
        ref_mv = b->ref_mv;
    } else {
        ref_mv = opt->inter_ref_idx == 0
                   ? (c1enc_mv_t){0}
                   : c1enc_ref_at((c1enc_ctx_t *)ctx, b->sb_y, b->sb_x, b->yoff, b->xoff, opt->inter_ref_idx)->mv;
        b->ref_mv = ref_mv;
    }
    const uint8_t y0 = (uint8_t)ref_mv.y, x0 = (uint8_t)ref_mv.x;

    // get step_0
    uint8_t step_0 = C1__INTER_STEP_0_MAX;
    while (!(opt->inter_init_steps_mask & step_0) && step_0)
        step_0 /= 2;

    // prepare search matrix record
    uint32_t matrix_nb = (step_0 * 4 + 1) * (step_0 * 4 + 1);

    // buffers:
    // (1) matrices contain measures to mvs, inpterpreted as [mvy][mvx] size (step_0*4+1)^2 center [step_0*2][step_0*2]
    // -> y0,x0; (2) mvs sorted candidate mvs; (3) ms sorted measures
    uint32_t *matrices = malloc(matrix_nb * sizeof(uint32_t) + // all-in-one alloc
                                (nbcand + 1) * sizeof(c1enc_mv_t) + (nbcand + 1) * sizeof(uint32_t));
    assert_fatal(matrices);

    memset(matrices, 0xff, matrix_nb * sizeof(int32_t)); // set to UINT32_MAX

    // (1) conduct diamon search on every init steps indicated by mask

    uint8_t step_cur = 0;
    uint32_t best_m = UINT32_MAX;
    c1enc_mv_t best_mv = {0};
    while (step_cur <= step_0) {
        if ((opt->inter_init_steps_mask & step_cur) || step_cur == 0) {
            c1enc_search_inter_b_step(b, pix, ctx, opt, step_0, step_cur, matrices, y0, x0, &best_m, &best_mv);
            if (best_m <= (uint32_t)opt->thre_inter_efficient_sad * bh * bw * 3) {
                // debug("early skip: best_m=%u",best_m);
                break;
            }
            if (best_m >= opt->thre_skip_inter_sad * bh * bw * 3) {
                break;
            }
        }
        step_cur = step_cur * 2 + (step_cur == 0);
    }

    // (2) add certain number of candidates. search in matrices first, ordered by these matrices

    c1enc_mv_t *mvs = (void *)(matrices + matrix_nb);
    uint32_t *ms = (void *)(mvs + nbcand + 1);

    if (nbcand <= 1 || best_m >= opt->thre_skip_inter_sad * bh * bw * 3) {
        ms[0] = best_m, mvs[0] = best_mv;
    } else {
        // iterate each offset in matrices. the number of candidate is restricted by nbcand
        memset(ms, 0xff, (nbcand + 1) * sizeof(uint32_t)); // set to UINT32_MAX
        for (int16_t y = -step_0 * 2 + y0; y <= step_0 * 2 + y0; y++) {
            for (int16_t x = -step_0 * 2 + x0; x <= step_0 * 2 + x0; x++) {
                const uint16_t m_i = y - y0 + step_0 * 2, m_j = x - x0 + step_0 * 2;
                const uint16_t m_idx = m_i * (step_0 * 4 + 1) + m_j;
                // bubble-sort
                for (int i = nbcand; i >= 1; i--) {
                    if (matrices[m_idx] < ms[i - 1]) {
                        ms[i] = ms[i - 1], mvs[i] = mvs[i - 1];
                        ms[i - 1] = matrices[m_idx], mvs[i - 1] = (c1enc_mv_t){y, x};
                    } else
                        break;
                } // sort
            } // x iter
        } // y iter
    }

    // (3) traverse candidates and add to block
    for (int i = 0; i < nbcand; i++) {
        if (ms[i] != UINT32_MAX) {
            // gather SAD in 2 cases: case subsampled, recalculate, otherwise minus the smooth factor
            c1enc_rdstat_t stat = {.mask = C1_RD_SAD_BIT};
            const uint8_t dist = (uint8_t)c1_clamp16(c1_abs_i16(mvs[i].y) + c1_abs_i16(mvs[i].x), 0, 255);
            if (opt->inter_sad_subsamp_mask & dist) {
                // (3.1) recalc sad
                c1pd_option_t pred_opt = {.mode = C1_PRED_MVNEW, .mv = mvs[i]};
                stat.sad = 0;
                for (uint8_t ci = 0; ci < 3; ci++) {
                    c1pd_predict(b, b->p[ci].diff, ref_pix, &pred_opt, NULL);
                    stat.sad += c1enc__calc_p_sad(b, pix, &pred_opt);
                    if (opt->inter_only_y) {
                        stat.sad *= 3;
                        break;
                    }
                }
            } else {
                // (3.2) inverse sad from matrix
                stat.sad = ms[i] - opt->inter_smooth_lambda * dist / 4;
            }
            // gather mode info
            c1enc_mi_inter_t mi = {.mode = C1_PRED_MVNEW, .ref_frame = opt->inter_ref_idx, .mv = mvs[i]};
            // add cand to block
            // debug("add cand %u, %u, %u", stat.sad, mi.mv.y, mi.mv.x);
            c1enc_block_add_inter_cand(b, &mi, &stat);
        } else
            break; // ms are sorted
    }

    return 0;
}

// --- merge and divide search ---

int c1enc_search_merge(c1enc_partition_t *p, const c1_pixbuf_t *pix, const c1enc_ctx_t *ctx, //
                       const c1enc_search_option_t *opt) {
    // criterions:
    // (1) sub pred type not worse
    // (2) sub SADs smooth
    // (3) sum of sub SADs within some fraction of the whole frame
    // (4) results in better SAD
    if (!p->is_partition)
        return 0;
    for (int i = 0; i < 4; i++) {
        if (p->parts[i]->is_partition) {
            c1enc_search_merge(p->parts[i], pix, ctx, opt);
        }
    }
    for (int i = 0; i < 4; i++) {
        if (p->parts[i]->is_partition) {
            // debug("early exit");
            return 0;
        }
    }
    uint8_t try_inter = opt->try_inter, try_intra = opt->try_intra; // search switch

    // (1) collect sub SADs and filter available pred mode
    int32_t m_intra[4], m_inter[4]; // matrices, currently abs dif.
    // sum of part stats to cmp with merge stat
    c1enc_rdstat_t part_stat_intra = {C1_RD_SAD_BIT}, part_stat_inter = {C1_RD_SAD_BIT};
    for (int i = 0; i < 4; i++) {
        const c1enc_block_t *b = p->parts[i]->b;
        try_inter = try_inter
                 && !c1enc__is_mode_better(b->intra_cand_stats, b->inter_cand_stats, b->intra_cand_cnt,
                                           b->inter_cand_cnt, opt);
        try_intra = try_intra
                 && !c1enc__is_mode_better(b->inter_cand_stats, b->intra_cand_stats, b->inter_cand_cnt,
                                           b->intra_cand_cnt, opt);
        if (try_intra) {
            // choose the first cand to merge. if no cand, should ensure try_intra is false
            part_stat_intra = c1enc_rdstat_merge(&part_stat_intra, b->intra_cand_stats);
            m_intra[i] = b->intra_cand_stats[0].sad;
        }
        if (try_inter) {
            part_stat_inter = c1enc_rdstat_merge(&part_stat_inter, b->inter_cand_stats);
            m_inter[i] = b->inter_cand_stats[0].sad;
        }
    }
    assert_fatal(part_stat_intra.mask & C1_RD_SAD_BIT);
    assert_fatal(part_stat_inter.mask & C1_RD_SAD_BIT);

    // (2-3)
    if (try_intra
        && (!c1enc__is_sad_smooth(m_intra[0], m_intra[1], m_intra[2], m_intra[3], opt)
            || part_stat_intra.sad <= opt->thre_sad_max_b)) {
        try_intra = 0;
        // debug("no try intra merge for not smooth %d,%d", p->y, p->x);
    }
    if (try_inter
        && (!c1enc__is_sad_smooth(m_inter[0], m_inter[1], m_inter[2], m_inter[3], opt)
            || part_stat_inter.sad <= opt->thre_sad_max_b)) {
        try_inter = 0;
    }
    if (!try_intra && !try_inter)
        return 0;

    // (4) until now do we alloc a block for search
    assert_fatal(!p->b && (p->b = malloc(sizeof(c1enc_block_t))));
    *p->b = (c1enc_block_t){0};
    c1enc_block_update(p->b, p->sb, p->size, p->y, p->x, p->sb_y, p->sb_x, p->buf_offs);
    c1enc_search_option_t limited_opt = *opt;
    limited_opt.intra_rng_max = C1_PRED_DC + 1;
    limited_opt.inter_init_steps_mask = 0;
    if (try_intra) {
        // debug("try intra merge-search %d,%d", p->y, p->x);
        c1enc_search_intra_b(p->b, pix, &limited_opt);
        assert_fatal(p->b->intra_cand_cnt > 0);
    }
    if (try_inter) {
        c1enc_search_inter_b(p->b, pix, ctx, &limited_opt);
        assert_fatal(p->b->inter_cand_cnt > 0);
    }
    int can_merge
        = (try_intra && p->b->intra_cand_cnt > 0
           && part_stat_intra.sad
                  > ((p->b->intra_cand_stats[0].sad * opt->thre_mat_is_dif_mult) >> opt->thre_mat_is_dif_shift))
       || (try_inter && p->b->inter_cand_cnt > 0
           && part_stat_inter.sad
                  > ((p->b->inter_cand_stats[0].sad * opt->thre_mat_is_dif_mult) >> opt->thre_mat_is_dif_shift));
    if (can_merge) {
        // debug("can merge");
        // merge. release partitions
        p->is_partition = 0;
        for (int i = 0; i < 4; i++) {
            c1enc_partition_clear(p->parts[i]);
            c1_mpool_dealloc(&c1enc_part_pool, p->parts[i]);
            p->parts[i] = NULL;
        }
    } else {
        // do not merge, remain as partition and release tmp block
        c1enc_block_clear(p->b);
        free(p->b);
        p->b = NULL;
    }
    return 0;
}
int c1enc_search_divide(c1enc_partition_t *p, const c1_pixbuf_t *pix, const c1enc_ctx_t *ctx, //
                        const c1enc_search_option_t *opt) {
    if (p->is_partition) {
        // debug("skip");
        for (int i = 0; i < 4; i++) {
            c1enc_search_divide(p->parts[i], pix, ctx, opt);
        }
        return 0;
    }
    // ensure size is a square?
    if (p->size <= C1_SZ_8_8 || p->size > C1_SZ_64_64)
        return 0;
    const c1enc_block_t *b = p->b;
    if (!b->intra_cand_cnt && !b->inter_cand_cnt) {
        warning("no candidates");
        return -1;
    }

    // gather matrix
    uint32_t m_intra = UINT32_MAX, m_inter = UINT32_MAX;
    if (b->intra_cand_cnt > 0) {
        assert_fatal(b->intra_cand_stats[0].mask & C1_RD_SAD_BIT);
        m_intra = b->intra_cand_stats[0].sad;
    }
    if (b->inter_cand_cnt > 0) {
        assert_fatal(b->inter_cand_stats[0].mask & C1_RD_SAD_BIT);
        m_inter = b->inter_cand_stats[0].sad;
    }

    // divide if SAD exceeds
    if (m_intra > opt->thre_sad_max_b && m_inter > opt->thre_sad_max_b) {
        C1ENC_SEARCH_IS_DIVIDE_ENTER();
        // (1) update indicator
        p->is_partition = 1;
        // (2) dealloc block
        c1enc_block_clear(p->b);
        free(p->b);
        p->b = NULL;

        // (3) alloc parts
        C1_2D_SZ new_sz = p->size - 1;
        uint8_t new_hgt = c1_sz2hgt(new_sz);
        uint8_t new_wid = c1_sz2wid(new_sz);
        const uint8_t offs[4][2] = {
            {0, 0},
            {0, new_wid},
            {new_hgt, 0},
            {new_hgt, new_wid},
        };
        for (uint8_t i = 0; i < 4; i++) {
            assert_fatal(!p->parts[i] && (p->parts[i] = c1_mpool_alloc(&c1enc_part_pool)));
            *p->parts[i] = (c1enc_partition_t){0};
            c1enc_partition_init(p->parts[i], p->sb, new_sz, new_sz,                     //
                                 p->y + offs[i][0], p->x + offs[i][1], p->sb_y, p->sb_x, //
                                 p->buf_offs + i * new_hgt * new_wid * 3);

            // conduct search and further divide
            c1enc_search_p(p->parts[i], pix, ctx, opt);
            c1enc_search_divide(p->parts[i], pix, ctx, opt);
        }
        C1ENC_SEARCH_IS_DIVIDE_EXIT();
    }
    return 0;
}

void c1enc_search_option_validate(const c1enc_search_option_t *opt) {
    // validate options
    assert_fatal(opt->try_intra || opt->try_inter);
    if (opt->try_inter) {
        assert_fatal(opt->inter_init_steps_mask != 0);
        assert_fatal(opt->inter_newcand_cnt > 0);
    }
    if (opt->try_intra) {
        assert_fatal(!opt->intra_try_cfl || !opt->intra_try_uv);
        assert_fatal(opt->intra_rng_max > C1_PRED_DC && opt->intra_rng_max <= C1_PRED_PAETH + 1);
    }
    assert_fatal(opt->thre_mode_better_shift < 8);
    assert_fatal(opt->thre_mode_better_mult >= (1 << opt->thre_mode_better_shift));
    assert_fatal(opt->thre_mat_is_dif_shift < 8);
    assert_fatal(opt->thre_mat_is_dif_mult <= (1 << opt->thre_mat_is_dif_shift));
}

/** decide a sad max threshold for merge and divide
 @param a block desired sad max, eq to average sad given default block size(currently 16x16) at super block level
 @param b frame desired sad max, eq to average sad given default block size at frame level. it is a prediction from last
 frame or a default.
 @details
 - if a is much smaller, it indicates the sb has less details, thus should not occupy much bits,restricted by upper
 bound a*scale.
 - if a is much bigger, it indicates more details, but should not exceed possible rate limit, restricted by lower bound
 a/scale.
 - within range, b plays dominant role in bit allocation.
 the deviation scale is defined by local macro C1__FRAME_SAD_MAX_DEV_SCALE, which should be a power of 2.
*/
static uint32_t c1enc__search_decide_sad_max(uint32_t a, uint32_t b) {
    if (b < a / C1__FRAME_SAD_MAX_DEV_SCALE) {
        return a / C1__FRAME_SAD_MAX_DEV_SCALE;
    }
    if (b / C1__FRAME_SAD_MAX_DEV_SCALE > a) {
        // avoid overflow
        return UINT32_MAX / C1__FRAME_SAD_MAX_DEV_SCALE > a ? a * C1__FRAME_SAD_MAX_DEV_SCALE : UINT32_MAX;
    }
    return b;
}

int c1enc_search_sb(c1enc_super_block_t *sb, const c1_pixbuf_t *pix, const c1enc_ctx_t *ctx, //
                    const c1enc_search_option_t *opt) {
    c1enc_search_option_validate(opt);
    c1enc_search_option_t limited_opt = *opt;

    limited_opt.inter_init_steps_mask = (16 | 4 | 1) & opt->inter_init_steps_mask;
    // limited_opt.inter_smooth_lambda = 0; // unchanged
    limited_opt.inter_only_y = 1;

    limited_opt.intra_try_uv = 0;
    limited_opt.intra_try_cfl = 0;
    limited_opt.intra_rng_max = C1_PRED_V + 1;
    limited_opt.intra_only_y = 1;
    // limited_opt.thre_sad_max_b = 1 << 12;

    C1ENC_SEARCH_SB_ENTER();
    c1enc_search_p(sb->root, pix, ctx, &limited_opt);
    C1ENC_SEARCH_SB_STEP(1);
    c1enc_part_gather_rdstat(sb->root);

    C1ENC_SEARCH_SB_STEP(2);
    // make SAD threshold adaptive. (4*4) is averaging 64x64->64x64 currently
    limited_opt.thre_sad_max_b = c1enc__search_decide_sad_max( //
        sb->root->stats.sad / (1 * 1), limited_opt.thre_sad_max_b);

    // C1ENC_SEARCH_SB_STEP(3);
    c1enc_search_merge(sb->root, pix, ctx, &limited_opt);
    C1ENC_SEARCH_SB_STEP(3);
    c1enc_search_divide(sb->root, pix, ctx, &limited_opt);
    C1ENC_SEARCH_SB_STEP(4);
    c1enc_search_p(sb->root, pix, ctx, opt);
    C1ENC_SEARCH_SB_EXIT();
    return 0;
}
