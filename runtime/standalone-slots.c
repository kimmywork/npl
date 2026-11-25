/*
 * standalone-slots.c — definitive data slots for C-only harnesses.
 *
 * Generated NPL programs define `npl_stack_limit` (a QBE `thread data` TLS
 * slot), `npl_opaque_zero`, and the `npl_gc_stop_requested` world-stop flag
 * (plain QBE `data` slots) themselves; see src/compiler/backend/program.npl.
 * The runtime declares all three extern, so a C program that links
 * libnpl_runtime.a directly without any generated IL — gc_stress,
 * numeric_stress, thread_test, safepoint_test — must supply them. All
 * artifacts and native packages reach the same slots, never independent
 * copies.
 *
 * This file is linked only into those harnesses, not into npl_runtime.
 */

#include "npl.h"

NPL_TLS uintptr_t npl_stack_limit = 0;
uintptr_t npl_opaque_zero = 0;
uintptr_t npl_gc_stop_requested = 0;
