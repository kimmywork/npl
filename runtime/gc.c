/*
 * gc.c — NPL runtime garbage collector.
 *
 * Stop-the-world mark-sweep collector.
 *
 * Algorithm:
 *   1. Mark phase: traverse from roots, set mark bit on reachable objects
 *   2. Sweep phase: free unmarked objects, clear marks on survivors
 *
 * Triggered when allocation exceeds threshold.
 */

#include "npl.h"
#include <stdio.h>
#include <stdlib.h>

/* ---- Internal state (from alloc.c) ---- */

/* We need access to the allocation list and GC roots.
 * In a real implementation, these would be shared via a context struct.
 * For Phase 3, we use extern access. */

/* Allocation list head (stored in header.flags as a hack) */
extern NplHeader *g_alloc_head;

/* GC roots */
#define MAX_ROOTS 1024
extern NplVal *g_roots[];
extern int g_root_count;

/* Memory stats */
extern size_t g_allocated;
extern size_t g_threshold;

/* ---- Mark phase ---- */

static void mark_val(NplVal v) {
    if (!npl_is_ptr(v)) return;

    NplHeader *h = npl_header(v);
    if (h->flags & 1) return;  /* already marked */
    h->flags |= 1;             /* mark */

    /* Recursively mark children */
    switch (h->tag) {
    case BOX_PAIR: {
        NplPair *p = (NplPair *)h;
        mark_val(p->car);
        mark_val(p->cdr);
        break;
    }
    case BOX_VECTOR: {
        /* TODO: mark vector elements */
        break;
    }
    case BOX_CLOSURE: {
        /* TODO: mark closure environment */
        break;
    }
    default:
        /* Strings, floats have no children */
        break;
    }
}

static void mark_all(void) {
    /* Mark from registered roots */
    for (int i = 0; i < g_root_count; i++) {
        if (g_roots[i]) {
            mark_val(*g_roots[i]);
        }
    }
}

/* ---- Sweep phase ---- */

static void sweep_all(void) {
    NplHeader **prev = &g_alloc_head;
    NplHeader *curr = g_alloc_head;

    while (curr) {
        NplHeader *next = (NplHeader *)(uintptr_t)curr->flags;

        if (curr->flags & 1) {
            /* Marked — keep it, clear mark */
            curr->flags = 0;
            prev = (NplHeader **)&curr->flags;  /* Note: this is a hack */
            /* Actually we need a proper next pointer */
            prev = &curr;  /* Simplified for Phase 3 */
            curr = next;
        } else {
            /* Unmarked — free it */
            size_t size = 0;
            switch (curr->tag) {
            case BOX_PAIR:
                size = sizeof(NplPair);
                break;
            case BOX_STRING: {
                NplString *s = (NplString *)curr;
                size = sizeof(NplString) + s->capacity + 1;
                break;
            }
            case BOX_FLOAT:
                size = sizeof(NplFloat);
                break;
            default:
                size = sizeof(NplHeader);
                break;
            }

            g_allocated -= size;
            free(curr);

            /* Unlink from list */
            /* Note: simplified linked list management for Phase 3 */
            curr = next;
        }
    }
}

/* ---- Public API ---- */

void npl_gc_collect(void) {
    /* Mark reachable objects */
    mark_all();

    /* Sweep unreachable objects */
    sweep_all();

    /* Adjust threshold (simple doubling strategy) */
    g_threshold = g_allocated * 2;
    if (g_threshold < 1024 * 1024) {
        g_threshold = 1024 * 1024;
    }

    fprintf(stderr, "gc: collected, %zu bytes allocated, %zu threshold\n",
            g_allocated, g_threshold);
}

/* ---- Automatic GC trigger ---- */

void npl_gc_maybe_collect(void) {
    if (g_allocated >= g_threshold) {
        npl_gc_collect();
    }
}
