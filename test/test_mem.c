#include "src/util/log.h"
#include "src/util/mem.h"

#include <stdio.h>

static const int nptr = 100;

static c1_mpool_t test_pool = {.sz = 8, .nb = nptr/3, NULL};

int main() {
    void *ptrs[3 * nptr];
    for (int i = 0; i < 3 * nptr; i++) {
        ptrs[i] = c1_mpool_alloc(&test_pool);
        info("%d",c1_mpool_dbgcnt(&test_pool));
        assert_fatal(c1_mpool_dbgcnt(&test_pool) == i + 1);
    }
    for (int i = 3 * nptr - 1; i >= 0; i--) {
        c1_mpool_dealloc(&test_pool, ptrs[i]);
        assert_fatal(c1_mpool_dbgcnt(&test_pool) == i);
    }
    for (int i = 0; i < 3 * nptr; i++) {
        ptrs[i] = c1_mpool_alloc(&test_pool);
        assert_fatal(c1_mpool_dbgcnt(&test_pool) == i + 1);
    }
    for (int i = 0; i < 3 * nptr; i++) {
        c1_mpool_dealloc(&test_pool, ptrs[i]);
        assert_fatal(c1_mpool_dbgcnt(&test_pool) == 3 * nptr - i - 1);
    }
    for (int i = 0; i < 3 * nptr; i++) {
        ptrs[i] = c1_mpool_alloc(&test_pool);
        assert_fatal(c1_mpool_dbgcnt(&test_pool) == i + 1);
    }
    for (int i = nptr; i < 2 * nptr; i++) {
        c1_mpool_dealloc(&test_pool, ptrs[i]);
        // info("%d",c1_mpool_dbgcnt(&test_pool));
        assert_fatal(c1_mpool_dbgcnt(&test_pool) == 4 * nptr - i - 1);
    }
    for (int i = 2 * nptr; i < 3 * nptr; i++) {
        c1_mpool_dealloc(&test_pool, ptrs[i]);
        assert_fatal(c1_mpool_dbgcnt(&test_pool) == 4 * nptr - i - 1);
    }
    for (int i = 0; i < nptr; i++) {
        c1_mpool_dealloc(&test_pool, ptrs[i]);
        assert_fatal(c1_mpool_dbgcnt(&test_pool) == nptr - i - 1);
    }
    assert_fatal(test_pool.root_==NULL);
}