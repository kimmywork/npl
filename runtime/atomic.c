/*
 * atomic.c — SRFI 230 atomic flags, boxes, fixnum boxes, pairs and fences.
 *
 * T3-A/B reference implementation: one dedicated atomic-domain mutex
 * serializes every public atomic operation, including fences.  This
 * deliberately provides sequentially-consistent ordering for every requested
 * order and one total order across all object kinds.  It is a correctness
 * baseline, not a claim of lock-free operation.
 *
 * The critical section must not allocate managed objects, run Scheme
 * callbacks, poll, or raise.  Types, orders and arithmetic operands are
 * validated before the lock; arithmetic range failure is recorded inside and
 * raised after release, so an unwind can never skip an unlock.  A waiter
 * loops on trylock with a safepoint poll, so a world stop never waits on a
 * thread blocked in pthread_mutex_lock.
 *
 * Lock order: atomic-domain lock -> heap lock (only through
 * npl_gc_write_barrier_atomic for pointer stores).  No path takes them in
 * the other order, and neither lock is held while polling.
 */
#define _POSIX_C_SOURCE 200809L

#include "npl.h"
#include "thread.h"
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>

#define NPL_ATOMIC_RELAXED 0
#define NPL_ATOMIC_ACQUIRE 1
#define NPL_ATOMIC_RELEASE 2
#define NPL_ATOMIC_ACQUIRE_RELEASE 3
#define NPL_ATOMIC_SEQUENTIALLY_CONSISTENT 4

static pthread_mutex_t g_atomic_lock = PTHREAD_MUTEX_INITIALIZER;

/* A plain pthread_mutex_lock waiter is not at a poll site, so the collector
 * could wait forever for it.  Trylock + poll keeps every waiter parkable;
 * the critical section itself never polls. */
static void atomic_lock(void)
{
    for (;;) {
        if (pthread_mutex_trylock(&g_atomic_lock) == 0)
            return;
        npl_safepoint_poll();
        sched_yield();
    }
}

static void atomic_unlock(void)
{
    pthread_mutex_unlock(&g_atomic_lock);
}

static int tagged(NplVal value, uint32_t tag)
{
    return npl_is_ptr(value) && npl_header(value)->tag == tag;
}

static void require(NplVal value, uint32_t tag, const char *who)
{
    if (!tagged(value, tag))
        npl_primitive_error(who, "wrong type of argument", value);
}

/* Orders arrive as the NPL wrapper's validated fixnum code.  Revalidate here
 * so a direct C caller cannot pass an arbitrary value into an operation. */
static int order_code(NplVal value, const char *who)
{
    if (!npl_is_fixnum(value))
        npl_primitive_error(who, "expected a memory order", value);
    int64_t code = npl_fixnum_value(value);
    if (code < NPL_ATOMIC_RELAXED ||
        code > NPL_ATOMIC_SEQUENTIALLY_CONSISTENT)
        npl_primitive_error(who, "invalid memory order", value);
    return (int)code;
}

static int64_t fixnum_argument(NplVal value, const char *who)
{
    if (!npl_is_fixnum(value))
        npl_primitive_error(who, "expected a fixnum", value);
    return npl_fixnum_value(value);
}

/* T3-D: scalar operations with a weaker-than-sequentially-consistent order
 * use per-location C11-style atomics.  Sequentially consistent operations
 * stay in the common locked domain, as do pair operations and fences, so the
 * public default still has one total order across every object kind.  Set
 * NPL_ATOMIC_FORCE_LOCKED to compare against the T3-A/B reference path. */
static int fast_path_enabled(void)
{
    static int cached = -1;
    int value = __atomic_load_n(&cached, __ATOMIC_RELAXED);
    if (value < 0) {
        const char *text = getenv("NPL_ATOMIC_FORCE_LOCKED");
        value = (text && *text) ? 0 : 1;
        __atomic_store_n(&cached, value, __ATOMIC_RELAXED);
    }
    return value;
}

static int c_load_order(int order)
{
    switch (order) {
    case NPL_ATOMIC_RELAXED: return __ATOMIC_RELAXED;
    case NPL_ATOMIC_ACQUIRE: return __ATOMIC_ACQUIRE;
    /* No direct "release load": strengthen the requested order. */
    case NPL_ATOMIC_RELEASE: return __ATOMIC_SEQ_CST;
    case NPL_ATOMIC_ACQUIRE_RELEASE: return __ATOMIC_SEQ_CST;
    default: return __ATOMIC_SEQ_CST;
    }
}

static int c_store_order(int order)
{
    switch (order) {
    case NPL_ATOMIC_RELAXED: return __ATOMIC_RELAXED;
    /* No direct "acquire store": strengthen the requested order. */
    case NPL_ATOMIC_ACQUIRE: return __ATOMIC_SEQ_CST;
    case NPL_ATOMIC_RELEASE: return __ATOMIC_RELEASE;
    case NPL_ATOMIC_ACQUIRE_RELEASE: return __ATOMIC_SEQ_CST;
    default: return __ATOMIC_SEQ_CST;
    }
}

static int c_rmw_order(int order)
{
    switch (order) {
    case NPL_ATOMIC_RELAXED: return __ATOMIC_RELAXED;
    case NPL_ATOMIC_ACQUIRE: return __ATOMIC_ACQUIRE;
    case NPL_ATOMIC_RELEASE: return __ATOMIC_RELEASE;
    case NPL_ATOMIC_ACQUIRE_RELEASE: return __ATOMIC_ACQ_REL;
    default: return __ATOMIC_SEQ_CST;
    }
}

static int c_cas_failure_order(int order)
{
    switch (order) {
    case NPL_ATOMIC_RELAXED: return __ATOMIC_RELAXED;
    case NPL_ATOMIC_ACQUIRE: return __ATOMIC_ACQUIRE;
    /* A release RMW has a relaxed failure order. */
    case NPL_ATOMIC_RELEASE: return __ATOMIC_RELAXED;
    case NPL_ATOMIC_ACQUIRE_RELEASE: return __ATOMIC_ACQUIRE;
    default: return __ATOMIC_SEQ_CST;
    }
}

/* Sequentially consistent operations share the locked domain; the other
 * orders may use the per-location fast path unless the reference path was
 * requested. */
static int use_fast_path(int order)
{
    return order != NPL_ATOMIC_SEQUENTIALLY_CONSISTENT &&
           fast_path_enabled();
}

static NplAtomicFlag *flag_argument(NplVal value, const char *who)
{
    require(value, BOX_ATOMIC_FLAG, who);
    return (NplAtomicFlag *)npl_header(value);
}

static NplAtomicBox *box_argument(NplVal value, const char *who)
{
    require(value, BOX_ATOMIC_BOX, who);
    return (NplAtomicBox *)npl_header(value);
}

static NplAtomicFxbox *fxbox_argument(NplVal value, const char *who)
{
    require(value, BOX_ATOMIC_FXBOX, who);
    return (NplAtomicFxbox *)npl_header(value);
}

static NplAtomicPair *pair_argument(NplVal value, const char *who)
{
    require(value, BOX_ATOMIC_PAIR, who);
    return (NplAtomicPair *)npl_header(value);
}

/* Checked signed arithmetic in the NPL fixnum range.  SRFI 230 does not
 * specify overflow behavior; NPL rejects before changing the box. */
static int add_checked(int64_t left, int64_t right, int64_t *result)
{
    if (right > 0 && left > NPL_FIXNUM_MAX - right) return 0;
    if (right < 0 && left < NPL_FIXNUM_MIN - right) return 0;
    *result = left + right;
    return 1;
}

static int subtract_checked(int64_t left, int64_t right, int64_t *result)
{
    if (right < 0 && left > NPL_FIXNUM_MAX + right) return 0;
    if (right > 0 && left < NPL_FIXNUM_MIN + right) return 0;
    *result = left - right;
    return 1;
}

static int in_fixnum_range(int64_t value)
{
    return value >= NPL_FIXNUM_MIN && value <= NPL_FIXNUM_MAX;
}

/* Root both observed fields before the result packet allocation.  A failed
 * CAS still returns heap objects, so they must survive a collection here. */
static NplVal two_values(NplVal first, NplVal second)
{
    NplVal slots[2];
    slots[0] = first;
    slots[1] = second;
    npl_gc_root(&slots[0]);
    npl_gc_root(&slots[1]);
    NplVal result = npl_make_values(2, slots);
    npl_gc_unroot(&slots[1]);
    npl_gc_unroot(&slots[0]);
    return result;
}

static void store_box(NplAtomicBox *box, NplVal value)
{
    box->value = value;
    npl_gc_write_barrier_atomic(&box->header, value);
}

static void store_pair(NplAtomicPair *pair, NplVal first, NplVal second)
{
    pair->first = first;
    pair->second = second;
    npl_gc_write_barrier_atomic(&pair->header, first);
    npl_gc_write_barrier_atomic(&pair->header, second);
}

/* ---- flags ---- */

NplVal npl_atomic_flag_make(void)
{
    NplAtomicFlag *flag =
        (NplAtomicFlag *)npl_alloc_object(sizeof(NplAtomicFlag));
    flag->header.tag = BOX_ATOMIC_FLAG;
    flag->header.flags = 0;
    flag->value = 0;
    return (NplVal)(uintptr_t)flag | TAG_PTR;
}

NplVal npl_atomic_flag_p(NplVal value)
{
    return tagged(value, BOX_ATOMIC_FLAG) ? npl_true() : npl_false();
}

NplVal npl_atomic_flag_test_and_set(NplVal value, NplVal order)
{
    NplAtomicFlag *flag = flag_argument(value, "atomic-flag-test-and-set!");
    int code = order_code(order, "atomic-flag-test-and-set!");
    int previous;
    if (use_fast_path(code)) {
        previous = __atomic_exchange_n(&flag->value, 1, c_rmw_order(code));
    } else {
        atomic_lock();
        previous = flag->value;
        flag->value = 1;
        atomic_unlock();
    }
    return previous ? npl_true() : npl_false();
}

NplVal npl_atomic_flag_clear(NplVal value, NplVal order)
{
    NplAtomicFlag *flag = flag_argument(value, "atomic-flag-clear!");
    int code = order_code(order, "atomic-flag-clear!");
    if (use_fast_path(code))
        __atomic_store_n(&flag->value, 0, c_store_order(code));
    else {
        atomic_lock();
        flag->value = 0;
        atomic_unlock();
    }
    return npl_unspecified();
}

/* ---- arbitrary-value boxes ---- */

NplVal npl_atomic_box_make(NplVal value)
{
    NplAtomicBox *box =
        (NplAtomicBox *)npl_alloc_object(sizeof(NplAtomicBox));
    box->header.tag = BOX_ATOMIC_BOX;
    box->header.flags = 0;
    box->value = value;
    return (NplVal)(uintptr_t)box | TAG_PTR;
}

NplVal npl_atomic_box_p(NplVal value)
{
    return tagged(value, BOX_ATOMIC_BOX) ? npl_true() : npl_false();
}

NplVal npl_atomic_box_ref(NplVal value, NplVal order)
{
    NplAtomicBox *box = box_argument(value, "atomic-box-ref");
    int code = order_code(order, "atomic-box-ref");
    if (use_fast_path(code))
        return __atomic_load_n(&box->value, c_load_order(code));
    atomic_lock();
    NplVal result = box->value;
    atomic_unlock();
    return result;
}

NplVal npl_atomic_box_set(NplVal value, NplVal object, NplVal order)
{
    NplAtomicBox *box = box_argument(value, "atomic-box-set!");
    int code = order_code(order, "atomic-box-set!");
    if (use_fast_path(code)) {
        __atomic_store_n(&box->value, object, c_store_order(code));
        npl_gc_write_barrier_atomic(&box->header, object);
    } else {
        atomic_lock();
        store_box(box, object);
        atomic_unlock();
    }
    return npl_unspecified();
}

NplVal npl_atomic_box_swap(NplVal value, NplVal object, NplVal order)
{
    NplAtomicBox *box = box_argument(value, "atomic-box-swap!");
    int code = order_code(order, "atomic-box-swap!");
    if (use_fast_path(code)) {
        NplVal previous = __atomic_exchange_n(&box->value, object,
                                              c_rmw_order(code));
        npl_gc_write_barrier_atomic(&box->header, object);
        return previous;
    }
    atomic_lock();
    NplVal previous = box->value;
    store_box(box, object);
    atomic_unlock();
    return previous;
}

NplVal npl_atomic_box_compare_and_swap(NplVal value, NplVal expected,
                                       NplVal desired, NplVal order)
{
    NplAtomicBox *box = box_argument(value, "atomic-box-compare-and-swap!");
    int code = order_code(order, "atomic-box-compare-and-swap!");
    if (use_fast_path(code)) {
        NplVal actual = expected;
        if (__atomic_compare_exchange_n(&box->value, &actual, desired, 0,
                                        c_rmw_order(code),
                                        c_cas_failure_order(code)))
            npl_gc_write_barrier_atomic(&box->header, desired);
        return actual;
    }
    atomic_lock();
    NplVal actual = box->value;
    if (actual == expected)
        store_box(box, desired);
    atomic_unlock();
    return actual;
}

/* ---- fixnum boxes ---- */

NplVal npl_atomic_fxbox_make(NplVal value)
{
    int64_t initial = fixnum_argument(value, "make-atomic-fxbox");
    NplAtomicFxbox *box =
        (NplAtomicFxbox *)npl_alloc_object(sizeof(NplAtomicFxbox));
    box->header.tag = BOX_ATOMIC_FXBOX;
    box->header.flags = 0;
    box->value = initial;
    return (NplVal)(uintptr_t)box | TAG_PTR;
}

NplVal npl_atomic_fxbox_p(NplVal value)
{
    return tagged(value, BOX_ATOMIC_FXBOX) ? npl_true() : npl_false();
}

NplVal npl_atomic_fxbox_ref(NplVal value, NplVal order)
{
    NplAtomicFxbox *box = fxbox_argument(value, "atomic-fxbox-ref");
    int code = order_code(order, "atomic-fxbox-ref");
    if (use_fast_path(code))
        return npl_fixnum(__atomic_load_n(&box->value, c_load_order(code)));
    atomic_lock();
    int64_t result = box->value;
    atomic_unlock();
    return npl_fixnum(result);
}

NplVal npl_atomic_fxbox_set(NplVal value, NplVal object, NplVal order)
{
    NplAtomicFxbox *box = fxbox_argument(value, "atomic-fxbox-set!");
    int64_t desired = fixnum_argument(object, "atomic-fxbox-set!");
    int code = order_code(order, "atomic-fxbox-set!");
    if (use_fast_path(code)) {
        __atomic_store_n(&box->value, desired, c_store_order(code));
    } else {
        atomic_lock();
        box->value = desired;
        atomic_unlock();
    }
    return npl_unspecified();
}

NplVal npl_atomic_fxbox_swap(NplVal value, NplVal object, NplVal order)
{
    NplAtomicFxbox *box = fxbox_argument(value, "atomic-fxbox-swap!");
    int64_t desired = fixnum_argument(object, "atomic-fxbox-swap!");
    int code = order_code(order, "atomic-fxbox-swap!");
    if (use_fast_path(code))
        return npl_fixnum(__atomic_exchange_n(&box->value, desired,
                                             c_rmw_order(code)));
    atomic_lock();
    int64_t previous = box->value;
    box->value = desired;
    atomic_unlock();
    return npl_fixnum(previous);
}

NplVal npl_atomic_fxbox_compare_and_swap(NplVal value, NplVal expected,
                                         NplVal desired, NplVal order)
{
    NplAtomicFxbox *box =
        fxbox_argument(value, "atomic-fxbox-compare-and-swap!");
    int64_t expected_fx = fixnum_argument(expected,
        "atomic-fxbox-compare-and-swap!");
    int64_t desired_fx = fixnum_argument(desired,
        "atomic-fxbox-compare-and-swap!");
    int code = order_code(order, "atomic-fxbox-compare-and-swap!");
    if (use_fast_path(code)) {
        int64_t actual = expected_fx;
        (void)__atomic_compare_exchange_n(&box->value, &actual, desired_fx, 0,
                                          c_rmw_order(code),
                                          c_cas_failure_order(code));
        return npl_fixnum(actual);
    }
    atomic_lock();
    int64_t actual = box->value;
    if (actual == expected_fx)
        box->value = desired_fx;
    atomic_unlock();
    return npl_fixnum(actual);
}

static int fxbox_next(int64_t previous, int64_t operand, int operation,
                      int64_t *next)
{
    switch (operation) {
    case 0:
        return add_checked(previous, operand, next);
    case 1:
        return subtract_checked(previous, operand, next);
    case 2:
        *next = (int64_t)((uint64_t)previous & (uint64_t)operand);
        return in_fixnum_range(*next);
    case 3:
        *next = (int64_t)((uint64_t)previous | (uint64_t)operand);
        return in_fixnum_range(*next);
    default:
        *next = (int64_t)((uint64_t)previous ^ (uint64_t)operand);
        return in_fixnum_range(*next);
    }
}

static NplVal fxbox_update(NplVal value, NplVal object, NplVal order,
                           int operation, const char *who)
{
    NplAtomicFxbox *box = fxbox_argument(value, who);
    int64_t operand = fixnum_argument(object, who);
    int code = order_code(order, who);
    int overflow = 0;
    int64_t previous;

    if (use_fast_path(code)) {
        previous = __atomic_load_n(&box->value, c_load_order(code));
        for (;;) {
            int64_t next;
            if (!fxbox_next(previous, operand, operation, &next)) {
                overflow = 1;
                break;
            }
            int64_t actual = previous;
            if (__atomic_compare_exchange_n(&box->value, &actual, next, 0,
                                            c_rmw_order(code),
                                            c_cas_failure_order(code))) {
                previous = actual;
                break;
            }
            previous = actual;   /* observed current value; retry */
        }
        if (overflow)
            npl_primitive_error(who, "fixnum overflow", npl_fixnum(operand));
        return npl_fixnum(previous);
    }

    atomic_lock();
    previous = box->value;
    int64_t next;
    if (fxbox_next(previous, operand, operation, &next)) box->value = next;
    else overflow = 1;
    atomic_unlock();
    if (overflow)
        npl_primitive_error(who, "fixnum overflow", npl_fixnum(operand));
    return npl_fixnum(previous);
}

NplVal npl_atomic_fxbox_add_fetch(NplVal value, NplVal object, NplVal order)
{
    return fxbox_update(value, object, order, 0, "atomic-fxbox+/fetch!");
}

NplVal npl_atomic_fxbox_sub_fetch(NplVal value, NplVal object, NplVal order)
{
    return fxbox_update(value, object, order, 1, "atomic-fxbox-/fetch!");
}

NplVal npl_atomic_fxbox_and_fetch(NplVal value, NplVal object, NplVal order)
{
    return fxbox_update(value, object, order, 2, "atomic-fxbox-and/fetch!");
}

NplVal npl_atomic_fxbox_ior_fetch(NplVal value, NplVal object, NplVal order)
{
    return fxbox_update(value, object, order, 3, "atomic-fxbox-ior/fetch!");
}

NplVal npl_atomic_fxbox_xor_fetch(NplVal value, NplVal object, NplVal order)
{
    return fxbox_update(value, object, order, 4, "atomic-fxbox-xor/fetch!");
}

/* ---- pairs ---- */

NplVal npl_atomic_pair_make(NplVal first, NplVal second)
{
    NplAtomicPair *pair =
        (NplAtomicPair *)npl_alloc_object(sizeof(NplAtomicPair));
    pair->header.tag = BOX_ATOMIC_PAIR;
    pair->header.flags = 0;
    pair->first = first;
    pair->second = second;
    return (NplVal)(uintptr_t)pair | TAG_PTR;
}

NplVal npl_atomic_pair_p(NplVal value)
{
    return tagged(value, BOX_ATOMIC_PAIR) ? npl_true() : npl_false();
}

NplVal npl_atomic_pair_ref(NplVal value, NplVal order)
{
    NplAtomicPair *pair = pair_argument(value, "atomic-pair-ref");
    (void)order_code(order, "atomic-pair-ref");
    atomic_lock();
    NplVal first = pair->first;
    NplVal second = pair->second;
    atomic_unlock();
    return two_values(first, second);
}

NplVal npl_atomic_pair_set(NplVal value, NplVal first, NplVal second,
                           NplVal order)
{
    NplAtomicPair *pair = pair_argument(value, "atomic-pair-set!");
    (void)order_code(order, "atomic-pair-set!");
    atomic_lock();
    store_pair(pair, first, second);
    atomic_unlock();
    return npl_unspecified();
}

NplVal npl_atomic_pair_swap(NplVal value, NplVal first, NplVal second,
                            NplVal order)
{
    NplAtomicPair *pair = pair_argument(value, "atomic-pair-swap!");
    (void)order_code(order, "atomic-pair-swap!");
    atomic_lock();
    NplVal previous_first = pair->first;
    NplVal previous_second = pair->second;
    store_pair(pair, first, second);
    atomic_unlock();
    return two_values(previous_first, previous_second);
}

NplVal npl_atomic_pair_compare_and_swap(NplVal value, NplVal expected_first,
                                        NplVal expected_second,
                                        NplVal desired_first,
                                        NplVal desired_second, NplVal order)
{
    NplAtomicPair *pair =
        pair_argument(value, "atomic-pair-compare-and-swap!");
    (void)order_code(order, "atomic-pair-compare-and-swap!");
    atomic_lock();
    NplVal actual_first = pair->first;
    NplVal actual_second = pair->second;
    if (actual_first == expected_first && actual_second == expected_second)
        store_pair(pair, desired_first, desired_second);
    atomic_unlock();
    return two_values(actual_first, actual_second);
}

/* ---- fence ---- */

NplVal npl_atomic_fence(NplVal order)
{
    (void)order_code(order, "atomic-fence");
    atomic_lock();
    /* The mutex supplies the inter-thread ordering for the locked baseline;
     * the compiler barrier also keeps ordinary accesses on this side of the
     * fence from moving across it while the specialized C99 path is absent. */
    __sync_synchronize();
    atomic_unlock();
    return npl_unspecified();
}
