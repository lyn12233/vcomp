#include "src/math/entcoder.h"
#include "src/util/log.h"
#include "src/util/mem.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>
#include <iostream>

#include "entenc.cpp"

// Simple test example
TEST(EntropyEncoder, BasicTest) {
    const int Nitem = 1000;
    const int Niter = 1000;
    uint16_t cdf[4] = {1, 12678, 1 << 15, 0};
    uint16_t icdf[4] = {(1 << 15) - 1, (1 << 15) - 12678, 0, 0};

    uint8_t *data = (uint8_t *)malloc(Nitem);

    for (int iter = 0; iter < Niter; iter++) {

        srand(1145);
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

TEST(ExampleTest, AnotherTest) {
    EXPECT_TRUE(true);
}
