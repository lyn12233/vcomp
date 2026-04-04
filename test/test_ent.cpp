#include "src/math/entcoder.h"
#include "src/util/log.h"
#include "src/util/mem.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>
#include <iostream>

#include "entenc.cpp"

// Simple test example
TEST(EntropyEncoder, EncodeIntegrity_1) {
    const int Nitem = 100;
    const int Niter = 100;
    uint16_t cdf[4] = {1, 12678, 1 << 15, 0};
    uint16_t icdf[4] = {(1 << 15) - 1, (1 << 15) - 12678, 0, 0};

    uint8_t *data = (uint8_t *)malloc(Nitem);
    srand(1145);

    for (int iter = 0; iter < Niter; iter++) {

        for (int i = 0; i < Nitem; i++) {
            data[i] = rand() % 3;
        }

        c1ent_enc_t enc;
        ASSERT_FALSE(c1ent_enc_init(&enc, 0));
        FILE *fp = fopen("logent1.txt", "w");
        for (int i = 0; i < Nitem; i++) {
            EXPECT_FALSE(c1ent_encode_cdf(&enc, data[i], cdf, 3));
            c1ent_enc_repr(&enc, fp);
        }
        uint32_t sz;
        uint8_t *out = c1ent_enc_done(&enc, &sz);
        ASSERT_TRUE(out);
        // info("encode result:");
        // c1_dump_buf(out, sz);

        od_ec_enc enc2;
        od_ec_enc_init(&enc2, 1);
        ASSERT_EQ(enc2.error, 0);
        freopen("logent2.txt", "w", fp);
        for (int i = 0; i < Nitem; i++) {
            od_ec_encode_cdf_q15(&enc2, data[i], icdf, 3);
            fprintf(fp, "Encoder(bufferSize=%u,PrecarrySize=%u,offset=%u,low=%08x,rng=%04x,cnt=%d)\n", //
                    enc2.storage, enc2.precarry_storage, enc2.offs, enc2.low, enc2.rng, enc2.cnt);
        }
        uint32_t sz2;
        uint8_t *out2 = od_ec_enc_done(&enc2, &sz2);
        ASSERT_TRUE(out2);
        // info("encode result(canonical):");
        // c1_dump_buf(out, sz);

        fclose(fp);
        ASSERT_TRUE(sz == sz2);
        ASSERT_FALSE(memcmp(out, out2, sz));

        ASSERT_FALSE(c1ent_enc_clear(&enc));
        od_ec_enc_clear(&enc2);

        info("iter %d done", iter);
    }

    free(data);
}

TEST(EntropyEncoder, StrStrmIntegrity) {
    uint8_t data[100];
    for (int i = 0; i < 100; i++) {
        data[i] = rand();
    }
    c1ent_strstrm_t strm = {data, 800, 0};
    for (int i = 0; i < 100; i += 2) {
        int val = c1ent_strstrm_read_bits16(&strm, 16);
        // info("%02x %02x %04x", data[i], data[i + 1], val);
        ASSERT_GE(val, 0);
        ASSERT_EQ(val >> 8, data[i]);
        ASSERT_EQ(val & 0xff, data[i + 1]);
    }
}

TEST(EntropyEncoder, DecodeIntegrity_1) {
    const int Nitem = 100;
    const int Niter = 100;
    uint16_t cdf[5] = {100, 5000, 12678, 1 << 15, 0};

    uint8_t *data = (uint8_t *)malloc(Nitem);
    srand(114514);
    for (int iter = 0; iter < Niter; iter++) {
        for (int i = 0; i < Nitem; i++) {
            data[i] = rand() % 4;
            // info("input symbol %d: %d", i, data[i]);
        }
        c1ent_enc_t enc;
        ASSERT_FALSE(c1ent_enc_init(&enc, 0));
        for (int i = 0; i < Nitem; i++) {
            ASSERT_FALSE(c1ent_encode_cdf(&enc, data[i], cdf, 4));
        }
        c1ent_strstrm_t strm = {0};
        ASSERT_TRUE(strm.data = c1ent_enc_done(&enc, &strm.sz));
        strm.sz *= 8;
        c1ent_dec_t dec;
        ASSERT_GE(c1ent_dec_init(&dec, strm.sz / 8, c1ent_strstrm_read_bits16, &strm), 0);
        for (int i = 0; i < Nitem; i++) {
            int sym = c1ent_decode_cdf(&dec, c1ent_strstrm_read_bits16, &strm, cdf, 4);
            // info("output symbol %d: %d", i, sym);
            ASSERT_EQ(sym, data[i]);
        }
        c1ent_enc_clear(&enc);
        info("iter %d done", iter);
    }
    free(data);
}

static int comp_16(const void *a, const void *b) {
    uint16_t ia = *(const uint16_t *)a;
    uint16_t ib = *(const uint16_t *)b;
    return (ia > ib) - (ia < ib);
}
static int is_valid_cdf(uint16_t cdf[16], int nbsym) {
    if (nbsym < 0 || nbsym > 16)
        return -1;
    for (int i = 0; i < nbsym - 1; i++) {
        if (cdf[i] > cdf[i + 1])
            return -2;
    }
    if (cdf[nbsym - 1] != (1 << 15))
        return -3;
    if (cdf[nbsym] != 0)
        return -4;
    return 0;
}

TEST(EntropyEncoder, Compound_1) {
    const unsigned Ntp = 20;   // types of symbols
    const unsigned Nsym = 100; // number of symbols to encode
    typedef uint16_t cdf_t[16];
    cdf_t *cdfs = (cdf_t *)malloc(sizeof(cdf_t) * Ntp);
    memset(cdfs, 0, sizeof(cdf_t) * Ntp);
    uint32_t *symseq = (uint32_t *)malloc(sizeof(int) * Nsym); // type sequence
    uint32_t *symidx = (uint32_t *)malloc(sizeof(int) * Nsym); // sym idx sequence, source to encode
    uint32_t *symnbs = (uint32_t *)malloc(sizeof(int) * Ntp);

    srand(114);
    for (int i = 0; i < Ntp; i++) {
        symnbs[i] = std::clamp(rand() % 16, 2, 16);
        for (int j = 0; j < symnbs[i] - 1; j++) {
            cdfs[i][j] = std::clamp(rand() % (1 << 15), 0, (1 << 15));
        }
        qsort(cdfs[i], symnbs[i] - 1, sizeof(uint16_t), comp_16);
        cdfs[i][symnbs[i] - 1] = (1 << 15);
        cdfs[i][symnbs[i]] = 0;
    }
    for (int i = 0; i < Nsym; i++) {
        symseq[i] = (uint32_t)rand() % Ntp;
        symidx[i] = (uint32_t)rand() % symnbs[symseq[i]];
    }
    for (int i = 0; i < Ntp; i++) {
        ASSERT_EQ(is_valid_cdf(cdfs[i], symnbs[i]), 0);
    }

    c1ent_enc_t enc;
    c1ent_enc_init(&enc, 1);
    for (int i = 0; i < Nsym; i++) {
        int r = c1ent_encode_cdf(&enc, symidx[i], cdfs[symseq[i]], symnbs[symseq[i]]);
        ASSERT_EQ(r, 0);
    }
    c1ent_strstrm_t strm = {0};
    ASSERT_TRUE(strm.data = c1ent_enc_done(&enc, &strm.sz));
    strm.sz *= 8;
    c1ent_dec_t dec;
    ASSERT_GE(c1ent_dec_init(&dec, strm.sz / 8, c1ent_strstrm_read_bits16, &strm), 0);
    for (int i = 0; i < Nsym; i++) {
        int sym = c1ent_decode_cdf(&dec, c1ent_strstrm_read_bits16, &strm, cdfs[symseq[i]], symnbs[symseq[i]]);
        // info("output symbol %d: %d", i, sym);
        ASSERT_EQ(sym, symidx[i]);
    }
    c1ent_enc_clear(&enc);
    free(symnbs),free(symidx),free(symseq),free(cdfs);
}
