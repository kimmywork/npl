/* Internal contract between virtual-thread.c (park/await, helper and carrier
 * loops) and virtual-thread-root.c (saved dynamic state and the private CPS
 * root).  Not part of the runtime public ABI. */
#ifndef NPL_VIRTUAL_THREAD_INTERNAL_H
#define NPL_VIRTUAL_THREAD_INTERNAL_H

#include "thread-sched.h"

/* Stores into an existing (possibly old) heap object need a barrier. */
static inline void npl_vthread_set_field(NplVirtualThread *vt,
                                         NplVal *field, NplVal value)
{
    *field = value;
    npl_gc_write_barrier(vt, value);
}

void npl_vthread_save_state(NplVirtualThread *vt);
void npl_vthread_restore_state(NplVirtualThread *vt);
void npl_vthread_clear_carrier_state(void);
void npl_vthread_mount_new(NplVirtualThread *vt);
NplVal npl_vthread_root_finish_closure(NplVal root);
NplVal npl_vthread_uncaught(NplVal environment, int64_t argc, NplVal *argv);
void npl_vthread_finish_record(NplVirtualThread *vt, NplVal result,
                               NplVal exception);

#endif
