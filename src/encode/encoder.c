#include "encoder.h"
#include "encode/search.h"
#include "encode/types.h"
#include "predictor.h"
#include "quant.h"
#include "tables.h"
#include "types.h"

#include "math/transform.h"
#include "util/log.h"
#include "util/mem.h"
#include "util/pixbuf.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* table of contents
    - basic type ctor and dtors and reprs
        - frame:            50
        - super block:      130
        - partition:        180
        - block:            290
    - context accessing and referencing
        - context management:   340
        - context access:       410
        - super block access:   440
        - get reference mv:     460
*/

#define C1_ENC_PART_POOLNB (4000 / (sizeof(c1enc_partition_t) + 1))
#define C1_ENC_REF_POOLNB (4000 / (sizeof(c1enc_ref_t) + 1))

// this is the pool context to store all macro blocks
c1_mpool_t c1enc_part_pool = {C1_ROUND_UP(sizeof(c1enc_partition_t), 8) * 8, C1_ENC_PART_POOLNB, NULL};
c1_mpool_t c1enc_ref_pool = {C1_ROUND_UP(sizeof(c1enc_ref_t), 8) * 8, C1_ENC_REF_POOLNB, NULL};

static void c1__print_ind(FILE *f, int ind) {
    for (int i = 0; i < ind; i += 4)
        fprintf(f, "    ");
}

// --- --- constructors and destructors --- ---

// --- frame ---

int c1enc_frame_update(c1enc_frame_t *frm, const c1_pixbuf_t *pix) {
    // expected height and width
    uint16_t eh = C1_ROUND_UP(pix->h, 64) * 64;
    uint16_t ew = C1_ROUND_UP(pix->w, 64) * 64;
    // just dont treat zero size as error
    eh += eh == 0 ? 64 : 0, ew += ew == 0 ? 64 : 0;
    // hgt and wid per super block
    uint16_t hb = eh / 64, wb = ew / 64;

    if (hb != frm->hgt_per_sb || wb != frm->wid_per_sb) {
        // frame size (after rounding) has changed.
        // 1. clear sb
        for (int i = 0; i < frm->hgt_per_sb * frm->wid_per_sb; i++) {
            c1enc_sb_clear(frm->super_blocks + i);
        }
        // 2. new sb. malloc and init with {0}
        if (frm->super_blocks)
            free(frm->super_blocks);
        frm->super_blocks = malloc(sizeof(c1enc_super_block_t) * hb * wb);
        assert_fatal(frm->super_blocks);
        memset(frm->super_blocks, 0, sizeof(c1enc_super_block_t) * hb * wb);
        assert_fatal(frm->super_blocks);

        // 3. clear pix
        if (frm->pix.buf) {
            c1_pixbuf_clear(&frm->pix);
            assert_fatal(!frm->pix.buf); // the sptr should be null
        }

        // 4. init info fields
        frm->frame_type = C1_FRAME_I;
        frm->hgt = eh;
        frm->wid = ew;
        frm->hgt_per_sb = hb;
        frm->wid_per_sb = wb;
    } else {
        frm->frame_type = C1_FRAME_P;
    }

    // paste pix to frame
    if (!frm->pix.buf) {
        // pix not prepared, create one
        frm->pix = c1_pixbuf_create(C1_PIXBUF_C3I16, eh, ew);
    }
    c1_pixbuf_paste(&frm->pix, pix, 0, 0);
    for (uint16_t i = 0; i < hb; i++) {
        for (uint16_t j = 0; j < wb; j++)
            c1enc_sb_update(frm->super_blocks + wb * i + j, frm, i, j);
    }

    return 0;
}
int c1enc_frame_clear(c1enc_frame_t *frm) {
    for (int i = 0; i < frm->hgt_per_sb * frm->wid_per_sb; i++) {
        c1enc_sb_clear(frm->super_blocks + i);
    }
    if (frm->super_blocks)
        free(frm->super_blocks); // this set to NULL afterwards
    c1_pixbuf_clear(&frm->pix);
    *frm = (c1enc_frame_t){0};
    return 0;
}
int c1enc_frame_validate(const c1enc_frame_t *frm) {
    if (frm->pix.h != frm->hgt || frm->pix.w != frm->wid || frm->hgt != frm->hgt_per_sb * 64
        || frm->wid != frm->wid_per_sb * 64) {
        warning2("frame with invalid sizes");
        return -1;
    }
    for (int i = 0; i < frm->hgt_per_sb * frm->wid_per_sb; i++) {
        int res = c1enc_sb_validate(frm->super_blocks + i);
        if (res < 0) {
            warning2("super block validation failed");
            return res;
        }
    }
    return 0;
}
void c1enc_frame_repr(FILE *f, const c1enc_frame_t *frm, int ind) {
    c1_pixbuf_repr(stdout, &frm->pix);
    c1__print_ind(f, ind);
    fprintf(f, "Frame [%s, bqi=%d, height=%dx64, width=%dx64](\r\n", frm->frame_type == C1_FRAME_P ? "P" : "I",
            frm->q_index, frm->hgt_per_sb, frm->wid_per_sb);
    for (int i = 0; i < frm->hgt_per_sb; i++) {
        for (int j = 0; j < frm->wid_per_sb; j++) {
            c1__print_ind(f, ind + 4);
            fprintf(f, "\"at [%d][%d]:\"\r\n", i, j);
            c1enc_sb_repr(f, frm->super_blocks + i * frm->wid_per_sb + j, ind + 4);
        }
    }
    c1__print_ind(f, ind);
    fprintf(f, ")\r\n");
}

// --- super block ---

int c1enc_sb_update(c1enc_super_block_t *sb, const c1enc_frame_t *frm, uint16_t sb_y, uint16_t sb_x) {
    sb->sb_y = sb_y, sb->sb_x = sb_x;

    // volatile attrs

    sb->has_q_index = 0;
    sb->q_index_delta = 0;
    // coef_bufs may not be maintained. just to assure
    if (sb->coef_bufs) {
        warning("coef buf expects to be clear");
        c1enc_sb_dealloc_coef_bufs(sb);
    }

    if (!sb->root) {
        // init, create a mb tree
        assert_fatal((sb->root = c1_mpool_alloc(&c1enc_part_pool)));
        *sb->root = (c1enc_partition_t){0};
        c1enc_partition_init(sb->root, sb, C1_SZ_64_64, C1_SZ_32_32, 0, 0, sb_y, sb_x, 0);
    } else {
        c1enc_partition_reset_cands(sb->root);
        // case init, cand cnt is 0, no need to reset
    }
    return 0;
}
int c1enc_sb_clear(c1enc_super_block_t *sb) {
    int res = 0;
    if ((res = c1enc_partition_clear(sb->root)) < 0 ||              //
        (res = c1_mpool_dealloc(&c1enc_part_pool, sb->root)) < 0 || //
        (res = c1enc_sb_dealloc_coef_bufs(sb)) < 0)
        return res;
    return 0;
}
int c1enc_sb_validate(const c1enc_super_block_t *sb) {
    int r;
    if (!sb->root) {
        warning2("no macro block");
        return -1;
    }
    if ((r = c1enc_partition_validate(sb->root)) < 0) {
        warning2("macro block validation failed");
        return r;
    }
    return 0;
}
void c1enc_sb_repr(FILE *f, const c1enc_super_block_t *sb, int ind) {
    c1__print_ind(f, ind);
    fprintf(f, "\"SuperBlock [qidelta=%d]\"\r\n", sb->q_index_delta);
    if (sb->root) {
        c1enc_partition_repr(f, sb->root, ind);
    } else {
        c1__print_ind(f, ind);
        fprintf(f, "Partition [NULL]");
    }
}

static int c1enc__part_set_coef_bufs(c1enc_partition_t *p, //
                                     int32_t *coef_bufs, int32_t *qcoef_bufs, int32_t *dqcoef_bufs) {
    const int h = c1_sz2hgt(p->size), w = c1_sz2wid(p->size);
    if (p->is_partition) {
        for (int i = 0; i < 4; i++) {
            c1enc__part_set_coef_bufs(p->parts[i], coef_bufs, qcoef_bufs, dqcoef_bufs);
        }
    } else {
        c1enc_block_t *b = p->b;
        for (int ci = 0; ci < 3; ci++) {
            b->p[ci].coef = coef_bufs ? coef_bufs + p->buf_offs + h * w * ci : NULL;
            b->p[ci].qcoef = qcoef_bufs ? qcoef_bufs + p->buf_offs + h * w * ci : NULL;
            b->p[ci].dqcoef = dqcoef_bufs ? dqcoef_bufs + p->buf_offs + h * w * ci : NULL;
        }
    }
    return 0;
}
static int c1enc__part_unset_coef_bufs(c1enc_partition_t *p) {
    if (p->is_partition) {
        for (int i = 0; i < 4; i++) {
            c1enc__part_unset_coef_bufs(p->parts[i]);
        }
    } else {
        c1enc_block_t *b = p->b;
        for (int ci = 0; ci < 3; ci++) {
            b->p[ci].coef = NULL;
            b->p[ci].qcoef = NULL;
            b->p[ci].dqcoef = NULL;
        }
    }
    return 0;
}
int c1enc_sb_require_coef_bufs(c1enc_super_block_t *sb, uint8_t lv) {
    if (sb->coef_bufs) {
        // do not check if lv matches. it's ok
        return 0;
    }
    assert_fatal_ex(lv == 1 || lv == 3, "invalide lv=%u", lv);

    int32_t *coef_bufs = malloc(lv * 3 * 64 * 64 * sizeof(int32_t));
    assert_fatal(coef_bufs);
    sb->coef_bufs = coef_bufs;

    if (lv == 3) {
        c1enc__part_set_coef_bufs(sb->root, coef_bufs, coef_bufs + 3 * 64 * 64, coef_bufs + 2 * 3 * 64 * 64);
    } else {
        c1enc__part_set_coef_bufs(sb->root, coef_bufs, NULL, NULL);
    }

    return 0;
}
int c1enc_sb_dealloc_coef_bufs(c1enc_super_block_t *sb) {
    if (!sb->coef_bufs)
        return 0;
    free(sb->coef_bufs);
    sb->coef_bufs = NULL;
    c1enc__part_unset_coef_bufs(sb->root);
    return 0;
}

// --- partition ---

int c1enc_partition_init(c1enc_partition_t *part, c1enc_super_block_t *sb, C1_2D_SZ size, C1_2D_SZ targ_size, //
                         uint8_t y, uint8_t x, uint16_t sb_y, uint16_t sb_x,                                  //
                         uint32_t buf_offs) {
    // 0. case size change
    // note init means {0}, which may or may not effect
    if (part->size != size) {
        c1enc_partition_clear(part);
    }
    // 1. assign volatile attrs
    part->size = size;
    part->is_all_intra = 0;
    part->y = y, part->x = x, part->sb_y = sb_y, part->sb_x = sb_x;
    part->buf_offs = buf_offs;
    part->sb = sb;

    // 3. case init, init a 64x64...8x8 partition by default
    if (part->is_partition /*wont met but for robustness*/ && part->parts[0] == NULL || //
        !part->is_partition && part->b == NULL) {
        assert_fatal(size <= C1_SZ_64_64 && "unimpl size");

        if (size <= targ_size) {
            // terminal size, init block
            part->is_partition = 0;
            assert_fatal(!part->b && (part->b = malloc(sizeof(c1enc_block_t))));
            *part->b = (c1enc_block_t){0};
            c1enc_block_update(part->b, sb, size, y, x, sb_y, sb_x, buf_offs);
        } else {
            // partition by 4: tl,tr,bl,br
            C1_2D_SZ new_sz = size - 1;
            uint8_t new_hgt = c1_sz2hgt(new_sz);
            uint8_t new_wid = c1_sz2wid(new_sz);
            const uint8_t offs[4][2] = {
                {0, 0},
                {0, new_wid},
                {new_hgt, 0},
                {new_hgt, new_wid},
            };

            part->is_partition = 1;
            for (uint8_t i = 0; i < 4; i++) {
                assert_fatal(!part->parts[i] && (part->parts[i] = c1_mpool_alloc(&c1enc_part_pool)));
                *part->parts[i] = (c1enc_partition_t){0};
                c1enc_partition_init(part->parts[i], sb, new_sz, targ_size,      //
                                     y + offs[i][0], x + offs[i][1], sb_y, sb_x, //
                                     buf_offs + i * new_hgt * new_wid * 3);
            }
        }
    }
    return 0;
}
int c1enc_partition_reset_cands(c1enc_partition_t *part) {
    if (part->is_partition) {
        for (int i = 0; i < 4; i++) {
            c1enc_partition_reset_cands(part->parts[i]);
        }
    } else {
        part->b->intra_cand_cnt = part->b->inter_cand_cnt = 0;
    }
    return 0;
}
int c1enc_partition_clear(c1enc_partition_t *part) {
    for (int i = 0; i < 4; i++) {
        if (part->parts[i]) {
            c1enc_partition_clear(part->parts[i]);
            c1_mpool_dealloc(&c1enc_part_pool, part->parts[i]);
        }
    }
    if (part->b) {
        c1enc_block_clear(part->b);
        free(part->b);
    }

    *part = (c1enc_partition_t){0};
    return 0;
}
int c1enc_partition_validate(const c1enc_partition_t *part) {
    if (part->is_partition) {
        for (int i = 0; i < 4; i++) {
            if (!part->parts[i]) {
                warning2("impaired partition tree (node %d)", i);
                return -1;
            }
            if (part->parts[i]->size != part->size - 1) {
                warning2("wrong next partition size %d", part->parts[i]->size);
                return -1;
            }
        }
    } else {
        if (!part->b) {
            warning2("block ptr is null");
            return -1;
        }
        int res;
        if ((res = c1enc_block_validate(part->b)) < 0) {
            warning2("block validation failed");
            return res;
        }
    }
    return 0;
}
void c1enc_partition_repr(FILE *f, const c1enc_partition_t *p, int ind) {
    c1__print_ind(f, ind);
    fprintf(f, "Partition [%dx%d, %s] (\r\n", c1_sz2wid(p->size), c1_sz2wid(p->size), p->is_partition ? "mid" : "end");
    if (p->is_partition) {
        for (int i = 0; i < 4; i++) {
            c1enc_partition_repr(f, p->parts[i], ind + 4);
        }
    } else {
        c1enc_block_repr(f, p->b, ind + 4);
    }
    c1__print_ind(f, ind);
    fprintf(f, ")\r\n");
}

// --- block ---

int c1enc_block_update(c1enc_block_t *b, c1enc_super_block_t *sb, C1_2D_SZ size, //
                       uint8_t y, uint8_t x, uint16_t sb_y, uint16_t sb_x,       //
                       uint32_t buf_offs) {
    // not many owned instances, thus always clear first
    c1enc_block_clear(b);

    // reset volatile attrs

    b->sb_y = sb_y, b->sb_x = sb_x;
    b->yoff = y, b->xoff = x;
    b->size = size;

    b->has_ref_mv = 0;
    b->has_tx_cand = 0;
    b->intra_cand_cnt = 0;
    b->inter_cand_cnt = 0;
    b->cached_pred_stat_cnt = 0;
    b->pred_type_determined = 0;

    // assign buf in sb for planes
    for (int ci = 0; ci < 3; ci++) {
        b->p[ci].diff = sb->diff_buf + buf_offs + c1_sz2hgt(size) * c1_sz2wid(size) * ci;
        b->p[ci].coef = NULL;
        b->p[ci].qcoef = NULL;
        b->p[ci].dqcoef = NULL;
    }
    return 0;
}
int c1enc_block_clear(c1enc_block_t *b) {
    // dealloc resources is owned by block
    for (int ci = 0; ci < 3; ci++) {
        if (b->p[ci].border_above) {
            free(b->p[ci].border_above);
            b->p[ci].border_above = NULL;
        }
        if (b->p[ci].border_left) {
            free(b->p[ci].border_left);
            b->p[ci].border_left = NULL;
        }
    }
    *b = (c1enc_block_t){0};
    return 0;
}
int c1enc_block_validate(c1enc_block_t *b) {
    return 0;
}
void c1enc_block_repr(FILE *f, const c1enc_block_t *b, int ind) {
    c1__print_ind(f, ind);
    fprintf(f, "Block [%dx%d, (+%d,+%d)] ", c1_sz2wid(b->size), c1_sz2wid(b->size), b->yoff, b->xoff);
    if (b->pred_type_determined) {
        fprintf(f, "[best_pred=%s] ", b->pred_type == C1_PRED_INTRA ? "intra" : "inter");
    }
    fprintf(f, "(\r\n");
    // for (int ci = 0; ci < 3; ci++) {
    //     c1__print_ind(f, ind + 4);
    //     fprintf(f, "Plane [%c] (\r\n", "yuv"[ci]);
    //     c1__print_ind(f, ind + 4);
    //     fprintf(f, ")\r\n");
    // }
    if (b->intra_cand_cnt > 0) {
        c1__print_ind(f, ind + 4);
        fprintf(f, "intra_cands: ");
        for (int i = 0; i < b->intra_cand_cnt; i++) {
            const c1enc_mi_intra_t *mi = b->intra_cands + i;
            const c1enc_rdstat_t *stat = b->intra_cand_stats + i;
            fprintf(f, "(%s, ", c1pd_mode2str(mi->mode_y));
            if (mi->mode_y != mi->mode_uv && !mi->use_cfl)
                fprintf(f, "%s, ", c1pd_mode2str(mi->mode_uv));
            if (mi->use_cfl) {
                fprintf(f, "use_cfl, ");
            }
            fprintf(f, "SAD=%u", stat->sad);
            fprintf(f, "), ");
        }
        fprintf(f, "\r\n");
    }
    c1__print_ind(f, ind);
    fprintf(f, ")\r\n");
}

// --- --- context accessing and referencing --- ---

// --- context managment ---

int c1enc_ctx_clear_entry(c1enc_ctx_t *ctx, uint8_t idx) {
    const uint16_t h = C1_ROUND_UP(ctx->ref_frames[idx].h, 64);
    const uint16_t w = C1_ROUND_UP(ctx->ref_frames[idx].w, 64);
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            c1enc_ref_clear(ctx->sb_refs[idx] + i * w + j);
        }
    }
    free(ctx->sb_refs[idx]);
    ctx->sb_refs[idx] = NULL;
    c1_pixbuf_clear(ctx->ref_frames + idx);
    return 0;
}

int c1enc_push_ref(c1enc_ctx_t *ctx, const c1enc_frame_t *frm) {
    // (1) push pixbuf. it should be dequant+inv tx result
    assert_fatal(ctx->avail_ref_cnt <= C1_ENC_REF_FRAME_CNT);
    if (ctx->avail_ref_cnt == C1_ENC_REF_FRAME_CNT) {
        c1enc_ctx_clear_entry(ctx, 0);
        memmove(ctx->ref_frames, ctx->ref_frames + 1, (C1_ENC_REF_FRAME_CNT - 1) * sizeof(c1_pixbuf_t));
        memmove(ctx->sb_refs, ctx->sb_refs + 1, (C1_ENC_REF_FRAME_CNT - 1) * sizeof(c1enc_ref_t *));
    } else {
        ctx->avail_ref_cnt++;
    }
    const uint8_t idx = ctx->avail_ref_cnt - 1;
    ctx->ref_frames[idx] = c1_pixbuf_dupview(&frm->pix);
    // (2) create ref for each super block in frame
    const uint16_t h = frm->hgt_per_sb, w = frm->wid_per_sb;
    ctx->sb_refs[idx] = malloc(sizeof(c1enc_ref_t) * h * w);
    assert_fatal(ctx->sb_refs[idx]);
    for (uint16_t sb_y = 0; sb_y < h; sb_y++) {
        for (uint16_t sb_x = 0; sb_x < w; sb_x++) {
            c1enc_ref_from_part(ctx->sb_refs[idx] + sb_y * w + sb_x, frm->super_blocks[sb_y * w + sb_x].root);
        }
    }
    return 0;
}
static c1enc_mv_t c1enc__block_get_mvref(const c1enc_block_t *b) {
    if (b->pred_type_determined && b->pred_type == C1_PRED_INTER) {
        assert_fatal(b->inter_cand_cnt > 0);
        return b->inter_cands[0].mv; // currently mvd not considered. change later?
    } else {
        return (c1enc_mv_t){0};
    }
}
int c1enc_ref_from_part(c1enc_ref_t *ref, const c1enc_partition_t *p) {
    ref->is_partition = p->is_partition;
    ref->y = p->y, ref->x = p->x;
    if (p->is_partition) {
        for (int i = 0; i < 4; i++) {
            assert_fatal((ref->refs[i] = c1_mpool_alloc(&c1enc_ref_pool)));
            c1enc_ref_from_part(ref->refs[i], p->parts[i]);
        }
    } else {
        ref->size = p->size;
        // get mv from block;
        const c1enc_block_t *b = p->b;
        ref->mv = c1enc__block_get_mvref(b);
    }
    return 0;
}
int c1enc_ref_clear(c1enc_ref_t *ref) {
    if (ref->is_partition) {
        for (int i = 0; i < 4; i++) {
            c1enc_ref_clear(ref->refs[i]);
            c1_mpool_dealloc(&c1enc_ref_pool, ref->refs[i]);
            ref->refs[i] = NULL;
        }
    }
    *ref = (c1enc_ref_t){0};
    return 0;
}

// --- context access ---

static c1enc_ref_t *c1enc__ref_at_fromref(c1enc_ref_t *ref, uint8_t y, uint8_t x) {
    const uint8_t h = c1_sz2hgt(ref->size), w = c1_sz2wid(ref->size);
    assert_fatal(y >= ref->y && y < ref->y + h);
    assert_fatal(x >= ref->x && x < ref->x + w);
    if (ref->is_partition) {
        // remind the order of parts: tl, tr, bl, br
        int idx = (y >= ref->y + h / 2) * 2 + (x >= ref->x + w / 2);
        return c1enc__ref_at_fromref(ref->refs[idx], y, x);
    } else {
        return ref;
    }
}
c1enc_ref_t *c1enc_ref_at(c1enc_ctx_t *ctx, uint16_t sb_y, uint16_t sb_x, uint8_t y, uint8_t x, uint8_t ref_id) {
    const int idx = (int)ctx->avail_ref_cnt - ref_id;
    assert_fatal(idx >= 0 && idx < ctx->avail_ref_cnt);
    const uint16_t h = C1_ROUND_UP(ctx->ref_frames[idx].h, 64);
    const uint16_t w = C1_ROUND_UP(ctx->ref_frames[idx].w, 64);
    if (sb_y >= h || sb_x >= w) {
        warning("super block index out of range");
        return NULL;
    }
    c1enc_ref_t *sb_ref = ctx->sb_refs[idx] + sb_y * w + sb_x;
    return c1enc__ref_at_fromref(sb_ref, y, x);
}

// --- super block access ---

static c1enc_block_t *c1enc__block_at_fromp(c1enc_partition_t *p, uint8_t y, uint8_t x) {
    const uint8_t h = c1_sz2hgt(p->size), w = c1_sz2wid(p->size);
    assert_fatal(y >= p->y && y < p->y + h);
    assert_fatal(x >= p->x && x < p->x + w);
    if (p->is_partition) {
        int idx = (y >= p->y + h / 2) * 2 + (x >= p->x + w / 2);
        return c1enc__block_at_fromp(p->parts[idx], y, x);
    } else {
        return p->b;
    }
}
c1enc_block_t *c1enc_block_at(c1enc_frame_t *frm, uint16_t sb_y, uint16_t sb_x, uint8_t y, uint8_t x) {
    if (sb_y > frm->hgt_per_sb || sb_x > frm->wid_per_sb) {
        return NULL;
    }
    c1enc_partition_t *p = frm->super_blocks[sb_y * frm->wid_per_sb + sb_x].root;
    return c1enc__block_at_fromp(p, y, x);
}

// --- get reference mv ---

c1enc_mv_t c1enc_get_mvref(const c1enc_frame_t *frm, const c1enc_ctx_t *ctx, //
                           uint16_t sb_y, uint16_t sb_x, uint8_t y, uint8_t x, uint8_t ref_id) {
    if (ref_id == 0) {
        const c1enc_block_t *b = c1enc_block_at((c1enc_frame_t *)frm, sb_y, sb_x, y, x);
        assert_fatal(b);
        return c1enc__block_get_mvref(b);
    } else {
        const c1enc_ref_t *ref = c1enc_ref_at((c1enc_ctx_t *)ctx, sb_y, sb_x, y, x, ref_id);
        assert_fatal(ref);
        return ref->mv;
    }
}

// --- vis ---

static void c1enc__get_dif_part(c1_pixbuf_t *pix, const c1enc_partition_t *p) {
    if (p->is_partition) {
        for (int i = 0; i < 4; i++) {
            c1enc__get_dif_part(pix, p->parts[i]);
        }
    } else {
        const c1enc_block_t *b = p->b;
        for (int i = 0; i < c1_sz2hgt(p->size); i++) {
            for (int j = 0; j < c1_sz2wid(p->size); j++) {
                int16_t *ptr = c1_pixbuf_get(pix, i + p->y, j + p->x);
                for (int ci = 0; ci < 3; ci++) {
                    ptr[ci] = b->p[ci].diff[i * c1_sz2wid(p->size) + j];
                }
            }
        }
    }
}

int c1enc_get_dif_sb(const c1enc_super_block_t *sb, c1_pixbuf_t *pix) {
    assert_fatal(pix->h == 64 && pix->w == 64 && pix->type == C1_PIXBUF_C3I16);
    c1enc__get_dif_part(pix, sb->root);
    return 0;
}

int c1enc_get_dif(const c1enc_frame_t *frm, c1_pixbuf_t *pix) {
    for (int i = 0; i < frm->hgt_per_sb; i++) {
        for (int j = 0; j < frm->wid_per_sb; j++) {
            c1_pixbuf_t tmp
                = c1_pixbuf_fromview(pix, (int[3]){i * 64, i * 64 + 64, 1}, (int[3]){j * 64, j * 64 + 64, 1});
            c1enc_get_dif_sb(frm->super_blocks + i * frm->wid_per_sb + j, &tmp);
            c1_pixbuf_clear(&tmp);
        }
    }
    return 0;
}
static void c1enc__get_coef_part(c1_pixbuf_t *pix, const c1enc_partition_t *p) {
    if (p->is_partition) {
        for (int i = 0; i < 4; i++)
            c1enc__get_coef_part(pix, p->parts[i]);
    } else {
        const c1enc_block_t *b = p->b;
        assert_fatal(b->has_tx_cand);
        const int bh = c1_sz2hgt(b->size), bw = c1_sz2wid(b->size);
        const int txh = c1_sz2hgt(b->tx_inf.tx_size), txw = c1_sz2wid(b->tx_inf.tx_size);
        for (int bi = 0; bi < bh; bi += txh) {
            for (int bj = 0; bj < bw; bj += txw) {
                for (int i = 0; i < txh; i++) {
                    for (int j = 0; j < txw; j++) {
                        int idx = bi * bw + bj * txh + i * txw + j;
                        int16_t *out = c1_pixbuf_geti16(pix, bi + i, bj + j);
                        for (int ci = 0; ci < 3; ci++) {
                            out[ci] = (int16_t)c1_clamp32(b->p[ci].coef[idx], INT16_MIN, INT16_MAX);
                        }
                    }
                }
            } // bj
        } // bi
    }
}
int c1enc_get_coef_sb(const c1enc_super_block_t *sb, c1_pixbuf_t *pix) {
    assert_fatal(pix->h == 64 && pix->w == 64 && pix->type == C1_PIXBUF_C3I16);
    c1enc__get_coef_part(pix, sb->root);
    return 0;
}
int c1enc_get_coef(const c1enc_frame_t *frm, c1_pixbuf_t *pix) {
    for (int i = 0; i < frm->hgt_per_sb; i++) {
        for (int j = 0; j < frm->wid_per_sb; j++) {
            c1_pixbuf_t tmp
                = c1_pixbuf_fromview(pix, (int[3]){i * 64, i * 64 + 64, 1}, (int[3]){j * 64, j * 64 + 64, 1});
            c1enc_get_coef_sb(frm->super_blocks + i * frm->wid_per_sb + j, &tmp);
            c1_pixbuf_clear(&tmp);
        }
    }
    return 0;
}

//

int c1enc_encode(c1enc_frame_t *frm, const c1_pixbuf_t *pix, c1enc_ctx_t *ctx, const c1enc_option_t *opt) {
    // (0) in case resize or init
    c1enc_frame_update(frm, pix);

    // (1) frame type

    int use_i_frame = frm->frame_type == C1_FRAME_I;
    // decide if consecutive p frame exceeds range in option
    if (!use_i_frame) {
        if (ctx->consecutive_p_cnt >= opt->max_p_frames) {
            use_i_frame = 1;
        }
    }
    // decide if frame changes greatly. todo: impl
    if (!use_i_frame) {
        (void)0;
    }
    // respond to frame type
    if (use_i_frame) {
        for (uint8_t idx = 0; idx < ctx->avail_ref_cnt; idx++) {
            c1enc_ctx_clear_entry(ctx, idx);
        }
        ctx->avail_ref_cnt = 0;
    }
    frm->frame_type = use_i_frame ? C1_FRAME_I : C1_FRAME_P;

    // (2) search prediction

    // todo impl better ref_id
    uint8_t ref_id = 1; // the previous first frame

    // ephemeral search opt impl
    c1enc_search_option_t srch_opt = {0};
    if (!use_i_frame) {
        srch_opt.try_inter = 1;
        srch_opt.inter_init_steps_mask = 32 | 16 | 8 | 4;
        srch_opt.inter_sad_subsamp_mask = (1 << 7) - (1 << 3);
        srch_opt.inter_smooth_lambda = 0;
        srch_opt.inter_newcand_cnt = C1_ENC_INTER_CAND_CNT;
        srch_opt.inter_ref_idx = ref_id;
    }
    srch_opt.try_intra = 1;
    srch_opt.intra_try_uv = 1;
    srch_opt.intra_try_cfl = 0;
    srch_opt.intra_rng_max = C1_PRED_PAETH + 1;
    srch_opt.thre_mode_better_mult = 3;
    srch_opt.thre_mode_better_shift = 1;
    srch_opt.thre_mat_is_dif_mult = 3;
    srch_opt.thre_mat_is_dif_shift = 2;
    srch_opt.thre_mat_is_dif_delta = 1 << 12;
    // todo: impl
    srch_opt.thre_sad_max_b = 1 << 12;

    // search in a order that ensure prediction edges(at least bh+bw<=64*2) exist ?
    const int hb = frm->hgt_per_sb, wb = frm->wid_per_sb;
    for (int idx = 0; idx < hb * wb; idx++) {
        c1enc_search_sb(frm->super_blocks + idx, pix, ctx, &srch_opt);
        // gather mode and diff
        c1enc_part_gather_pred_type(frm->super_blocks[idx].root);
        c1enc_part_gather_residual(frm->super_blocks[idx].root, pix, ctx, ref_id);
    }

    // (3) tx&quantize&recon
    c1tx_search_option_t tx_opt = {0};
    tx_opt.size_depth = 2;
    tx_opt.measure = C1TX_MEASURE_NOP;
    tx_opt.tx_rng_max = TX2TYPE_DCT_DCT + 1;
    for (int idx = 0; idx < hb * wb; idx++) {
        c1tx_search_sb(frm->super_blocks + idx, tx_opt);
        c1enc_sb_gather_qi(frm->super_blocks + idx, opt->qp);
        c1enc_sb_dealloc_coef_bufs(frm->super_blocks + idx);
    }
    c1enc_frame_gather_qi(frm, opt->qp);
    tx_opt.measure = C1TX_MEASURE_RATE;
    tx_opt.tx_rng_max = TX2TYPE_IDEN + 1;
    for (int idx = 0; idx < hb * wb; idx++) {
        c1enc_super_block_t *sb = frm->super_blocks + idx;
        // currenty use y plance ac qstep to assess, todo: ?
        tx_opt.qstep = c1_lookup_q_ac[0][sb->q_index];
        c1tx_search_sb(sb, tx_opt);
        c1enc_quantize_sb(sb);

        c1enc_sb_dqc2c(sb);
        c1tx_reconstruct(sb);

        c1pd_reconstruct_sb(sb, frm, ctx); // pred is done again here.
        c1enc_sb_dealloc_coef_bufs(sb);
    }

    // (4) update ctx

    c1enc_push_ref(ctx, frm);
    ctx->est_qi = frm->q_index;
    ctx->consecutive_p_cnt += !use_i_frame;

    // assess/write to bitstream?

    return 0;
}