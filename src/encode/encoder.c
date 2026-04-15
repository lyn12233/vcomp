#include "src/encode/encoder.h"
#include "src/util/log.h"
#include "src/util/mem.h"
#include "types.h"
#include "util/pixbuf.h"


#include <stdlib.h>
#include <string.h>

#define C1__ROUND_UP(val, div) (((val) + (div) - 1) / (div))
#define C1_ENC_MB_POOLNB 64

// this is the pool context to store all macro blocks
static c1_mpool_t c1enc__mb_pool = {C1__ROUND_UP(sizeof(c1enc_macro_block_t), 8) * 8, C1_ENC_MB_POOLNB, NULL};

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
        if (frm->buf.pix.buf) {
            c1_pixbuf_clear(&frm->buf.pix);
            assert_fatal(!frm->buf.pix.buf); // the sptr should be null
        }

        // 4. init info fields
        frm->inf.frame_type = C1_FRAME_I;
        frm->inf.hgt = eh;
        frm->inf.wid = ew;
        frm->inf.hgt_per_sb = bh;
        frm->inf.wid_per_sb = bw;
    }

    // paste pix to frame
    if (!frm->buf.pix.buf) {
        // pix not prepared, create one
        frm->buf.pix = c1_pixbuf_create(C1_PIXBUF_C3I16, eh, ew);
        c1_pixbuf_paste(&frm->buf.pix, pix, 0, 0);
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
    c1_pixbuf_clear(&frm->buf.pix);
    *frm = (c1enc_frame_t){0};
    return 0;
}
int c1enc_frame_validate(const c1enc_frame_t *frm) {
    if (frm->buf.pix.h != frm->inf.hgt || frm->buf.pix.w != frm->inf.wid || frm->inf.hgt != frm->inf.hgt_per_sb * 64
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
    c1_pixbuf_clear(&sb->buf.pix);
    sb->buf.pix
        = c1_pixbuf_fromview(&frm->buf.pix, (int[3]){y * 64, (y + 1) * 64, 1}, (int[3]){x * 64, (x + 1) * 64, 1});
    if (frm->inf.frame_type == C1_FRAME_I) {
        sb->inf.force_all_intra = 1;
    } else if (frm->inf.frame_type == C1_FRAME_P) {
        sb->inf.force_all_intra = 0;
        sb->inf.is_all_intra = 0;
    } else {
        fatal("unhandled frame type %d", frm->inf.frame_type);
    }
    sb->inf.q_index_delta = 0;
    if (!sb->root_mb) {
        // create a mb tree
        sb->root_mb = c1_mpool_alloc(&c1enc__mb_pool);
        assert_fatal(sb->root_mb);
        memset(sb->root_mb, 0, sizeof(c1enc_macro_block_t));
    }
    c1enc_mb_update(sb->root_mb, sb);
    return 0;
}
int c1enc_sb_validate(const c1enc_super_block_t *sb) {
    int r;
    if (sb->buf.pix.h != 64 || sb->buf.pix.w != 64) {
        warning2("wrong pix size %dx%d", sb->buf.pix.h, sb->buf.pix.w);
        return -1;
    }
    if (!sb->root_mb) {
        warning2("no macro block");
        return -1;
    }
    if ((r = c1enc_mb_validate(sb->root_mb)) < 0) {
        warning2("macro block validation failed");
        return r;
    }
    return 0;
}
void c1enc_sb_repr(FILE *f, const c1enc_super_block_t *sb, int ind) {
    c1__print_ind(f, ind);
    fprintf(f, "\"SuperBlock [%sqidelta=%d]\"\r\n", sb->inf.force_all_intra ? "force_intra, " : "",
            sb->inf.q_index_delta);
    if (sb->root_mb) {
        c1enc_mb_repr(f, sb->root_mb, ind);
    } else {
        c1__print_ind(f, ind);
        fprintf(f, "MacroBlock [NULL]");
    }
}

int c1enc_sb_pass0();
int c1enc_sb_pass1();
int c1enc_sb_pass2();

int c1enc_mb_update(c1enc_macro_block_t *mb, const c1enc_super_block_t *sb) {
    c1_pixbuf_clear(&mb->buf.pix);
    mb->buf.pix = c1_pixbuf_dup(&sb->buf.pix);
    mb->inf.is_all_intra = sb->inf.is_all_intra;
    if (!mb->inf.is_cb && !mb->inf.parts[0]) {
    }
    return 0;
}
int c1enc_mb_validate(const c1enc_macro_block_t *mb) {
    return 0;
}
void c1enc_mb_repr(FILE *f, const c1enc_macro_block_t *mb, int ind) {}
