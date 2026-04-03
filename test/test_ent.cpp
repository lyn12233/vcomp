#include "src/math/entcoder.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>

// Simple test example
TEST(EntropyEncoder, BasicTest) {
    EXPECT_EQ(1, 1);
    c1ent_enc_t enc;
    EXPECT_FALSE(c1ent_enc_init(&enc, 0));
    uint16_t cdf[4] = {1, 10, 1 << 15, 0};
    EXPECT_FALSE(c1ent_encode_cdf(&enc, 1, cdf, 3));
}

TEST(ExampleTest, AnotherTest) {
    EXPECT_TRUE(true);
}
