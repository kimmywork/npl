/*
 * atomic_test.c — T3 acceptance for SRFI 230.
 *
 * Exercises the C substrate directly: flag/box/fxbox/pair identity and old
 * value results, pair snapshots, checked arithmetic boundaries, a contended
 * fixnum counter history, pointer publication, a concurrent collector, and
 * the atomic-object GC edges/barriers.  Run under
 * NPL_GC_NURSERY_BYTES=65536 NPL_GC_VERIFY=100 as well; the verification
 * catches an old atomic object holding a young pointer without a barrier.
 */
#define _POSIX_C_SOURCE 200809L

#include "npl.h"
#include "thread.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition, message)                                            \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "FAIL: %s\n", (message));                         \
            failures++;                                                       \
        }                                                                     \
    } while (0)

#define ORDER_SEQ npl_fixnum(4)
#define ORDER_RELEASE npl_fixnum(2)
#define ORDER_ACQUIRE npl_fixnum(1)

static NplVal value_at(NplVal values, uint32_t index)
{
    if (!npl_is_ptr(values) || npl_header(values)->tag != BOX_VALUES)
        return npl_nil();
    NplValues *packet = (NplValues *)npl_header(values);
    if (index >= packet->count) return npl_nil();
    return packet->data[index];
}

static int values_count(NplVal value)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_VALUES)
        return 1;
    return (int)((NplValues *)npl_header(value))->count;
}

/* ---- synchronization gate for concurrent phases ---- */

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int ready;
    int total;
    int go;
} Gate;

static void gate_init(Gate *gate, int total)
{
    pthread_mutex_init(&gate->mutex, NULL);
    pthread_cond_init(&gate->cond, NULL);
    gate->ready = 0;
    gate->total = total;
    gate->go = 0;
}

static void gate_wait(Gate *gate)
{
    pthread_mutex_lock(&gate->mutex);
    gate->ready++;
    pthread_cond_broadcast(&gate->cond);
    while (!gate->go)
        pthread_cond_wait(&gate->cond, &gate->mutex);
    pthread_mutex_unlock(&gate->mutex);
}

static void gate_open(Gate *gate)
{
    pthread_mutex_lock(&gate->mutex);
    while (gate->ready < gate->total)
        pthread_cond_wait(&gate->cond, &gate->mutex);
    gate->go = 1;
    pthread_cond_broadcast(&gate->cond);
    pthread_mutex_unlock(&gate->mutex);
}

static void gate_destroy(Gate *gate)
{
    pthread_mutex_destroy(&gate->mutex);
    pthread_cond_destroy(&gate->cond);
}

/* ---- scalar checks ---- */

static void check_flags(void)
{
    NplVal flag = npl_atomic_flag_make();
    npl_gc_root(&flag);
    CHECK(npl_atomic_flag_p(flag) == npl_true(), "atomic-flag?");
    CHECK(npl_atomic_flag_p(npl_fixnum(1)) == npl_false(),
          "atomic-flag? rejects a fixnum");
    CHECK(npl_atomic_flag_test_and_set(flag, ORDER_SEQ) == npl_false(),
          "flag starts clear");
    CHECK(npl_atomic_flag_test_and_set(flag, ORDER_SEQ) == npl_true(),
          "flag test-and-set returns previous state");
    (void)npl_atomic_flag_clear(flag, ORDER_SEQ);
    CHECK(npl_atomic_flag_test_and_set(flag, ORDER_SEQ) == npl_false(),
          "flag clear");
    npl_gc_unroot(&flag);
}

static void check_boxes(void)
{
    NplVal first = npl_cons(npl_fixnum(1), npl_fixnum(2));
    NplVal equal_not_eq = npl_cons(npl_fixnum(1), npl_fixnum(2));
    npl_gc_root(&first);
    npl_gc_root(&equal_not_eq);
    NplVal box = npl_atomic_box_make(first);
    npl_gc_root(&box);
    CHECK(npl_atomic_box_p(box) == npl_true(), "atomic-box?");
    CHECK(npl_atomic_box_ref(box, ORDER_SEQ) == first, "box ref");
    NplVal swapped = npl_atomic_box_swap(box, equal_not_eq, ORDER_SEQ);
    CHECK(swapped == first, "box swap returns previous");
    CHECK(npl_atomic_box_ref(box, ORDER_SEQ) == equal_not_eq,
          "box holds the new identity");
    CHECK(npl_atomic_box_compare_and_swap(box, first, npl_fixnum(3),
                                          ORDER_SEQ) == equal_not_eq,
          "failed box CAS returns actual old value");
    CHECK(npl_atomic_box_compare_and_swap(box, equal_not_eq, first,
                                          ORDER_SEQ) == equal_not_eq,
          "successful box CAS returns observed old value");
    CHECK(npl_atomic_box_ref(box, ORDER_SEQ) == first,
          "successful box CAS stored desired");
    (void)npl_atomic_box_set(box, npl_false(), ORDER_SEQ);
    CHECK(npl_atomic_box_ref(box, ORDER_SEQ) == npl_false(), "box set!");
    npl_gc_unroot(&box);
    npl_gc_unroot(&equal_not_eq);
    npl_gc_unroot(&first);
}

static void check_fxboxes(void)
{
    NplVal box = npl_atomic_fxbox_make(npl_fixnum(10));
    npl_gc_root(&box);
    CHECK(npl_atomic_fxbox_p(box) == npl_true(), "atomic-fxbox?");
    CHECK(npl_atomic_fxbox_ref(box, ORDER_SEQ) == npl_fixnum(10),
          "fxbox ref");
    CHECK(npl_atomic_fxbox_swap(box, npl_fixnum(20), ORDER_SEQ) ==
          npl_fixnum(10), "fxbox swap returns previous");
    CHECK(npl_atomic_fxbox_compare_and_swap(box, npl_fixnum(10),
                                            npl_fixnum(30),
                                            ORDER_SEQ) == npl_fixnum(20),
          "failed fxbox CAS returns actual old value");
    CHECK(npl_atomic_fxbox_compare_and_swap(box, npl_fixnum(20),
                                            npl_fixnum(30),
                                            ORDER_SEQ) == npl_fixnum(20),
          "successful fxbox CAS returns observed old value");
    CHECK(npl_atomic_fxbox_ref(box, ORDER_SEQ) == npl_fixnum(30),
          "successful fxbox CAS stored desired");
    CHECK(npl_atomic_fxbox_add_fetch(box, npl_fixnum(5), ORDER_SEQ) ==
          npl_fixnum(30), "fxbox +/fetch! returns previous");
    CHECK(npl_atomic_fxbox_ref(box, ORDER_SEQ) == npl_fixnum(35),
          "fxbox +/fetch! stored the sum");
    CHECK(npl_atomic_fxbox_sub_fetch(box, npl_fixnum(2), ORDER_SEQ) ==
          npl_fixnum(35), "fxbox -/fetch! returns previous");
    CHECK(npl_atomic_fxbox_ref(box, ORDER_SEQ) == npl_fixnum(33),
          "fxbox -/fetch! stored the difference");
    CHECK(npl_atomic_fxbox_and_fetch(box, npl_fixnum(15), ORDER_SEQ) ==
          npl_fixnum(33), "fxbox and/fetch! returns previous");
    CHECK(npl_atomic_fxbox_ref(box, ORDER_SEQ) == npl_fixnum(1),
          "fxbox and/fetch! stored the bitwise and");
    CHECK(npl_atomic_fxbox_ior_fetch(box, npl_fixnum(6), ORDER_SEQ) ==
          npl_fixnum(1), "fxbox ior/fetch! returns previous");
    CHECK(npl_atomic_fxbox_ref(box, ORDER_SEQ) == npl_fixnum(7),
          "fxbox ior/fetch! stored the bitwise or");
    CHECK(npl_atomic_fxbox_xor_fetch(box, npl_fixnum(3), ORDER_SEQ) ==
          npl_fixnum(7), "fxbox xor/fetch! returns previous");
    CHECK(npl_atomic_fxbox_ref(box, ORDER_SEQ) == npl_fixnum(4),
          "fxbox xor/fetch! stored the bitwise xor");

    /* Both boundaries accept zero and stay unchanged.  Overflow rejection
     * (including "box unchanged") is exercised from NPL with guard, because
     * the C substrate raises through the Scheme handler chain. */
    (void)npl_atomic_fxbox_set(box, npl_fixnum(NPL_FIXNUM_MAX), ORDER_SEQ);
    CHECK(npl_atomic_fxbox_add_fetch(box, npl_fixnum(0), ORDER_SEQ) ==
          npl_fixnum(NPL_FIXNUM_MAX), "fxbox add 0 at max");
    (void)npl_atomic_fxbox_set(box, npl_fixnum(NPL_FIXNUM_MIN), ORDER_SEQ);
    CHECK(npl_atomic_fxbox_sub_fetch(box, npl_fixnum(0), ORDER_SEQ) ==
          npl_fixnum(NPL_FIXNUM_MIN), "fxbox sub 0 at min");
    npl_gc_unroot(&box);
}

static void check_pairs(void)
{
    NplVal pair = npl_atomic_pair_make(npl_fixnum(1), npl_fixnum(2));
    npl_gc_root(&pair);
    CHECK(npl_atomic_pair_p(pair) == npl_true(), "atomic-pair?");
    NplVal snapshot = npl_atomic_pair_ref(pair, ORDER_SEQ);
    CHECK(values_count(snapshot) == 2, "pair ref returns two values");
    CHECK(value_at(snapshot, 0) == npl_fixnum(1) &&
          value_at(snapshot, 1) == npl_fixnum(2), "pair ref snapshot");
    NplVal previous = npl_atomic_pair_swap(pair, npl_fixnum(3),
                                           npl_fixnum(4), ORDER_SEQ);
    CHECK(values_count(previous) == 2 &&
          value_at(previous, 0) == npl_fixnum(1) &&
          value_at(previous, 1) == npl_fixnum(2),
          "pair swap returns previous two values");
    NplVal cas = npl_atomic_pair_compare_and_swap(
        pair, npl_fixnum(1), npl_fixnum(2), npl_fixnum(5), npl_fixnum(6),
        ORDER_SEQ);
    CHECK(values_count(cas) == 2 &&
          value_at(cas, 0) == npl_fixnum(3) &&
          value_at(cas, 1) == npl_fixnum(4),
          "failed pair CAS returns actual two values");
    cas = npl_atomic_pair_compare_and_swap(
        pair, npl_fixnum(3), npl_fixnum(4), npl_fixnum(5), npl_fixnum(6),
        ORDER_SEQ);
    CHECK(values_count(cas) == 2 &&
          value_at(cas, 0) == npl_fixnum(3) &&
          value_at(cas, 1) == npl_fixnum(4),
          "successful pair CAS returns observed two values");
    snapshot = npl_atomic_pair_ref(pair, ORDER_SEQ);
    CHECK(value_at(snapshot, 0) == npl_fixnum(5) &&
          value_at(snapshot, 1) == npl_fixnum(6),
          "successful pair CAS stored both desired values");
    (void)npl_atomic_pair_set(pair, npl_false(), npl_true(), ORDER_SEQ);
    snapshot = npl_atomic_pair_ref(pair, ORDER_SEQ);
    CHECK(value_at(snapshot, 0) == npl_false() &&
          value_at(snapshot, 1) == npl_true(), "pair set!");
    npl_gc_unroot(&pair);
}

/* ---- GC edges and old -> young stores ---- */

static void force_minor_collection(void)
{
    size_t before = npl_gc_minor_collections();
    while (npl_gc_minor_collections() == before) {
        NplVal filler = npl_cons(npl_fixnum(1), npl_fixnum(2));
        (void)filler;
    }
}

static void check_gc_edges(void)
{
    NplVal young = npl_cons(npl_fixnum(11), npl_fixnum(22));
    npl_gc_root(&young);
    NplVal box = npl_atomic_box_make(npl_false());
    NplVal pair = npl_atomic_pair_make(npl_false(), npl_false());
    npl_gc_root(&box);
    npl_gc_root(&pair);

    /* Promote the holders, then store young pointer values.  The barrier's
     * remembered-set entry is what keeps them alive when the direct `young`
     * root is removed; NPL_GC_VERIFY also checks it independently. */
    npl_gc_collect();
    npl_atomic_box_set(box, young, ORDER_RELEASE);
    npl_atomic_pair_set(pair, young, young, ORDER_RELEASE);
    npl_gc_unroot(&young);
    force_minor_collection();
    CHECK(npl_atomic_box_ref(box, ORDER_ACQUIRE) != npl_false(),
          "old box retains a young value across minor GC");
    NplVal snapshot = npl_atomic_pair_ref(pair, ORDER_ACQUIRE);
    CHECK(value_at(snapshot, 0) != npl_false() &&
          value_at(snapshot, 1) != npl_false(),
          "old pair retains young values across minor GC");
    npl_gc_collect();
    CHECK(npl_atomic_box_ref(box, ORDER_ACQUIRE) != npl_false(),
          "old box retains its value across full GC");
    npl_gc_unroot(&pair);
    npl_gc_unroot(&box);
}

/* ---- contended counter: every old value is a distinct permutation ---- */

typedef struct {
    Gate *gate;
    int iterations;
    int64_t *old_values;
    int ok;
} CounterWorker;

static NplVal g_counter_box;

static void *counter_worker(void *argument)
{
    CounterWorker *worker = (CounterWorker *)argument;
    if (!npl_thread_attach()) {
        worker->ok = 0;
        return NULL;
    }
    gate_wait(worker->gate);
    for (int i = 0; i < worker->iterations; i++) {
        NplVal old = npl_atomic_fxbox_add_fetch(
            g_counter_box, npl_fixnum(1), ORDER_SEQ);
        if (!npl_is_fixnum(old)) {
            worker->ok = 0;
            break;
        }
        worker->old_values[i] = npl_fixnum_value(old);
    }
    npl_thread_detach();
    return NULL;
}

static void check_counter_contention(void)
{
    enum { WORKERS = 4, PER_WORKER = 5000 };
    g_counter_box = npl_atomic_fxbox_make(npl_fixnum(0));
    npl_gc_root(&g_counter_box);
    Gate gate;
    gate_init(&gate, WORKERS);
    pthread_t threads[WORKERS];
    CounterWorker workers[WORKERS];
    memset(workers, 0, sizeof(workers));
    for (int i = 0; i < WORKERS; i++) {
        workers[i].gate = &gate;
        workers[i].iterations = PER_WORKER;
        workers[i].old_values = (int64_t *)calloc(PER_WORKER,
                                                  sizeof(int64_t));
        workers[i].ok = 1;
        if (!workers[i].old_values ||
            pthread_create(&threads[i], NULL, counter_worker,
                           &workers[i]) != 0) {
            fprintf(stderr, "FAIL: counter worker start\n");
            failures++;
            return;
        }
    }
    gate_open(&gate);
    npl_gc_exclude();
    for (int i = 0; i < WORKERS; i++)
        pthread_join(threads[i], NULL);
    npl_gc_include();

    int64_t total = (int64_t)WORKERS * PER_WORKER;
    CHECK(npl_atomic_fxbox_ref(g_counter_box, ORDER_SEQ) ==
          npl_fixnum(total), "contended fxbox reaches the exact total");
    unsigned char *seen = (unsigned char *)calloc((size_t)total, 1);
    int history_ok = 1;
    for (int i = 0; i < WORKERS; i++) {
        if (!workers[i].ok) history_ok = 0;
        for (int j = 0; j < PER_WORKER; j++) {
            int64_t value = workers[i].old_values[j];
            if (value < 0 || value >= total || seen[value]) {
                history_ok = 0;
            } else {
                seen[value] = 1;
            }
        }
        free(workers[i].old_values);
    }
    free(seen);
    CHECK(history_ok, "contended fxbox old values form one permutation");
    gate_destroy(&gate);
    npl_gc_unroot(&g_counter_box);
}

/* ---- pointer publication and pair snapshot under GC ---- */

typedef struct {
    Gate *gate;
    int iterations;
    int ok;
} PublishWorker;

static NplVal g_publish_box;
static NplVal g_publish_pair;

static void *publish_writer(void *argument)
{
    PublishWorker *worker = (PublishWorker *)argument;
    if (!npl_thread_attach()) {
        worker->ok = 0;
        return NULL;
    }
    gate_wait(worker->gate);
    for (int i = 0; i < worker->iterations; i++) {
        NplVal payload = npl_cons(npl_fixnum(i), npl_fixnum(i));
        npl_atomic_box_set(g_publish_box, payload, ORDER_RELEASE);
        npl_atomic_pair_set(g_publish_pair, npl_fixnum(i), npl_fixnum(i),
                            ORDER_RELEASE);
        if (i % 64 == 0) npl_safepoint_poll();
    }
    npl_thread_detach();
    return NULL;
}

static void *publish_reader(void *argument)
{
    PublishWorker *worker = (PublishWorker *)argument;
    if (!npl_thread_attach()) {
        worker->ok = 0;
        return NULL;
    }
    gate_wait(worker->gate);
    for (int i = 0; i < worker->iterations; i++) {
        NplVal payload = npl_atomic_box_ref(g_publish_box, ORDER_ACQUIRE);
        if (payload != npl_false() &&
            (!npl_is_ptr(payload) || npl_header(payload)->tag != BOX_PAIR ||
             npl_car(payload) != npl_cdr(payload)))
            worker->ok = 0;
        NplVal snapshot = npl_atomic_pair_ref(g_publish_pair, ORDER_ACQUIRE);
        if (values_count(snapshot) != 2 ||
            value_at(snapshot, 0) != value_at(snapshot, 1))
            worker->ok = 0;
        if (i % 64 == 0) npl_safepoint_poll();
    }
    npl_thread_detach();
    return NULL;
}

static void check_publication(void)
{
    g_publish_box = npl_atomic_box_make(npl_false());
    g_publish_pair = npl_atomic_pair_make(npl_fixnum(0), npl_fixnum(0));
    npl_gc_root(&g_publish_box);
    npl_gc_root(&g_publish_pair);
    Gate gate;
    gate_init(&gate, 2);
    pthread_t writer_thread, reader_thread;
    PublishWorker writer = { &gate, 10000, 1 };
    PublishWorker reader = { &gate, 10000, 1 };
    CHECK(pthread_create(&writer_thread, NULL, publish_writer, &writer) == 0,
          "publication writer start");
    CHECK(pthread_create(&reader_thread, NULL, publish_reader, &reader) == 0,
          "publication reader start");
    gate_open(&gate);
    /* These workers allocate and can trigger a collection; the joining main
     * thread has no poll site, so it must leave the mutator count first. */
    npl_gc_exclude();
    pthread_join(writer_thread, NULL);
    pthread_join(reader_thread, NULL);
    npl_gc_include();
    CHECK(writer.ok, "publication writer completed");
    CHECK(reader.ok, "acquire readers never observed a torn pair or payload");
    gate_destroy(&gate);
    npl_gc_unroot(&g_publish_pair);
    npl_gc_unroot(&g_publish_box);
}

/* ---- collector thread racing pointer swaps and failed CAS results ---- */

typedef struct {
    volatile int *stop;
    int collections;
} CollectorWorker;

static void *collector_worker(void *argument)
{
    CollectorWorker *worker = (CollectorWorker *)argument;
    if (!npl_thread_attach()) return NULL;
    while (!*worker->stop) {
        npl_gc_collect();
        worker->collections++;
    }
    npl_thread_detach();
    return NULL;
}

typedef struct {
    Gate *gate;
    int iterations;
    int ok;
} SwapWorker;

static NplVal g_swap_box;

static void *swap_worker(void *argument)
{
    SwapWorker *worker = (SwapWorker *)argument;
    if (!npl_thread_attach()) {
        worker->ok = 0;
        return NULL;
    }
    gate_wait(worker->gate);
    for (int i = 0; i < worker->iterations; i++) {
        NplVal fresh = npl_cons(npl_fixnum(i), npl_fixnum(i + 1));
        NplVal previous = npl_atomic_box_swap(g_swap_box, fresh, ORDER_SEQ);
        if (npl_is_ptr(previous) && npl_header(previous)->tag == BOX_PAIR) {
            if (npl_fixnum_value(npl_car(previous)) + 1 !=
                npl_fixnum_value(npl_cdr(previous)))
                worker->ok = 0;
        } else if (previous != npl_false()) {
            worker->ok = 0;
        }
        /* Keep result histories alive for a collection while the collector
         * runs on the main thread below. */
        if (i % 32 == 0) npl_safepoint_poll();
    }
    npl_thread_detach();
    return NULL;
}

static void check_concurrent_collector(void)
{
    enum { WORKERS = 2, ITERATIONS = 3000 };
    g_swap_box = npl_atomic_box_make(npl_false());
    npl_gc_root(&g_swap_box);
    Gate gate;
    gate_init(&gate, WORKERS);
    pthread_t threads[WORKERS];
    SwapWorker workers[WORKERS];
    for (int i = 0; i < WORKERS; i++) {
        workers[i].gate = &gate;
        workers[i].iterations = ITERATIONS;
        workers[i].ok = 1;
        pthread_create(&threads[i], NULL, swap_worker, &workers[i]);
    }
    volatile int stop = 0;
    CollectorWorker collector = { &stop, 0 };
    pthread_t collector_thread;
    pthread_create(&collector_thread, NULL, collector_worker, &collector);
    gate_open(&gate);
    /* The main thread waits in pthread_join, which has no safepoint poll.
     * Exclude it from the mutator count while the collector thread runs, as
     * the T2b harness does for its own joins. */
    npl_gc_exclude();
    for (int i = 0; i < WORKERS; i++)
        pthread_join(threads[i], NULL);
    stop = 1;
    pthread_join(collector_thread, NULL);
    npl_gc_include();
    CHECK(collector.collections > 0,
          "concurrent collector ran at least one collection");
    CHECK(workers[0].ok && workers[1].ok,
          "pointer swaps stayed well formed under concurrent GC");
    npl_gc_collect();
    NplVal value = npl_atomic_box_ref(g_swap_box, ORDER_SEQ);
    CHECK(npl_is_ptr(value) && npl_header(value)->tag == BOX_PAIR &&
          npl_fixnum_value(npl_car(value)) + 1 ==
              npl_fixnum_value(npl_cdr(value)),
          "a swapped result survives a later collection");
    gate_destroy(&gate);
    npl_gc_unroot(&g_swap_box);
}

/* ---- failed CAS observed value survives replacement + GC ---- */

static void check_failed_cas_result(void)
{
    NplVal first = npl_cons(npl_fixnum(1), npl_fixnum(2));
    NplVal second = npl_cons(npl_fixnum(3), npl_fixnum(4));
    NplVal replacement = npl_cons(npl_fixnum(5), npl_fixnum(6));
    npl_gc_root(&first);
    npl_gc_root(&second);
    npl_gc_root(&replacement);
    NplVal box = npl_atomic_box_make(first);
    npl_gc_root(&box);
    NplVal old = npl_atomic_box_compare_and_swap(box, second, replacement,
                                                 ORDER_SEQ);
    CHECK(old == first, "failed CAS returns the replaced observed value");
    (void)npl_atomic_box_set(box, second, ORDER_SEQ);
    npl_gc_collect();
    CHECK(npl_car(old) == npl_fixnum(1) && npl_cdr(old) == npl_fixnum(2),
          "failed CAS result remains live after replacement and GC");
    npl_gc_unroot(&box);
    npl_gc_unroot(&replacement);
    npl_gc_unroot(&second);
    npl_gc_unroot(&first);
}

int main(void)
{
    npl_runtime_init();
    check_flags();
    check_boxes();
    check_fxboxes();
    check_pairs();
    check_gc_edges();
    check_counter_contention();
    check_publication();
    check_concurrent_collector();
    check_failed_cas_result();
    if (getenv("NPL_ATOMIC_TEST_VERBOSE")) fputs("phases complete\n", stderr);

    if (failures) {
        fprintf(stderr, "atomic: %d failure(s)\n", failures);
        return 1;
    }
    puts("PASS: atomic flags, boxes, fxboxes, pairs, fences, histories and GC");
    return 0;
}
