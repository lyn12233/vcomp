#include "writer.h"
#include "math/entcoder.h"
#include "types.h"
#include "util/log.h"

#include <assert.h>
#include <corecrt_search.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

c1_encstrm_t c1_es_create(int (*write_callback)(void *, uint32_t, void *), void *write_ctx, uint32_t buf_sz_max) {
    c1_encstrm_t res = {0};
    res.prev_is_symbol = 0;
    res.bit_offs = 0;
    res.buf_sz = 0;
    res.buf_sz_max = buf_sz_max;
    res.buf_resv_sz = 1024;
    res.buf = malloc(res.buf_resv_sz);
    assert_fatal(res.buf);
    assert_fatal(c1ent_enc_init(&res.enc, 0) >= 0);
    res.write_callback = write_callback;
    res.write_ctx = write_ctx;
    return res;
}
int c1_es_align(c1_encstrm_t *es) {
    if (es->bit_offs > 0) {
        // remaining bits may be either lsb or msb, thus take random padding.
        // es->buf[es->buf_sz] &= (uint8_t)(256 - (1 << (8 - es->bit_offs))); // 8-bit_offs 0's at lsb side
        es->buf_sz++;
    }
    return 0;
}
static int c1_es__try_write(c1_encstrm_t *es) {
    if (!es->write_callback) {
        return 0;
    }
    int ind = 0;
    while (ind + es->buf_sz_max <= es->buf_sz) {
        es->write_callback(es->buf + ind, es->buf_sz_max, es->write_ctx);
        ind += es->buf_sz_max;
    }
    memmove(es->buf, es->buf + ind, es->buf_sz - ind);
    es->buf_sz -= ind;
    return 0;
}
int c1_es_write_raw(c1_encstrm_t *es, uint32_t len, uint64_t bits) {
    assert((1 << bits) > len);
    if (es->prev_is_symbol) {
        uint32_t nbytes;
        const uint8_t *ent_out = c1ent_enc_done(&es->enc, &nbytes);
        if (!ent_out) {
            fatal("encoder failed");
            return -1;
        }
        // concate to buf
        assert(nbytes > 0); // less edge cases
        uint32_t new_buf_sz = es->buf_sz + nbytes + (nbytes <= 1);
        if (new_buf_sz > es->buf_resv_sz) {
            assert_fatal((es->buf = realloc(es->buf, new_buf_sz * 2)) != NULL);
            es->buf_resv_sz = new_buf_sz * 2;
        }
        memcpy(es->buf + es->buf_sz, ent_out, nbytes);
        if (nbytes <= 1) {
            // add a zero, though redundant, assures decoder do not consumes extra bits.
            // nbytes=1 is a special case for the decoder, which requires to read desiredly 15bit to init its states.
            es->buf[es->buf_sz + nbytes] = 0;
        }
        es->buf_sz = new_buf_sz;

        if (c1ent_enc_clear(&es->enc) < 0 || //
            c1ent_enc_init(&es->enc, 0)) {
            fatal("ent_enc_t failed");
        };

        c1_es__try_write(es);
        es->prev_is_symbol = 0;
    }
    uint32_t new_buf_sz = es->buf_sz + (len + 7) / 8;
    if (new_buf_sz > es->buf_resv_sz) {
        assert_fatal((es->buf = realloc(es->buf, new_buf_sz * 2)) != NULL);
        es->buf_resv_sz = new_buf_sz * 2;
    }
    if (es->bit_offs + len < 8) {
        uint8_t m1 = (uint8_t)((1 << (es->bit_offs)) - 1);
        uint8_t m2 = (uint8_t)((1 << (len)) - 1);
        es->buf[es->buf_sz] &= m1;
        es->buf[es->buf_sz] |= (bits & m2) << es->bit_offs;
        es->bit_offs += len;
    } else {
        if (es->bit_offs > 0) {
            uint8_t m1 = (uint8_t)((1 << (es->bit_offs)) - 1);
            uint8_t m2 = (uint8_t)((1 << (8 - es->bit_offs)) - 1);
            es->buf[es->buf_sz] &= m1;
            es->buf[es->buf_sz] |= (bits & m2) << es->bit_offs;
            // update bits
            len -= 8 - es->bit_offs;
            bits >>= 8 - es->bit_offs;
            // update buf
            es->buf_sz++;
            es->bit_offs = 0;
        }
        // bit_offs=0
        while (len >= 8) {
            // update buf
            es->buf[es->buf_sz] = (uint8_t)(bits & 0xff);
            es->buf_sz++;
            // update bits
            len -= 8;
            bits >>= 8;
        }
        // len<8
        if (len > 0) {
            uint8_t m2 = (uint8_t)((1 << len) - 1);
            es->buf[es->buf_sz] = (bits & m2);
            es->bit_offs = (uint8_t)len;
        }
    }
    c1_es__try_write(es);
    return 0;
}
int c1_es_write_sym(c1_encstrm_t *es, int sym, uint16_t *cdf, int nbsym, int update_cdf) {
    if (!es->prev_is_symbol) {
        c1_es_align(es);
        es->prev_is_symbol = 1;
    }
    c1ent_encode_cdf(&es->enc, sym, cdf, nbsym);
    if (update_cdf) {
        c1ent_update_cdf(cdf, (uint8_t)sym, (uint8_t)nbsym);
    }
    return 0;
}
int c1_es_flush(c1_encstrm_t *es) {
    if (!es->write_callback) {
        warning2("no write callback");
        return 0;
    }
    c1_es__try_write(es);
    assert(es->buf_sz < es->buf_sz_max);
    es->write_callback(es->buf, es->buf_sz, es->write_ctx);
    es->buf_sz = 0;

    return 0;
}
int c1_es_clear(c1_encstrm_t *es) {
    assert_fatal(es->buf);
    free(es->buf);
    c1ent_enc_clear(&es->enc);
    return 0;
}

c1_decstrm_t c1_ds_create(c1_sptr_t *sbuf, uint32_t buf_sz) {
    c1_decstrm_t dec = {0};
    dec.prev_is_symbol = 0;
    // dec.buf_sz = buf_sz;
    // uint32_t offs = 0;

    dec.sbuf = sbuf;
    c1_sptr_incref(&sbuf);

    assert_fatal(dec.strstrm = malloc(sizeof(c1ent_strstrm_t)));
    *dec.strstrm = (c1ent_strstrm_t){sbuf->ptr, buf_sz * 8, 0};

    assert_fatal(c1ent_dec_init(&dec.dec, buf_sz, c1ent_strstrm_read_bits16, dec.strstrm));
    return dec;
}
int c1_ds_align(c1_decstrm_t *ds) {
    if (ds->strstrm->offs % 8 > 0)
        ds->strstrm->offs += 8 - ds->strstrm->offs % 8;
    return 0;
}

static uint8_t c1ent__strstrm_get_lsb(c1ent_strstrm_t *strm, uint32_t offs) {
    uint8_t c = strm->data[offs / 8];
    return (c >> (offs % 8)) & 1;
}
uint64_t c1_ds_read_raw(c1_decstrm_t *ds, uint32_t len) {
    if (ds->prev_is_symbol) {
        c1ent_dec_exit(&ds->dec, c1ent_strstrm_read_bits16, ds->strstrm);
        c1_ds_align(ds);
        ds->prev_is_symbol = 0;
    }
    uint64_t bits = 0;
    assert_fatal(len <= 64);
    for (uint32_t i = 0; i < len; i++) {
        bits += (uint64_t)c1ent__strstrm_get_lsb(ds->strstrm, ds->strstrm->offs + i) << (uint64_t)i;
    }
    ds->strstrm->offs += len;
    return bits;
}
int c1_ds_read_sym(c1_decstrm_t *ds, uint16_t *cdf, int nbsym, int update_cdf) {
    if (!ds->prev_is_symbol) {
        c1_ds_align(ds);
        c1ent_dec_init(&ds->dec, ds->strstrm->sz / 8 - ds->strstrm->offs / 8, c1ent_strstrm_read_bits16, ds->strstrm);
        ds->prev_is_symbol = 1;
    }
    int sym = c1ent_decode_cdf(&ds->dec, c1ent_strstrm_read_bits16, &ds->strstrm, cdf, nbsym);
    if (update_cdf) {
        c1ent_update_cdf(cdf, (uint8_t)sym, (uint8_t)nbsym);
    }
    return sym;
}
int c1_ds_clear(c1_decstrm_t *ds) {
    c1ent_dec_exit(&ds->dec, c1ent_strstrm_read_bits16, ds->strstrm);
    free(ds->strstrm);
    c1_sptr_decref(&ds->sbuf);
    ds->sbuf = NULL;
    ds->strstrm = NULL;
    return 0;
}

static uint8_t c1_wr__ceil_log2(uint32_t x) {
    uint8_t i = 0;
    while (x > (1 << i))
        i++;
    return i;
}

int c1enc_wr_frame_header(const c1enc_frame_t *frm, const c1enc_ctx_t *ctx, int update_cdf, //
                          int ref_id) {
    c1_encstrm_t *es = ctx->enc;
    c1_cdf_ctx_t *cdfs = ctx->cdfs;
    // gather info
    uint8_t has_size_inf = frm->size_change_or_init;
    uint8_t has_bqi = 1;
    uint8_t bqi = frm->q_index;
    uint8_t frame_type = frm->frame_type;

    c1_es_write_sym(es, frm->size_change_or_init, cdfs->frame_has_sz_inf_cdf, C1_CDFCNT_FRAME_HAS_SZ, update_cdf);
    // todo: support ref bqi
    c1_es_write_sym(es, 1, cdfs->frame_has_bqi_cdf, C1_CDFCNT_FRAME_HAS_BQI, update_cdf);
    c1_es_write_sym(es, frm->frame_type, cdfs->frame_type, C1_CDFCNT_FRAME_TYPE, update_cdf);
    if (frm->frame_type == C1_FRAME_P) {
        c1_es_write_sym(es, ref_id, cdfs->frame_ref_id, C1_CDFCNT_FRAME_REF_ID, update_cdf);
    }
    if (has_size_inf) {
        const uint16_t h = frm->hgt, w = frm->wid;
        uint8_t nh = c1_wr__ceil_log2(h), nw = c1_wr__ceil_log2(w);
        nh -= (nh > 0), nw -= (nw > 0);
        assert(nh < 8 && nw < 8 && h > 0 && w > 0);
        c1_es_write_raw(es, 3, nh), c1_es_write_raw(es, nh + 1, h - 1);
        c1_es_write_raw(es, 3, nw), c1_es_write_raw(es, nw + 1, w - 1);
    }
    if (has_bqi) {
        uint8_t nbqi = c1_wr__ceil_log2(bqi);
        nbqi -= (nbqi > 0);
        c1_es_write_raw(es, 3, nbqi);
        c1_es_write_raw(es, nbqi + 1, bqi);
    }
    return 0;
}
int c1enc_wr_super_block(const c1enc_frame_t *frm, const c1enc_ctx_t *ctx, int update_cdf);