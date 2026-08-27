/* gc_stress.c — Phase 3 collector acceptance. */

#include "npl.h"
#include <stdio.h>

#define ALLOCATIONS 1000000
#define RETAIN_EVERY 10000

int main(void)
{
    npl_runtime_init();

    NplVal retained = npl_nil();
    NplVal garbage = npl_nil();
    npl_gc_root(&retained);

    for (int i = 0; i < ALLOCATIONS; i++) {
        garbage = npl_cons(npl_fixnum(i), npl_nil());
        if (i % RETAIN_EVERY == 0)
            retained = npl_cons(npl_fixnum(i), retained);
    }

    /* Keep the final temporary conservative root intentional and collect. */
    npl_gc_collect();

    int retained_count = 0;
    for (NplVal it = retained; !npl_is_nil(it); it = npl_cdr(it))
        retained_count++;

    size_t allocated = npl_gc_allocated();
    size_t live = npl_gc_live_objects();
    size_t collections = npl_gc_collections();

    if (retained_count != ALLOCATIONS / RETAIN_EVERY) {
        fprintf(stderr, "gc stress: retained list corrupted (%d)\n", retained_count);
        return 1;
    }
    if (collections == 0) {
        fprintf(stderr, "gc stress: automatic collection never ran\n");
        return 1;
    }
    if (allocated >= 4u * 1024u * 1024u || live >= 50000u) {
        fprintf(stderr, "gc stress: heap not reclaimed (bytes=%zu live=%zu)\n",
                allocated, live);
        return 1;
    }

    npl_gc_unroot(&retained);
    printf("PASS: 1000000 allocations, %zu collections, %zu live objects, %zu bytes\n",
           collections, live, allocated);
    (void)garbage;
    return 0;
}
