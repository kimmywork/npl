/*
 * alloc.c — NPL runtime memory allocator.
 *
 * Uses malloc for Phase 3 (mmap can be added later).
 * Provides allocation for boxed values (pairs, strings, vectors, floats).
 *
 * Memory model:
 *   - All allocations go through npl_alloc()
 *   - GC handles deallocation via mark-sweep
 *   - Free list for recycled objects
 */

#include "npl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Global state ---- */

/* Head of allocated object list (for GC traversal) */
static NplHeader *g_alloc_head = NULL;

/* GC thresholds */
static size_t g_allocated = 0;
static size_t g_threshold = 1024 * 1024;  /* 1MB initial threshold */

/* GC roots */
#define MAX_ROOTS 1024
static NplVal *g_roots[MAX_ROOTS];
static int g_root_count = 0;

/* ---- Internal allocation ---- */

static void *npl_alloc(size_t size) {
    void *ptr = calloc(1, size);
    if (!ptr) {
        fprintf(stderr, "npl_alloc: out of memory\n");
        exit(1);
    }
    g_allocated += size;
    return ptr;
}

/* ---- Runtime initialization ---- */

void npl_runtime_init(void) {
    g_alloc_head = NULL;
    g_allocated = 0;
    g_threshold = 1024 * 1024;
    g_root_count = 0;
}

/* ---- Value constructors ---- */

NplVal npl_cons(NplVal car, NplVal cdr) {
    NplPair *p = (NplPair *)npl_alloc(sizeof(NplPair));
    p->header.tag = BOX_PAIR;
    p->header.flags = 0;
    p->car = car;
    p->cdr = cdr;

    /* Link into allocation list */
    p->header.flags = (uint32_t)(uintptr_t)g_alloc_head;
    g_alloc_head = &p->header;

    return (NplVal)(uintptr_t)p | TAG_PTR;
}

NplVal npl_string(const char *s, uint32_t len) {
    size_t size = sizeof(NplString) + len + 1;
    NplString *str = (NplString *)npl_alloc(size);
    str->header.tag = BOX_STRING;
    str->header.flags = 0;
    str->length = len;
    str->capacity = len;
    if (s) {
        memcpy(str->data, s, len);
    }
    str->data[len] = '\0';

    /* Link into allocation list */
    str->header.flags = (uint32_t)(uintptr_t)g_alloc_head;
    g_alloc_head = &str->header;

    return (NplVal)(uintptr_t)str | TAG_PTR;
}

NplVal npl_float(double v) {
    NplFloat *f = (NplFloat *)npl_alloc(sizeof(NplFloat));
    f->header.tag = BOX_FLOAT;
    f->header.flags = 0;
    f->value = v;

    /* Link into allocation list */
    f->header.flags = (uint32_t)(uintptr_t)g_alloc_head;
    g_alloc_head = &f->header;

    return (NplVal)(uintptr_t)f | TAG_PTR;
}

/* ---- Value accessors ---- */

NplVal npl_car(NplVal v) {
    if (!npl_is_ptr(v)) {
        fprintf(stderr, "npl_car: not a pair\n");
        return npl_nil();
    }
    NplPair *p = npl_pair_ptr(v);
    if (p->header.tag != BOX_PAIR) {
        fprintf(stderr, "npl_car: not a pair\n");
        return npl_nil();
    }
    return p->car;
}

NplVal npl_cdr(NplVal v) {
    if (!npl_is_ptr(v)) {
        fprintf(stderr, "npl_cdr: not a pair\n");
        return npl_nil();
    }
    NplPair *p = npl_pair_ptr(v);
    if (p->header.tag != BOX_PAIR) {
        fprintf(stderr, "npl_cdr: not a pair\n");
        return npl_nil();
    }
    return p->cdr;
}

void npl_set_car(NplVal pair, NplVal val) {
    if (!npl_is_ptr(pair)) return;
    NplPair *p = npl_pair_ptr(pair);
    if (p->header.tag != BOX_PAIR) return;
    p->car = val;
}

void npl_set_cdr(NplVal pair, NplVal val) {
    if (!npl_is_ptr(pair)) return;
    NplPair *p = npl_pair_ptr(pair);
    if (p->header.tag != BOX_PAIR) return;
    p->cdr = val;
}

/* ---- GC root management ---- */

void npl_gc_root(NplVal *root) {
    if (g_root_count < MAX_ROOTS) {
        g_roots[g_root_count++] = root;
    }
}

void npl_gc_unroot(NplVal *root) {
    for (int i = 0; i < g_root_count; i++) {
        if (g_roots[i] == root) {
            g_roots[i] = g_roots[--g_root_count];
            return;
        }
    }
}

/* ---- GC stats ---- */

size_t npl_gc_allocated(void) {
    return g_allocated;
}

size_t npl_gc_threshold(void) {
    return g_threshold;
}

/* ---- Display (for debugging) ---- */

void npl_display(NplVal v) {
    if (npl_is_fixnum(v)) {
        printf("%lld", (long long)npl_fixnum_value(v));
    } else if (npl_is_nil(v)) {
        printf("()");
    } else if (v == npl_true()) {
        printf("#t");
    } else if (v == npl_false()) {
        printf("#f");
    } else if (npl_is_ptr(v)) {
        NplHeader *h = npl_header(v);
        switch (h->tag) {
        case BOX_PAIR: {
            NplPair *p = (NplPair *)h;
            printf("(");
            npl_display(p->car);
            NplVal rest = p->cdr;
            while (!npl_is_nil(rest) && npl_is_ptr(rest) && npl_box_tag(rest) == BOX_PAIR) {
                printf(" ");
                npl_display(npl_car(rest));
                rest = npl_cdr(rest);
            }
            if (!npl_is_nil(rest)) {
                printf(" . ");
                npl_display(rest);
            }
            printf(")");
            break;
        }
        case BOX_STRING: {
            NplString *s = (NplString *)h;
            /* R7RS display prints strings without quotes */
            printf("%.*s", (int)s->length, s->data);
            break;
        }
        case BOX_SYMBOL: {
            NplSymbolLike *s = (NplSymbolLike *)h;
            printf("%.*s", (int)s->length, s->data);
            break;
        }
        case BOX_FLOAT: {
            NplFloat *f = (NplFloat *)h;
            printf("%g", f->value);
            break;
        }
        default:
            printf("<unknown>");
        }
    } else {
        printf("<unknown>");
    }
}

void npl_newline(void) {
    printf("\n");
}

/* ---- Helper: fixnum to string ---- */

char *npl_fixnum_to_string(int64_t v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)v);
    return strdup(buf);
}
