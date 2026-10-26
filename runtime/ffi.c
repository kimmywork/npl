/* Run-time foreign calls for npli (docs/design-primitives.md, section 5,
 * level A): load shared libraries and turn C functions written against
 * npl.h (NplVal arguments and result) into NPL procedures. All such
 * functions share one ABI, so fixed trampolines for 0..8 arguments cover
 * them without libffi. Library naming, search paths, and error policy are
 * NPL: src/repl/main.npl loads libraries (npli -l) and
 * lib/npl/evaluator-session.scm binds declared C primitives.
 *
 * Symbols resolve through the whole process (RTLD_DEFAULT): the host's own
 * runtime functions and every library loaded here with RTLD_GLOBAL. */

#define _DARWIN_C_SOURCE
#define _GNU_SOURCE

#include "port.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FFI_MAX_ARGUMENTS 8

/* ---- C pointers ---- */

NplVal npl_pointer(void *address)
{
    NplPointer *pointer = (NplPointer *)npl_alloc_object(sizeof(NplPointer));
    pointer->header.tag = BOX_POINTER;
    pointer->header.flags = 0;
    pointer->address = address;
    return (NplVal)(uintptr_t)pointer | TAG_PTR;
}

int npl_is_pointer(NplVal value)
{
    return npl_is_ptr(value) && npl_header(value)->tag == BOX_POINTER;
}

void *npl_pointer_address(NplVal value, const char *who)
{
    if (!npl_is_pointer(value))
        npl_primitive_error(who, "expected a pointer", value);
    return ((NplPointer *)npl_header(value))->address;
}

NplVal npl_pointer_p(NplVal value)
{
    return npl_is_pointer(value) ? npl_true() : npl_false();
}

NplVal npl_pointer_null_p(NplVal value)
{
    return npl_pointer_address(value, "pointer-null?") ? npl_false()
                                                       : npl_true();
}

NplVal npl_null_pointer(void)
{
    return npl_pointer(NULL);
}

/* The address as an exact integer, for display and hashing. */
NplVal npl_pointer_to_integer(NplVal value)
{
    uintptr_t address =
        (uintptr_t)npl_pointer_address(value, "pointer->integer");
    char text[24];
    snprintf(text, sizeof text, "%llu", (unsigned long long)address);
    return npl_integer_from_string(npl_string(text, (uint32_t)strlen(text)));
}

/* (ffi-load-library path): #t, or the loader's message as a string. */
NplVal npl_ffi_load_library(NplVal path_value)
{
    char *path = npl_c_string_internal(
        npl_require_bytes_internal(path_value, "ffi-load-library"));
    void *handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    free(path);
    if (handle) return npl_true();
    const char *message = dlerror();
    if (!message) message = "cannot load library";
    return npl_string(message, (uint32_t)strlen(message));
}

typedef NplVal (*FfiFunction0)(void);
typedef NplVal (*FfiFunction1)(NplVal);
typedef NplVal (*FfiFunction2)(NplVal, NplVal);
typedef NplVal (*FfiFunction3)(NplVal, NplVal, NplVal);
typedef NplVal (*FfiFunction4)(NplVal, NplVal, NplVal, NplVal);
typedef NplVal (*FfiFunction5)(NplVal, NplVal, NplVal, NplVal, NplVal);
typedef NplVal (*FfiFunction6)(NplVal, NplVal, NplVal, NplVal, NplVal,
                               NplVal);
typedef NplVal (*FfiFunction7)(NplVal, NplVal, NplVal, NplVal, NplVal,
                               NplVal, NplVal);
typedef NplVal (*FfiFunction8)(NplVal, NplVal, NplVal, NplVal, NplVal,
                               NplVal, NplVal, NplVal);

/* The closure environment is the function address as a fixnum. ISO C has
 * no conversion from an object pointer to a function pointer, so dlsym's
 * result is copied into the function pointer (the POSIX idiom). */
static void *ffi_address(NplVal environment)
{
    return (void *)(uintptr_t)npl_fixnum_value(environment);
}

/* CPS closure ABI: argv holds the arguments, then the continuation; the
 * caller checked the closure's arity. A C function that rejects an argument
 * raises through npl_primitive_error, which unwinds to the trampoline. */
static NplVal ffi_return(NplVal continuation, NplVal result)
{
    npl_tail_request_closure(continuation, 1, &result);
    return npl_nil();
}

#define FFI_CALL(n, call)                                                    \
    static NplVal ffi_call_##n(NplVal environment, int64_t argc,            \
                               NplVal *argv)                                \
    {                                                                       \
        FfiFunction##n function;                                            \
        void *address = ffi_address(environment);                           \
        (void)argc;                                                         \
        memcpy(&function, &address, sizeof function);                       \
        return ffi_return(argv[n], call);                                   \
    }

FFI_CALL(0, function())
FFI_CALL(1, function(argv[0]))
FFI_CALL(2, function(argv[0], argv[1]))
FFI_CALL(3, function(argv[0], argv[1], argv[2]))
FFI_CALL(4, function(argv[0], argv[1], argv[2], argv[3]))
FFI_CALL(5, function(argv[0], argv[1], argv[2], argv[3], argv[4]))
FFI_CALL(6, function(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5]))
FFI_CALL(7, function(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
                     argv[6]))
FFI_CALL(8, function(argv[0], argv[1], argv[2], argv[3], argv[4], argv[5],
                     argv[6], argv[7]))

static const NplGenericFunction FFI_TRAMPOLINES[FFI_MAX_ARGUMENTS + 1] = {
    ffi_call_0, ffi_call_1, ffi_call_2, ffi_call_3, ffi_call_4,
    ffi_call_5, ffi_call_6, ffi_call_7, ffi_call_8,
};

/* (ffi-procedure symbol count): a procedure of count arguments calling the
 * C function symbol, or #f when no loaded code defines symbol. */
NplVal npl_ffi_procedure(NplVal symbol_value, NplVal count_value)
{
    if (!npl_is_fixnum(count_value) || npl_fixnum_value(count_value) < 0 ||
        npl_fixnum_value(count_value) > FFI_MAX_ARGUMENTS)
        npl_primitive_error("ffi-procedure",
                            "expected an argument count from 0 to 8",
                            count_value);
    char *symbol = npl_c_string_internal(
        npl_require_bytes_internal(symbol_value, "ffi-procedure"));
    void *address = dlsym(RTLD_DEFAULT, symbol);
    free(symbol);
    if (!address) return npl_false();
    NplVal environment = npl_fixnum((int64_t)(uintptr_t)address);
    if (ffi_address(environment) != address)
        npl_primitive_error("ffi-procedure", "address out of fixnum range",
                            symbol_value);
    int64_t count = npl_fixnum_value(count_value);
    return npl_make_closure(FFI_TRAMPOLINES[count], environment, count + 1);
}
