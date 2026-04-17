#include "src/encode/encoder.h"
#include "encoder.h"
#include "math/transform.h"
#include "src/util/log.h"
#include "src/util/mem.h"
#include "types.h"
#include "util/pixbuf.h"

#include <stdlib.h>
#include <string.h>

#define C1__ROUND_UP(val, div) (((val) + (div) - 1) / (div))
#define C1_ENC_PART_POOLNB 64
#define C1_ENC_BLK_POOLNB 16

// this is the pool context to store all macro blocks
static c1_mpool_t c1enc__part_pool = {C1__ROUND_UP(sizeof(c1enc_partition_t), 8) * 8, C1_ENC_PART_POOLNB, NULL};
static c1_mpool_t c1enc__blk_pool = {C1__ROUND_UP(sizeof(c1enc_block_t), 8) * 8, C1_ENC_BLK_POOLNB, NULL};

static void c1__print_ind(FILE *f, int ind) {
    for (int i = 0; i < ind; i += 4)
        fprintf(f, "    ");
}

int c1enc_frame_update(c1enc_frame_t *frm, const c1_pixbuf_t *pix) {
    // expected height and width
    int eh = C1__ROUND_UP(pix->h, 64) * 64;
    int ew = C1__ROUND_UP(pix->w, 64) * 64;
    int bh = eh / 64, bw = ew / 64;
    // int update_sb=0
    if (eh != frm->inf.hgt_per_sb || ew != frm->inf.wid_per_sb) {
        // frame size (after rounding) has changed.
        // 1. clear sb
        for (int i = 0; i < frm->inf.hgt_per_sb * frm->inf.wid_per_sb; i++) {
            c1enc_sb_clear(frm->super_blocks + i);
        }
        // 2. new sb. malloc and init with {0}
        if (frm->super_blocks)
            free(frm->super_blocks);
        frm->super_blocks = malloc(sizeof(c1enc_super_block_t) * bh * bw);
        assert_fatal(frm->super_blocks);
        memset(frm->super_blocks, 0, sizeof(c1enc_super_block_t) * bh * bw);
        assert_fatal(frm->super_blocks);

        // 3. clear pix
        if (frm->pix.buf) {
            c1_pixbuf_clear(&frm->pix);
            assert_fatal(!frm->pix.buf); // the sptr should be null
        }

        // 4. init info fields
        frm->inf.frame_type = C1_FRAME_I;
        frm->inf.hgt = eh;
        frm->inf.wid = ew;
        frm->inf.hgt_per_sb = bh;
        frm->inf.wid_per_sb = bw;
    }

    // paste pix to frame
    if (!frm->pix.buf) {
        // pix not prepared, create one
        frm->pix = c1_pixbuf_create(C1_PIXBUF_C3I16, eh, ew);
        c1_pixbuf_paste(&frm->pix, pix, 0, 0);
    }
    for (int i = 0; i < bh; i++) {
        for (int j = 0; j < bw; j++)
            c1enc_sb_update(frm->super_blocks + i, frm, i, j);
    }

    return 0;
}
int c1enc_frame_clear(c1enc_frame_t *frm) {
    for (int i = 0; i < frm->inf.hgt_per_sb * frm->inf.wid_per_sb; i++) {
        c1enc_sb_clear(frm->super_blocks + i);
    }
    if (frm->super_blocks)
        free(frm->super_blocks); // this set to NULL afterwards
    c1_pixbuf_clear(&frm->pix);
    *frm = (c1enc_frame_t){0};
    return 0;
}
int c1enc_frame_validate(const c1enc_frame_t *frm) {
    if (frm->pix.h != frm->inf.hgt || frm->pix.w != frm->inf.wid || frm->inf.hgt != frm->inf.hgt_per_sb * 64
        || frm->inf.wid != frm->inf.wid_per_sb * 64) {
        warning2("frame with invalid sizes");
        return -1;
    }
    for (int i = 0; i < frm->inf.hgt_per_sb * frm->inf.wid_per_sb; i++) {
        int res = c1enc_sb_validate(frm->super_blocks + i);
        if (res < 0) {
            warning2("super block validation failed");
            return res;
        }
    }
    return 0;
}
void c1enc_frame_repr(FILE *f, const c1enc_frame_t *frm, int ind) {
    c1__print_ind(f, ind);
    fprintf(f, "Frame [%s, bqi=%d, height=%dx64, width=%dx64](\r\n", frm->inf.frame_type == C1_FRAME_P ? "P" : "I",
            frm->inf.base_q_index, frm->inf.hgt_per_sb, frm->inf.wid_per_sb);
    for (int i = 0; i < frm->inf.hgt_per_sb; i++) {
        for (int j = 0; j < frm->inf.wid_per_sb; j++) {
            c1__print_ind(f, ind + 4);
            fprintf(f, "\"at [%d][%d]:\"\r\n", i, j);
            c1enc_sb_repr(f, frm->super_blocks + i * frm->inf.wid_per_sb + j, ind + 4);
        }
    }
    c1__print_ind(f, ind);
    fprintf(f, ")\r\n");
}

int c1enc_sb_update(c1enc_super_block_t *sb, const c1enc_frame_t *frm, int y, int x) {
    c1_pixbuf_clear(&sb->pix);
    sb->pix = c1_pixbuf_fromview(&frm->pix, (int[3]){y * 64, (y + 1) * 64, 1}, (int[3]){x * 64, (x + 1) * 64, 1});
    if (frm->inf.frame_type == C1_FRAME_I) {
        sb->inf.force_all_intra = 1;
    } else if (frm->inf.frame_type == C1_FRAME_P) {
        sb->inf.force_all_intra = 0;
        sb->inf.is_all_intra = 0;
    } else {
        fatal("unhandled frame type %d", frm->inf.frame_type);
    }
    sb->inf.q_index_delta = 0;
    if (!sb->root) {
        // create a mb tree
        sb->root = c1_mpool_alloc(&c1enc__part_pool);
        assert_fatal(sb->root);
        memset(sb->root, 0, sizeof(c1enc_partition_t));
    }
    c1enc_partition_update(sb->root, &sb->pix, TX2SZ_64_64);
    return 0;
}
int c1enc_sb_clear(c1enc_super_block_t *sb) {
    int res;
    if ((res = c1enc_partition_clear(sb->root)) < 0 || //
        (res = c1_mpool_dealloc(&c1enc__part_pool, sb->root)) < 0)
        return res;
    return 0;
}
int c1enc_sb_validate(const c1enc_super_block_t *sb) {
    int r;
    if (sb->pix.h != 64 || sb->pix.w != 64) {
        warning2("wrong pix size %dx%d", sb->pix.h, sb->pix.w);
        return -1;
    }
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
    fprintf(f, "\"SuperBlock [%sqidelta=%d]\"\r\n", sb->inf.force_all_intra ? "force_intra, " : "",
            sb->inf.q_index_delta);
    if (sb->root) {
        c1enc_partition_repr(f, sb->root, ind);
    } else {
        c1__print_ind(f, ind);
        fprintf(f, "Partition [NULL]");
    }
}

int c1enc_sb_pass0();
int c1enc_sb_pass1();
int c1enc_sb_pass2();

int c1enc_partition_update(c1enc_partition_t *part, const c1_pixbuf_t *pix, C1_TX_2D_SZ size) {
    // 0. case size change or init, uninit
    if (part->pix.h != pix->h || part->pix.w != pix->w) {
        c1enc_partition_clear(part);
    }
    // 1. assign volatile attrs
    part->size = size;
    part->is_all_intra = 0;
    // 2. assign pixbuf
    c1_pixbuf_clear(&part->pix);
    part->pix = c1_pixbuf_dupview(pix);
    // 3. case uninit, init a 64x64...8x8 partition by default
    if (part->is_partition && part->parts[0] == NULL || !part->is_partition && part->b == NULL) {
        if (size == TX2SZ_8_8) {
            // terminal size, init block
            part->is_partition = 0;
            part->b = c1_mpool_alloc(&c1enc__blk_pool);
            *part->b = (c1enc_block_t){0};
            c1enc_block_update(part->b, &part->pix, size);
        } else {
            // partition by 4: tl,tr,bl,br
            assert_fatal(size <= TX2SZ_64_64 && "otherwise unimpl");
            uint8_t new_wid = c1tx_sz2wid(size) / 2;
            C1_TX_2D_SZ new_sz = size - 1;
            c1_pixbuf_t new_pix;
            int slices[4][2][3] = {
                {{0, new_wid, 1}, {0, new_wid, 1}},
                {{0, new_wid, 1}, {new_wid, new_wid * 2, 1}},
                {{new_wid, new_wid * 2, 1}, {0, new_wid, 1}},
                {{new_wid, new_wid * 2, 1}, {new_wid, new_wid * 2, 1}},
            };

            part->is_partition = 1;
            for (int i = 0; i < 4; i++) {
                part->parts[i] = c1_mpool_alloc(&c1enc__part_pool);
                assert_fatal(part->parts[i]);
                *part->parts[i] = (c1enc_partition_t){0};
                new_pix = c1_pixbuf_fromview(&part->pix, slices[i][0], slices[i][1]);
                c1enc_partition_update(part->parts[i], &new_pix, new_sz);
            }
        }
    }
    return 0;
}
int c1enc_partition_clear(c1enc_partition_t *part) {
    if (part->is_partition) {
        for (int i = 0; i < 4; i++) {
            if (part->parts[i]) {
                c1enc_partition_clear(part->parts[i]);
                c1_mpool_dealloc(&c1enc__part_pool, part->parts[i]);
            }
        }
    } else {
        if (part->b) {
            c1enc_block_clear(part->b);
            c1_mpool_dealloc(&c1enc__blk_pool, part->b);
        }
    }
    *part = (c1enc_partition_t){0};
    return 0;
}
int c1enc_partition_validate(const c1enc_partition_t *part) {
    if (part->pix.w != c1tx_sz2wid(part->size)) {
        warning2("mismatch partition size: %d!=%d", part->pix.w, part->size);
        return -1;
    }
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
    fprintf(f, "Partition [%dx%d, %s] (\r\n", c1tx_sz2wid(p->size), c1tx_sz2wid(p->size),
            p->is_partition ? "mid" : "end");
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

int c1enc_block_update(c1enc_block_t *b, const c1_pixbuf_t *pix, C1_TX_2D_SZ size) {
    if (b->size != size) {
        c1enc_block_clear(b);
    }
    // assign plane pixbuf
    for (int ci = 0 /*color index*/; ci < 3; ci++) {
        c1_pixbuf_clear(&b->p[ci].pix);
        b->p[ci].pix = c1_pixbuf_fromchnl(pix, ci);
    }
    // reset volatile attrs
    b->size = size;
    b->tx_size = size; // ?
    b->intra_cand_cnt = 0;
    b->inter_cand_cnt = 0;
    return 0;
}
int c1enc_block_clear(c1enc_block_t *b) {
    // currently no resources is owned by block
    *b = (c1enc_block_t){0};
    return 0;
}
int c1enc_block_validate(c1enc_block_t *b) {
    return 0;
}
void c1enc_block_repr(FILE *f, const c1enc_block_t *b, int ind) {
    c1__print_ind(f, ind);
    fprintf(f, "Block [%dx%d] (\r\n", c1tx_sz2wid(b->size), c1tx_sz2wid(b->size));
    for (int ci = 0; ci < 3; ci++) {
        c1__print_ind(f, ind + 4);
        fprintf(f, "Plane [%c] (\r\n", "yuv"[ci]);
        // c1_pixbuf_repr(f, &b->p[ci].pix);
        c1__print_ind(f, ind + 4);
        fprintf(f, ")\r\n");
    }
    c1__print_ind(f, ind);
    fprintf(f, ")\r\n");
}