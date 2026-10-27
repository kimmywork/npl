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
#include <errno.h>
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

/* ---- C type conversions for generated stubs ----
 * nplc --emit=c-stubs (src/compiler/frontend/c-functions.npl) chooses the
 * conversion and the C range for each declared type; these check and
 * convert. Integers outside the fixnum range go through their decimal
 * text, which is rare and keeps the bignum representation private. */

/* Fixnums hold 62-bit signed values. */
#define FFI_FIXNUM_LIMIT (INT64_C(1) << 61)

static void ffi_integer_error(const char *who, const char *ctype,
                              NplVal value)
{
    char message[96];
    snprintf(message, sizeof message,
             "expected an exact integer that fits C %s", ctype);
    npl_primitive_error(who, message, value);
}

/* The decimal text of an exact integer, NUL-terminated, or 0 when it is
 * too long for any 64-bit value. */
static int ffi_integer_text(NplVal value, char *text, size_t size)
{
    NplVal string = npl_number_to_string(value);
    size_t length;
    int owned;
    const char *bytes =
        npl_string_utf8((const NplString *)npl_header(string), &length, &owned);
    int fits = length < size;
    if (fits) {
        memcpy(text, bytes, length);
        text[length] = '\0';
    }
    if (owned) free((void *)bytes);
    return fits;
}

int64_t npl_ffi_to_int64(NplVal value, int64_t minimum, int64_t maximum,
                         const char *ctype, const char *who)
{
    int64_t result = 0;
    if (npl_is_fixnum(value)) {
        result = npl_fixnum_value(value);
    } else if (npl_integer_p(value) == npl_true()) {
        char text[24];
        if (!ffi_integer_text(value, text, sizeof text))
            ffi_integer_error(who, ctype, value);
        errno = 0;
        long long parsed = strtoll(text, NULL, 10);
        if (errno == ERANGE) ffi_integer_error(who, ctype, value);
        result = (int64_t)parsed;
    } else {
        ffi_integer_error(who, ctype, value);
    }
    if (result < minimum || result > maximum)
        ffi_integer_error(who, ctype, value);
    return result;
}

uint64_t npl_ffi_to_uint64(NplVal value, uint64_t maximum, const char *ctype,
                           const char *who)
{
    uint64_t result = 0;
    if (npl_is_fixnum(value)) {
        if (npl_fixnum_value(value) < 0) ffi_integer_error(who, ctype, value);
        result = (uint64_t)npl_fixnum_value(value);
    } else if (npl_integer_p(value) == npl_true()) {
        char text[24];
        if (!ffi_integer_text(value, text, sizeof text) || text[0] == '-')
            ffi_integer_error(who, ctype, value);
        errno = 0;
        unsigned long long parsed = strtoull(text, NULL, 10);
        if (errno == ERANGE) ffi_integer_error(who, ctype, value);
        result = (uint64_t)parsed;
    } else {
        ffi_integer_error(who, ctype, value);
    }
    if (result > maximum) ffi_integer_error(who, ctype, value);
    return result;
}

double npl_ffi_to_double(NplVal value, const char *who)
{
    if (npl_number_p(value) != npl_true() || npl_is_complex(value))
        npl_primitive_error(who, "expected a real number", value);
    return npl_real_double(value);
}

/* Scheme truth: only #f is false. */
int npl_ffi_to_bool(NplVal value)
{
    return value != npl_false();
}

void *npl_ffi_to_pointer(NplVal value, const char *who)
{
    return npl_pointer_address(value, who);
}

/* A C string argument: a string whose UTF-8 has no NUL byte. */
void npl_ffi_check_string(NplVal value, const char *who)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_STRING)
        npl_primitive_error(who, "expected a string", value);
    size_t length;
    int owned;
    const char *bytes =
        npl_string_utf8((const NplString *)npl_header(value), &length, &owned);
    int has_nul = memchr(bytes, '\0', length) != NULL;
    if (owned) free((void *)bytes);
    if (has_nul)
        npl_primitive_error(who, "string contains a null character", value);
}

/* A malloc'd UTF-8 copy of a checked string; the stub frees it after the
 * call and after boxing results, which may point into it. */
char *npl_ffi_copy_string(NplVal value)
{
    return npl_c_string_internal((const NplString *)npl_header(value));
}

void npl_ffi_free(void *memory)
{
    free(memory);
}

NplVal npl_ffi_from_int64(int64_t value)
{
    if (value > -FFI_FIXNUM_LIMIT && value < FFI_FIXNUM_LIMIT)
        return npl_fixnum(value);
    char text[24];
    snprintf(text, sizeof text, "%lld", (long long)value);
    return npl_integer_from_string(npl_string(text, (uint32_t)strlen(text)));
}

NplVal npl_ffi_from_uint64(uint64_t value)
{
    if (value < (uint64_t)FFI_FIXNUM_LIMIT) return npl_fixnum((int64_t)value);
    char text[24];
    snprintf(text, sizeof text, "%llu", (unsigned long long)value);
    return npl_integer_from_string(npl_string(text, (uint32_t)strlen(text)));
}

NplVal npl_ffi_from_double(double value)
{
    return npl_float(value);
}

NplVal npl_ffi_from_bool(int value)
{
    return value ? npl_true() : npl_false();
}

/* A copy of a NUL-terminated UTF-8 result; NULL is #f. */
NplVal npl_ffi_from_string(const char *text)
{
    if (!text) return npl_false();
    return npl_string_from_utf8((const uint8_t *)text, strlen(text),
                                BOX_STRING);
}

NplVal npl_ffi_from_pointer(void *address)
{
    return npl_pointer(address);
}

NplVal npl_ffi_unspecified(void)
{
    return npl_unspecified();
}

NplVal npl_ffi_values(int64_t count, NplVal *values)
{
    return npl_make_values(count, values);
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
