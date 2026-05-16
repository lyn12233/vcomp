#include "entcoder.h"
#include "util/log.h"

#include <stdint.h>
#include <stdlib.h>

#define C1ENT_PROB_SHIFT 6
#define C1ENT_MIN_PROB 4

static int8_t c1ent__floorlog2(uint16_t x) {
    for (int8_t i = 15; i >= 0; i--) {
        if (x & (1 << i))
            return i;
    }
    return -1; // to ensure leading_zeros(x)=15-floorlog2(x)
}
static int c1ent__min(int a, int b) {
    return a < b ? a : b;
}
static int c1ent__max(int a, int b) {
    return a > b ? a : b;
}

int c1ent_enc_init(c1ent_enc_t *enc, uint32_t sz) {
    void *buf, *pbuf;
    sz = sz == 0 ? 1 : sz;
    if ((buf = malloc(sizeof(uint8_t) * sz)) == NULL || //
        (pbuf = malloc(sizeof(uint16_t) * sz)) == NULL) {
        warning("malloc failed");
        return -1;
    }
    *enc = (c1ent_enc_t){buf, pbuf, sz, sz, 0, 0, 1 << 15, -9};
    return 0;
}

int c1ent_encode_cdf(c1ent_enc_t *enc, int sym, const uint16_t *cdf, int nbsym) {
    // refer to [libaom]/aom_dsp/entenc.c:156 od_ec_encode_q15
    // symbol's fraction (fl,fh], measured by dist to highest cdf
    uint32_t fl = sym > 0 ? (1 << 15) - cdf[sym - 1] : (1 << 15);
    uint32_t fh = (1 << 15) - cdf[sym];
    uint32_t low = enc->low;
    uint16_t rng = enc->rng;
    assert_fatal(rng >= (1 << 15) && fl >= fh && fl <= (1 << 15));
    uint32_t u, v;
    if (fl < (1 << 15)) {
        u = ((rng >> 8) * (uint32_t)(fl >> C1ENT_PROB_SHIFT) >> (7 - C1ENT_PROB_SHIFT))
          + C1ENT_MIN_PROB * (nbsym - sym);
        v = ((rng >> 8) * (uint32_t)(fh >> C1ENT_PROB_SHIFT) >> (7 - C1ENT_PROB_SHIFT))
          + C1ENT_MIN_PROB * (nbsym - 1 - sym);
        low += rng - u, rng = (uint16_t)(u - v);
    } else {
        // first symbol fl=1<<5, l not changed, u=rng
        rng -= ((rng >> 8) * (uint32_t)(fh >> C1ENT_PROB_SHIFT) >> (7 - C1ENT_PROB_SHIFT))
             + C1ENT_MIN_PROB * (nbsym - 1 - sym);
    }
    // normalize the range to >=1>>15, write low's significant bits.
    // refer to entenc.c:56 od_ec_enc_normalize
    int16_t d = (int16_t)15 - c1ent__floorlog2(rng);
    int16_t c = enc->cnt; // room for output?
    int16_t s = c + d;    // shifted cnt
    if (s >= 0) {
        // extend 2 slots(32bits) for output
        if (enc->offs + 2 > enc->precarry_sz) {
            uint32_t sz2 = enc->precarry_sz * 2 + 2;
            if (!(enc->precarry_buf = realloc(enc->precarry_buf, sizeof(uint16_t) * sz2))) {
                return -1;
            }
            enc->precarry_sz = sz2;
        }
        // c += 16; // 16 effective bits
        uint32_t msk = (1 << (c + 16)) - 1;
        if (s >= 8) { // needs to write twice
            // output low's bits above c+16(include)
            enc->precarry_buf[enc->offs++] = (uint16_t)(low >> (c + 16));
            low &= msk;
            // re-calculate c and msk
            c -= 8, msk >>= 8;
        }
        // output low's bits above c+16(include)
        enc->precarry_buf[enc->offs++] = (uint16_t)(low >> (c + 16));
        low &= msk;
        // new cnt, which assures less than 0
        s = c + d - 8;
    }
    // shift rng and low, update cnt
    enc->low = low << d, enc->rng = (uint16_t)(rng << d), enc->cnt = s;
    return 0;
}

uint8_t *c1ent_enc_done(c1ent_enc_t *enc, uint32_t *nbytes) {
    // end bits mask to round up low to [32]...1[14]0...0[0],
    // enough to distinguish the last rng (>=1<<15)
    uint32_t emsk = 0x3fff;
    // end bits
    int e = ((enc->low + emsk) & ~emsk) | (emsk + 1);
    int c = enc->cnt, s = enc->cnt + 10; //?

    // move minimum number of end bits to precarry_buf
    // should include 14th bit, worst case [14..7], thus until c+16<7
    if (s > 0) {
        // expand storage to hold ((c+9)/8+1) bytes
        if (enc->offs + ((s + 7) >> 3) > enc->precarry_sz) {
            uint32_t sz2 = enc->precarry_sz * 2 + ((s + 7) >> 3);
            if (!(enc->precarry_buf = realloc(enc->precarry_buf, sizeof(uint16_t) * sz2))) {
                return NULL;
            }
            enc->precarry_sz = sz2;
        }

        uint32_t msk = (1 << (c + 16)) - 1;
        do {
            enc->precarry_buf[enc->offs++] = (uint16_t)(e >> (c + 16));
            e &= msk;
            s -= 8, c -= 8;
            msk >>= 8;
        } while (s > 0); // c>=-9
    }

    // reserve bits for buf and propagate from prcarry_buf
    int carry = c1ent__max((s + 7) >> 3, 0);
    uint32_t offs = enc->offs;
    if (offs + carry > enc->buf_sz) {
        uint32_t sz2 = offs + carry;
        if (!(enc->buf = realloc(enc->buf, sizeof(uint8_t) * sz2))) {
            warning("malloc failed");
            return NULL;
        }
        enc->buf_sz = sz2;
    }
    uint8_t *out = enc->buf + enc->buf_sz - offs;
    *nbytes = offs, carry = 0;
    while (offs > 0) {
        offs--;
        carry += enc->precarry_buf[offs];
        out[offs] = (uint8_t)carry;
        carry >>= 8;
    }

    return out;
}

int c1ent_enc_clear(c1ent_enc_t *enc) {
    int res = 0;
    if (enc->buf) {
        free(enc->buf);
        enc->buf_sz = 0;
        enc->buf = NULL;
    } else {
        warning2("enc->buf is null ptr");
        res = -1;
    }
    if (enc->precarry_buf) {
        free(enc->precarry_buf);
        enc->precarry_sz = 0;
        enc->precarry_buf = NULL;
    } else {
        warning2("enc->precarry_buf is null ptr");
        res = -1;
    }
    return res;
}

// --- decoder part

int c1ent_dec_init(c1ent_dec_t *dec, uint32_t sz, //
                   int (*read_bits)(void *ctx, uint32_t bits), void *ctx) {
    assert_fatal(sz > 0);
    /* refer to av1 spec 8.2.2:
        - numBits = Min(sz*8,15)
        - buf is read using f(numBits) from MSB-first bitstream
        - paddedBuf = (buf<<(15-numBits))
        - SymVal = ((1<<15)-1)^paddedBuf    # 32767-paddedbits, dist from highest range of arithmetic coding
        - SymRng = 1<<15
        - SymMaxBits = sz*8-15              # desired bits to read, but without currently read bits(8 or 15)
    */
    dec->num_bits = (sz == 1) ? 8 : 15;
    if ((dec->buf = (uint16_t)read_bits(ctx, dec->num_bits)) < 0) {
        warning2("cant read %d bits", dec->num_bits);
        return -1;
    }
    dec->padded_buf = (uint16_t)((sz == 1) ? dec->buf << 7 : dec->buf);
    dec->val = ((1 << 15) - 1) ^ dec->padded_buf;
    dec->rng = 1 << 15;
    dec->max_bits = sz * 8 - 15;
    return 0;
}

int c1ent_dec_exit(c1ent_dec_t *dec, //
                   int (*read_bits)(void *ctx, uint32_t bits), void *ctx) {
    /* refer to av1 spec 8.2.4:
        - check SymMaxBits >= -14
        - trailingPos = bitstream.pos - Min(15,SymMaxBits-15)
        - bitstream advance by Max(0,SymMaxBits)
        - paddingEndPos = bitstream.pos
        - assure bits from trailingPos to paddingEndPos(exclude) is 0
        this shows that entropy coding unit is <sz> bytes with 15 trailing zeros
     */
    if (dec->max_bits < -14) {
        warning2("max_bits=%d is invalid", dec->max_bits);
        return -1;
    }
    int bits = dec->max_bits > 0 ? dec->max_bits : 0;
    for (int i = 0; i < bits; i += 16) {
        int r = read_bits(ctx, i + 16 <= bits ? 16 : bits - i);
        if (r != 0) {
            warning2("expect zeros, but %d", r);
            return -1;
        }
    }
    return 0;
}

int c1ent_decode_cdf(c1ent_dec_t *dec,                                      //
                     int (*read_bits)(void *ctx, uint32_t bits), void *ctx, //
                     uint16_t *cdf, int nbsym) {
    assert_fatal(nbsym > 1 && nbsym <= 16 && cdf[nbsym - 1] == (1 << 15));
    // (1) find pos of sym from lowest index, result cur is the lower bound, prev the upper
    /* refer to av1 spec 8.2.6:
        - cur = SymRng
        - symbol = -1
        - do{
        - symbol++
        - prev = cur
        - f = (1<<15)-cdf[symbol]
        - cur = (SymRng>>8)*(f>>PROB_SHIFT)>>(7-PROB_SHIFT)
        - cur += MIN_PROB*(N-symbol-1)
        - }while(SymVal<cur)
        cur and prev is dist from highest arithmetic coding pos, and iter from lowest;
        finally SymVal falls in range [cur,prev)
    */
    uint32_t cur, prev = dec->rng, sym = 0;
    while (1) {
        // fraction from highest pos, in 1<<15 scale
        uint32_t fraction = (1 << 15) - cdf[sym];
        // cur is the current pos of sym, in <rng> scale
        // a swift x*y/(1<<15), also assuring rng is 64 bit aligned, leaving space for 16*MIN_PROB
        cur = (((dec->rng >> 8) * (fraction >> C1ENT_PROB_SHIFT)) >> (7 - C1ENT_PROB_SHIFT))
            + C1ENT_MIN_PROB * (nbsym - sym - 1);
        if (dec->val >= cur) {
            break;
        }
        prev = cur, sym++;
    }

    // (2) update decoder ctx to a new rng-val
    dec->rng = (uint16_t)(prev - cur), dec->val -= (uint16_t)cur;
    // renormalize rng-val via filling with new bits
    // unused bits for <rng>, need to add up effective bits to 15
    uint8_t bits = 15 - c1ent__floorlog2(dec->rng); // 0..15
    dec->rng <<= bits;
    // clip number of bits to read to (0, max_bits)
    dec->num_bits = (bits <= dec->max_bits) ? bits : (uint8_t)c1ent__max(dec->max_bits, 0);
    if ((dec->buf = (uint16_t)read_bits(ctx, dec->num_bits)) < 0) {
        warning2("cant read %d bits", dec->num_bits);
        return -1;
    }
    // for bits not read, fill with 0's
    dec->padded_buf = (uint16_t)(dec->buf << (bits - dec->num_bits));
    // symbol value scales up 2**bits, then minus padded_buf
    // symval is actually stored in an inverse style, measuring dist form highest cdf(WTF)
    dec->val = (uint16_t)(dec->padded_buf ^ (((dec->val + 1) << bits) - 1));
    dec->max_bits -= bits;
    return sym;
    // to update cdf, call update_cdf directly.
}

void c1ent_update_cdf(uint16_t *cdf, uint8_t sym, uint8_t nbsym) {
    assert_fatal(sym >= 0 && sym < nbsym);
    int rate = 3 + (cdf[nbsym] > 15) + (cdf[nbsym] > 31) + c1ent__min(c1ent__floorlog2(nbsym), 2);
    int tmp = 0;
    for (int i = 0; i < nbsym - 1; i++) {
        tmp = (i == sym) ? (1 << 15) : tmp;
        if (tmp < cdf[i]) {
            cdf[i] -= (cdf[i] - tmp) >> rate;
        } else {
            cdf[i] += (tmp - cdf[i]) >> rate;
        }
    }
    cdf[nbsym] += (cdf[nbsym] < 32);
}

static uint8_t c1ent__strstrm_get(c1ent_strstrm_t *strm, uint32_t offs) {
    uint8_t c = strm->data[offs / 8];
    return (c >> (7 - offs % 8)) & 1;
}

int c1ent_strstrm_read_bits16(void *ctx, uint32_t bits) {
    c1ent_strstrm_t *strm = (c1ent_strstrm_t *)ctx;
    if (bits > 16 || strm->offs > strm->sz || strm->sz - strm->offs < bits) {
        return -1;
    }
    int res = 0;
    for (int i = 0; i < bits; i++) {
        // info("value %d at bit %u", c1ent__strstrm_get(strm, strm->offs + i), strm->offs + i);
        res = (res << 1) + c1ent__strstrm_get(strm, strm->offs + i);
    }
    strm->offs += bits;
    return res;
}