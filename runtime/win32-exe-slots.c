/*
 * win32-exe-slots.c — Windows PE/DLL slot binding for shared-runtime executables.
 *
 * PE/COFF DLLs cannot leave symbols unresolved against the host executable.
 * Every compiled NPL executable defines `npl_stack_limit` (PE .tls$) and
 * `npl_gc_stop_requested` (.data) in its own module so generated code reads
 * them with zero DLL indirection.  Linking this object into any executable
 * that uses libnpl_runtime.dll registers those executable-owned slots with
 * the DLL before main() runs, so both the executable and DLL share one
 * per-thread stack limit and one world-stop flag.
 */
#ifdef _WIN32

#include "npl.h"

static uintptr_t npl_exe_get_stack_limit(void)
{
    return npl_stack_limit;
}

static void npl_exe_set_stack_limit(uintptr_t value)
{
    npl_stack_limit = value;
}

__attribute__((constructor))
static void npl_win32_bind_exe_slots(void)
{
    npl_win_register_slots(&npl_gc_stop_requested,
                           npl_exe_get_stack_limit,
                           npl_exe_set_stack_limit);
}

#endif /* _WIN32 */
