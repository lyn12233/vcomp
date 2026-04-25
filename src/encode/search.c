#include "search.h"
#include "encoder.h"
#include "predictor.h"
#include "types.h"

#include "util/log.h"
#include "util/pixbuf.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define C1__UVMODE_SING_SRCH_CNT 4
#define C1__WORSE_MODE_ACCEPTABLE_SCALE 3 / 2 // no parenthesis. mult then div by 2**n
#define C1__MERGE_THRE_SAD_DIF_SCALE 3 / 4

static int c1enc__cmp(uint32_t a, uint32_t b) {
    return (a > b) - (a < b);
}
static int c1enc__cmpf(float a, float b) {
    return (a > b) - (a < b);
}

/** calculate sum of asbolute difference of a plane(p)
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
            res += (uint16_t)c1_abs_dif_i16(                 //
                *c1_pixbuf_geti16c(&ci_pix, by + i, bx + j), //
                b->p[opt->ci].diff[i * bw + j]               //
            );
        }
    }
    c1_pixbuf_clear(&ci_pix);
    return res;
}

/** decide whether mode a is much better than mode b
 @return true if a is much better than b
*/
static int c1enc__is_mode_better(const c1enc_rdstat_t *a, const c1enc_rdstat_t *b, int a_cnt, int b_cnt) {
    if (b_cnt == 0)
        return 1;
    if (a_cnt == 0)
        return 0;
    if (a[0].mask & b[0].mask & C1_RD_SAD_BIT) {
        return a[0].sad > b[0].sad * C1__WORSE_MODE_ACCEPTABLE_SCALE;
    } else {
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
static int c1enc__is_sad_smooth(int32_t a0, int32_t a1, int32_t a2, int32_t a3) {
    int32_t min_0 = a0 < a1 ? a0 : a1;
    int32_t max_0 = a0 > a1 ? a0 : a1;
    int32_t min_1 = a2 < a3 ? a2 : a3;
    int32_t max_1 = a2 > a3 ? a2 : a3;
    int32_t min_ = min_0 < min_1 ? min_0 : min_1;
    int32_t max_ = max_0 > max_1 ? max_0 : max_1;
    return min_ > max_ * C1__MERGE_THRE_SAD_DIF_SCALE;
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
    return (c1enc_rdstat_t){
        mask,
        (mask & C1_RD_RATE_BIT) ? a->r + b->r : 0,
        (mask & C1_RD_DIS_BIT) ? a->d + b->d : 0,
        (mask & C1_RD_SSE_BIT) ? a->sse + b->sse : 0,
        (mask & C1_RD_SAD_BIT) ? (uint32_t)c1_clamp64(a->sad + b->sad, INT32_MIN, INT32_MAX) : 0,
        (mask & C1_RD_FIT_BIT) ? a->fitness + b->fitness : 0,
    };
}

int c1enc_block_add_intra_cand(c1enc_block_t *b, const c1enc_mi_intra_t *mi, const c1enc_rdstat_t *stat) {
    c1enc_rdstat_t *stats = b->intra_cand_stats;
    c1enc_mi_intra_t *cands = b->intra_cands;
    const uint8_t nbcand = b->intra_cand_cnt;

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
    b->inter_cand_cnt = (uint8_t)(nbcand < C1_ENC_INTER_CAND_CNT ? nbcand + 1 : C1_ENC_INTER_CAND_CNT);
    return 0;
}

int c1enc_part_gather_rdstat(c1enc_partition_t *p, int depth) {
    if (p->is_partition && depth > 0) {
        p->stats = (c1enc_rdstat_t){0};
        for (int i = 0; i < 4; i++) {
            c1enc_part_gather_rdstat(p->parts[i], depth - 1);
            p->stats = c1enc_rdstat_merge(&p->stats, &p->parts[i]->stats);
        }
    } else {
        if (p->b->intra_cand_cnt == 0 && p->b->inter_cand_cnt == 0) {
            warning("block do not have dicision candidates");
            return -1;
        }
        const c1enc_rdstat_t *s1 = p->b->intra_cand_stats;
        const c1enc_rdstat_t *s2 = p->b->inter_cand_stats;
        if (p->b->intra_cand_cnt == 0) {
            p->stats = *s2;
        } else if (p->b->inter_cand_cnt == 0) {
            p->stats = *s1;
        } else {
            p->stats = c1enc_rdstat_cmp(s1, s2) > 0 ? *s1 : *s2;
        }
    }
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
            for (uint8_t ci = 0; ci < 3; ci++) {
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
                for (uint8_t ci = 0; ci < 3; ci++) {
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
            for (uint8_t ci = 0; ci < 3; ci++) {
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
/** search inter mode per block per step in a diamond search pattern
 @param step_0 the biggest step, is power of 2
 @param step_cur the current diamond search initial step, power of 2
 @param matrices persistent assessment of fitness of each mvs relative to y0,x0.
 of logical size (step_0*2+1)^2, with y0,x0 at index (step_0,step_0). matrices is measured by sad but
 is not true sad. it may add a distance smoothing and may be sub sampled sad.
*/
static int c1enc_search_inter_b_step(c1enc_block_t *b, const c1_pixbuf_t *pix, const c1enc_search_option_t *opt, //
                                     uint8_t step_0, uint8_t step_cur, int32_t *matrices) {

    // short alias of vars
    uint8_t step = step_cur;
    const uint8_t y0 = opt->inter_y0, x0 = opt->inter_x0;

    // init search info
    c1enc_mv_t c = {y0, x0};                           // mv search center
    c1enc_mv_t d1 = {step, 0};                         // d1 d2 the 2 directions to search, 4 points
    c1pd_option_t pred_opt = {b->size, C1_PRED_MVNEW}; // common predict option

    while (step > 0) {
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
            // deduce corresponding index in matrices
            const uint16_t m_i = mvs[cand].y - y0 + step_0, m_j = mvs[cand].x - x0 + step_0;
            idxs[cand] = m_i * (step_0 * 2 + 1) + m_j;

            if (matrices[idxs[cand]] >= 0) // searched twice, skip
                continue;

            // prepare prediction
            has_new_mv = 1;
            matrices[idxs[cand]] = 0;
            pred_opt.mv = mvs[cand]; // out-of-bound is handled by c1pd_predict

            for (uint8_t ci = 0; ci < 3; ci++) {
                pred_opt.ci = ci;
                c1pd_predict(b, b->p[ci].diff, pix, &pred_opt, NULL);
                // cumulate sad of yuv planes. considering sub sampling.
                if (opt->inter_sad_subsamp_mask & step_cur) {
                    matrices[idxs[cand]] += c1enc__calc_p_sad(b, pix, &pred_opt);
                } else {
                    matrices[idxs[cand]] += c1enc__calc_p_sad(b, pix, &pred_opt);
                }
            }
            // apply distance smoothing
            matrices[idxs[cand]] += opt->inter_smooth_lambda
                                  * (                                    //
                                        c1_abs_dif_i16(mvs[cand].y, 0)   //
                                        + c1_abs_dif_i16(mvs[cand].x, 0) //
                                        )
                                  / 4;
        }
        if (!has_new_mv)
            break;
        // determine new search diamond
        // try flip d1 and d2, make +d1 +d2 best direction (less abs diff)
        if (matrices[idxs[1]] <= matrices[idxs[0]]) {
            d1 = (c1enc_mv_t){0 - d1.y, 0 - d1.x};
        }
        if (matrices[idxs[3] <= matrices[idxs[2]]]) {
            d2 = (c1enc_mv_t){0 - d2.y, 0 - d2.x};
        }
        // update search inf
        c = (c1enc_mv_t){c.y + (d1.y + d2.y) / 2, c.x + (d1.x + d2.x) / 2};
        d1 = (c1enc_mv_t){(d1.y - d2.y) / 2, (d1.x - d2.x) / 2};
        d2 = (c1enc_mv_t){(d1.y + d2.y) / 2, (d1.x + d2.x) / 2};
        step /= 2;
    }
    return 0;
}

int c1enc_search_merge(c1enc_partition_t *p, const c1_pixbuf_t *pix, const c1enc_search_option_t *opt) {
    if (!p->is_partition)
        return 0;
    for (int i = 0; i < 4; i++) {
        if (p->parts[i]->is_partition)
            return 0;
    }
    int32_t m_intra[4], m_inter[4]; // matrices, currently sad.
    int try_inter = 1, try_intra = 1;
    c1enc_rdstat_t part_stat_intra = {C1_RD_SAD_BIT}, part_stat_inter = {C1_RD_SAD_BIT};
    for (int i = 0; i < 4; i++) {
        const c1enc_block_t *b = p->parts[i]->b;
        try_inter
            = try_inter
           && c1enc__is_mode_better(b->intra_cand_stats, b->inter_cand_stats, b->intra_cand_cnt, b->inter_cand_cnt);
        try_intra
            = try_intra
           && c1enc__is_mode_better(b->inter_cand_stats, b->intra_cand_stats, b->inter_cand_cnt, b->intra_cand_cnt);
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
    if (try_intra && !c1enc__is_sad_smooth(m_intra[0], m_intra[1], m_intra[2], m_intra[3])) {
        try_intra = 0;
    }
    if (try_inter && !c1enc__is_sad_smooth(m_inter[0], m_inter[1], m_inter[2], m_inter[3])) {
        try_inter = 0;
    }
    if (!try_intra && !try_inter)
        return 0;

    // until now do we alloc a block
    assert_fatal(!p->b && (p->b = malloc(sizeof(c1enc_block_t))));
    *p->b = (c1enc_block_t){0};
    c1enc_block_update(p->b, p->sb, p->size, p->y, p->x, p->sb_y, p->sb_x, p->buf_offs);
    if (try_intra) {
        c1enc_search_intra_b(p->b, pix, opt);
    }
    if (try_inter) {
        // unimpl
        assert_fatal(try_inter == 0);
    }
    // in case SAD not available, merge is cancelled. not robust?
    int can_merge = (try_intra && p->b->intra_cand_cnt
                     && p->b->intra_cand_stats[0].sad <= part_stat_intra.sad * C1__MERGE_THRE_SAD_DIF_SCALE)
                 || (try_inter && p->b->inter_cand_cnt
                     && p->b->inter_cand_stats[0].sad <= part_stat_inter.sad * C1__MERGE_THRE_SAD_DIF_SCALE);
    if (can_merge) {
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