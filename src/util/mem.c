#include "mem.h"
#include "log.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

c1_mpool_t c1_mpool_defs[9] = {
    {1, 220, NULL}, {2, 110, NULL}, {4, 120, NULL},  {8, 60, NULL},  {16, 30, NULL},
    {32, 15, NULL}, {64, 15, NULL}, {128, 15, NULL}, {256, 7, NULL},
};

typedef struct c1_mpool__node_s {
    struct c1_mpool__node_s *next;
    /* for 2**nblog2 element, data[] first contains memories to be allocated, then
    p->nb bit to mask occupation, mask 1 for occ, 0 for free slots
    this is cache unfriendly but easy to impl and align
    */
    uint8_t data[];
} c1_mpool__node_t;

static int c1_mpool__msksz(const c1_mpool_t *p) {
    int bits = p->nb;
    return bits / 8 + (bits % 8 != 0);
}

static int c1_mpool__freeidx(const c1_mpool_t *p, c1_mpool__node_t *n) {
    uint8_t *msk = n->data + (int)p->sz * p->nb;
    for (int i = 0; i < p->nb; i += 8) {
        if (msk[i / 8] != 0xff) {
            uint8_t bytemsk = 1;
            while ((msk[i / 8] & bytemsk) && i < p->nb) {
                bytemsk <<= 1, i++;
            }
            return i;
        }
    }
    return p->nb;
}

static int c1_mpool__node_isempty(const c1_mpool_t *p, c1_mpool__node_t *n) {
    uint8_t *msk = n->data + (int)p->sz * p->nb;
    for (int i = 0; i < p->nb; i += 8) {
        if (msk[i / 8]) {
            return 0;
        }
    }
    return 1;
}

static int c1_mpool__dbgncnt(const c1_mpool_t *p, const c1_mpool__node_t *n) {
    const uint8_t *msk = n->data + (int)p->sz * p->nb;
    int cnt = 0;
    for (int i = 0; i < p->nb; i++) {
        if (msk[i / 8] & (1 << (i % 8)))
            cnt++;
    }
    return cnt;
}

void *c1_mpool_alloc(c1_mpool_t *p) {
    c1_mpool__node_t *n = (c1_mpool__node_t *)(p->root_);
    c1_mpool__node_t *prev = NULL;
    while (n && c1_mpool__freeidx(p, n) >= p->nb) {
        prev = n;
        n = n->next;
    }

    // no empty slot, extend the list
    if (!n) {
        // debug("mpool extend list");
        c1_mpool__node_t **lnk = prev ? &prev->next : (c1_mpool__node_t **)&p->root_;
        const size_t required_bytes = sizeof(c1_mpool__node_t) + (int)p->sz * p->nb + c1_mpool__msksz(p);
        if (required_bytes > 4096) {
            warning2("undesired %ux%u", p->sz, p->nb);
        }
        *lnk = (c1_mpool__node_t *)malloc(required_bytes);
        assert_fatal(*lnk);
        n = *lnk;
        n->next = NULL;
        memset(n->data + (int)p->sz * p->nb, 0, c1_mpool__msksz(p));
    }

    // find first available offs
    int offs = c1_mpool__freeidx(p, n);
    // debug("mpool found free idx %d at node %p", offs, n);
    // set bit msk
    uint8_t *msk = n->data + (int)p->sz * p->nb;
    uint8_t bytemsk = (uint8_t)(1 << (offs % 8));
    msk[offs / 8] |= bytemsk;

    return n->data + offs * p->sz;
}

int c1_mpool_dealloc(c1_mpool_t *p, void *buf) {
    c1_mpool__node_t *n = (c1_mpool__node_t *)(p->root_);
    c1_mpool__node_t *prev = NULL;
    uint8_t *pbuf = (uint8_t *)buf;
    while (n && (n->data > pbuf || n->data + (int)p->sz * p->nb <= pbuf)) {
        prev = n;
        n = n->next;
    }

    // invalid: node not found or buf not aligned to sz
    if (!n || (pbuf - n->data) % p->sz != 0) {
        fatal2("invalid ptr: node not found or not aligned");
        return -1;
    }
    // try to unset msk
    size_t offs = (pbuf - n->data) / p->sz;
    uint8_t *msk = n->data + (int)p->sz * p->nb;
    uint8_t bytemsk = (uint8_t)(1 << (offs % 8));
    if (!(msk[offs / 8] & bytemsk)) {
        fatal2("try to free a unoccupied space");
        return -1;
    }
    msk[offs / 8] &= ~bytemsk;
    // debug("mpool free idx %zu at node %p", offs, n);

    // try to del node if empty
    if (c1_mpool__node_isempty(p, n)) {
        c1_mpool__node_t **lnk = prev ? &prev->next : (c1_mpool__node_t **)&p->root_;
        c1_mpool__node_t *next = n->next;
        if (next != NULL) {
            *lnk = next;
            free(n);
            // debug("mpool free node %p", n);
        } else {
            // nop, do not free the last node constantly
        }
    }

    return 0;
}

int c1_mpool_dbgcnt(const c1_mpool_t *p) {
    const c1_mpool__node_t *n = p->root_;
    int cnt = 0;
    while (n) {
        cnt += c1_mpool__dbgncnt(p, n);
        n = n->next;
    }
    return cnt;
}

#define C1_SPTR_POOLNB 64
#define C1_SPTR_CNTMAX 1024

static c1_mpool_t c1_sptr__pool = {sizeof(c1_sptr_t), C1_SPTR_POOLNB, NULL};

c1_sptr_t *c1_sptr_create(void *ptr, void (*dtor)(void *)) {
    c1_sptr_t *p = (c1_sptr_t *)c1_mpool_alloc(&c1_sptr__pool);
    *p = (c1_sptr_t){ptr, dtor, 1};
    return p;
}
int c1_sptr_incref(c1_sptr_t **p) {
    if ((*p) && (*p)->cnt_ < C1_SPTR_CNTMAX && (*p)->cnt_ > 0) {
        (*p)->cnt_++;
        return 0;
    }
    return -1;
}
int c1_sptr_decref(c1_sptr_t **p) {
    if ((*p) && (*p)->cnt_ > 0) {
        if ((*p)->cnt_ < C1_SPTR_CNTMAX)
            (*p)->cnt_--;
        if ((*p)->cnt_ == 0) {
            // debug("sptr count to 0");
            (*p)->dtor_((*p)->ptr);
            c1_mpool_dealloc(&c1_sptr__pool, *p);
            *p = NULL;
        }
        return 0;
    }
    warning2("invalid cnt/ptr");
    return -1;
}

void c1_dump_buf(void *buf, uint32_t sz) {
    c1_dump_buf_f(stdout, buf, sz);
}

void c1_dump_buf_f(FILE *fp, void *data, uint32_t len) {
    info_("\033[38;5;10mbuffer[%u]:\033[0m\r\n", len);
    const uint8_t *p = data;
    for (size_t i = 0; i < len; i += 16) {
        fprintf(fp, "%.4u:", (int)i);
        for (size_t j = i; j < i + 16; j++) {
            if (j < len)
                fprintf(fp, "%02x ", p[j]);
            else
                fprintf(fp, "   ");
        }
        fprintf(fp, " ");
        for (size_t j = i; j < i + 16 && j < len; j++) {
            if (p[j] < 128 && isprint(p[j])) {
                fprintf(fp, "%c", p[j]);
            } else {
                fprintf(fp, ".");
            }
        }
        fprintf(fp, "\r\n");
    }
}