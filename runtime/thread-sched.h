/*
 * thread-sched.h — T2 (SRFI 18) object layouts and scheduler internals.
 *
 * Runtime C owns pthreads, stacks and the scheduler; (npl thread) is the
 * public NPL API. These heap objects carry NplVal fields (visited by the GC)
 * alongside native pthread state (never GC-scanned). See docs/reference/
 * srfi-18.html and .agents/design/threads.md.
 */
#ifndef NPL_THREAD_SCHED_H
#define NPL_THREAD_SCHED_H

#include "npl.h"
#include "thread.h"
#include <pthread.h>
#include <setjmp.h>
#include <time.h>

/* SRFI 18 thread states. */
#define NPL_THREAD_NEW 0
#define NPL_THREAD_RUNNABLE 1
#define NPL_THREAD_BLOCKED 2
#define NPL_THREAD_TERMINATED 3

/* SRFI 18 mutex states. */
#define NPL_MUTEX_UNLOCKED_NOT_ABANDONED 0
#define NPL_MUTEX_UNLOCKED_ABANDONED 1
#define NPL_MUTEX_LOCKED_NOT_OWNED 2
#define NPL_MUTEX_LOCKED_OWNED 3

/* SRFI 18 exception object kinds (opaque). */
#define NPL_EXC_JOIN_TIMEOUT 0
#define NPL_EXC_ABANDONED_MUTEX 1
#define NPL_EXC_TERMINATED_THREAD 2
#define NPL_EXC_UNCAUGHT 3

/* T2b virtual-thread states. */
#define NPL_VTHREAD_NEW 0
#define NPL_VTHREAD_RUNNABLE 1
#define NPL_VTHREAD_RUNNING 2
#define NPL_VTHREAD_SUSPENDED 3
#define NPL_VTHREAD_TERMINATED 4

/* What a thread is waiting on, for the single global scheduler condvar. */
#define NPL_WAIT_NONE 0
#define NPL_WAIT_JOIN 1
#define NPL_WAIT_MUTEX 2
#define NPL_WAIT_CONDVAR 3
#define NPL_WAIT_SLEEP 4

typedef struct {
    NplHeader header;
    NplVal name;
    NplVal specific;
    NplVal end_result;
    NplVal end_exception;
    NplVal owned_mutexes;      /* list of mutex objects */
    NplVal thunk;              /* procedure run on the new thread */
    NplVal parameters;         /* inherited dynamic environment */
    /* Native state (not GC-scanned). */
    pthread_t handle;
    jmp_buf terminate_jump;    /* set in thread_entry, longjmp'd on cancel */
    int state;
    int started;
    int cancel_requested;
    int wake;                  /* set by signal/terminate, cleared by a wait */
    int wait_kind;
    void *wait_target;
    int has_native;            /* pthread_create succeeded */
    int registered;            /* thread_entry published its context root */
    NplThreadContext *context; /* owner context, for cancellation wakeups */
} NplThread;

/* T2b: a virtual thread is a one-shot scheduler continuation mounted on a
 * carrier pthread. All NplVal fields are GC-visited (gc.c case
 * BOX_VIRTUAL_THREAD); the suspended computation keeps no C stack frame.
 * The private CPS root (vthread_done) is a C closure bound to this record,
 * so a user call/cc continuation ends here instead of escaping into the
 * carrier loop. */
typedef struct NplVirtualThread {
    NplHeader header;
    NplVal name;
    NplVal end_result;
    NplVal end_exception;
    NplVal thunk;              /* initial CPS thunk (arity 1: trailing root) */
    NplVal root;               /* private CPS root closure, env = this vt */
    NplVal continuation;       /* one-shot suspended CPS continuation, or nil */
    NplVal work;               /* thunk awaiting a helper worker, or nil */
    NplVal resume_value;       /* helper result (or unspecified for a yield) */
    /* Saved dynamic environment, owned by the virtual thread, not the carrier. */
    NplVal wind;
    NplVal handler;
    NplVal parameters;
    NplVal parameter_extents;
    NplVal pending_extent_key;
    NplVal initial_handler;
    uint64_t next_wind_id;
    /* Native state (not GC-scanned). */
    int state;
    int park_count;
    int resume_count;
    int migration_count;       /* resumes on a different carrier */
    int carrier_index;         /* run queue that last mounted this vt */
    void *carrier_context;     /* NplThreadContext * that owns the bindings */
    struct NplVirtualThread *queue_next;
} NplVirtualThread;

typedef struct {
    NplHeader header;
    NplVal name;
    NplVal specific;
    NplVal owner;              /* thread object, or #f when not owned */
    NplVal waiters;            /* list of threads waiting to lock */
    int state;
} NplMutex;

typedef struct {
    NplHeader header;
    NplVal name;
    NplVal specific;
    NplVal waiters;            /* list of threads blocked on this CV */
} NplCondvar;

typedef struct {
    NplHeader header;
    double seconds;            /* monotonic seconds; SRFI 18 reference point */
} NplTime;

typedef struct {
    NplHeader header;
    int kind;
    NplVal reason;             /* uncaught-exception-reason, else unspecified */
} NplThreadException;

/* The single scheduler lock and condvar; every wait uses them. */
extern pthread_mutex_t npl_sched_lock;
extern pthread_cond_t npl_sched_cond;

/* Object constructors and tagging helpers (thread-sched.c). */
NplVal npl_boxed_thread(void *header);
NplVal npl_thread_make(NplVal thunk, NplVal name, NplVal parameters);
NplVal npl_mutex_make(NplVal name);
NplVal npl_condvar_make(NplVal name);
NplVal npl_thread_time(double seconds);
NplVal npl_thread_exception(int kind, NplVal reason);
NplVal npl_thread_current_object(void);

/* Called from coordinator.c when a safepoint observes cancellation. */
void npl_safepoint_cancelled(void);
/* Terminate the *current* thread: SRFI 18's initial continuation and initial
 * exception handler act on the (then) current thread, not on the thread whose
 * closure was captured. end_exception is npl_false() to keep the value the
 * caller already recorded. Never longjmps to a dead jmp_buf. */
void npl_thread_finish_current(NplVal end_exception);

/* Timeout decoding shared by thread-lifecycle.c / thread-sync.c. */
int npl_thread_timeout(NplVal timeout, struct timespec *deadline,
                       int *has_deadline, const char *who);
double npl_thread_real(NplVal value, const char *who);

/* Public C primitives backing (npl thread). All return one NplVal and take
 * the declared number of operands; generated code calls them by symbol. */
NplVal npl_thread_p(NplVal value);
NplVal npl_mutex_p(NplVal value);
NplVal npl_condvar_p(NplVal value);
NplVal npl_time_p(NplVal value);
NplVal npl_thread_name(NplVal value);
NplVal npl_thread_specific(NplVal value);
NplVal npl_thread_specific_set(NplVal value, NplVal object);
NplVal npl_mutex_name(NplVal value);
NplVal npl_mutex_specific(NplVal value);
NplVal npl_mutex_specific_set(NplVal value, NplVal object);
NplVal npl_condvar_name(NplVal value);
NplVal npl_condvar_specific(NplVal value);
NplVal npl_condvar_specific_set(NplVal value, NplVal object);
NplVal npl_mutex_state(NplVal value);
NplVal npl_current_time(void);
NplVal npl_current_exception_handler(void);
NplVal npl_time_seconds(NplVal value);
NplVal npl_seconds_time(NplVal value);
NplVal npl_join_timeout_exception_p(NplVal value);
NplVal npl_abandoned_mutex_exception_p(NplVal value);
NplVal npl_terminated_thread_exception_p(NplVal value);
NplVal npl_uncaught_exception_p(NplVal value);
NplVal npl_uncaught_exception_reason(NplVal value);

/* Thread lifecycle and synchronization primitives (thread-lifecycle.c). */
NplVal npl_thread_make_prim(NplVal thunk, NplVal name);
NplVal npl_thread_current(void);
NplVal npl_thread_start(NplVal value);
NplVal npl_thread_join(NplVal value, NplVal timeout, NplVal timeout_val,
                       NplVal supplied);
NplVal npl_thread_yield(void);
NplVal npl_thread_sleep(NplVal timeout);
NplVal npl_thread_terminate(NplVal value);
NplVal npl_mutex_make_prim(NplVal name);
NplVal npl_condvar_make_prim(NplVal name);
NplVal npl_mutex_lock(NplVal value, NplVal timeout, NplVal thread_value,
                      NplVal supplied);
NplVal npl_mutex_unlock(NplVal value, NplVal condvar_value, NplVal timeout,
                        NplVal supplied);
NplVal npl_condvar_signal(NplVal value);
NplVal npl_condvar_broadcast(NplVal value);

/* Scheduler lifetime helpers used by thread-sched.c / thread-lifecycle.c. */
void npl_sched_wake_all(void);
void npl_sched_abandon_mutexes(NplThread *thread);
void npl_sched_finish(NplThread *thread, int terminated);

/* T2b virtual-thread substrate (virtual-thread.c). The public library
 * (npl thread virtual) wraps these; C harnesses call them directly. */
NplVal npl_vthread_make(NplVal thunk, NplVal name, NplVal parameters);
NplVal npl_vthread_p(NplVal value);
NplVal npl_vthread_name(NplVal value);
NplVal npl_vthread_start(NplVal value);
NplVal npl_vthread_join(NplVal value);
NplVal npl_vthread_current(void);
NplVal npl_vthread_current_parameters(void);
NplVal npl_vthread_root(void);
NplVal npl_vthread_end_exception(NplVal value);
int npl_vthread_park_count(NplVal value);
int npl_vthread_resume_count(NplVal value);
int npl_vthread_migration_count(NplVal value);
void npl_virtual_thread_set_carriers(int count);
void npl_virtual_thread_run(void);
void npl_virtual_thread_shutdown(void);

#endif /* NPL_THREAD_SCHED_H */
