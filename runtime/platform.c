/* Target platform identity for cond-expand and (npl platform).
 *
 * cond-expand runs while libraries are being expanded, before any library
 * has been loaded, so the feature source is a bootstrap hook rather than a
 * native package.  Keep this file to vocabulary only: C code keeps its
 * #ifdefs, NPL code uses these feature symbols, and package manifests use
 * the same names.
 */

#include "npl.h"

#include <string.h>

static int platform_name_is(const char *name, uint32_t length,
                            const char *literal)
{
    return length == (uint32_t)strlen(literal) &&
           memcmp(name, literal, length) == 0;
}

/* One predicate so (features), cond-expand, and (npl platform) cannot drift
 * on OS, architecture, or width. */
NplVal npl_platform_feature(NplVal name_value)
{
    const NplString *name = npl_require_bytes_internal(
        name_value, "platform-feature?");
    const char *bytes = name->data;
    uint32_t length = name->length;

    if (platform_name_is(bytes, length, "darwin")) {
#if defined(__APPLE__)
        return npl_true();
#else
        return npl_false();
#endif
    }
    if (platform_name_is(bytes, length, "linux")) {
#if defined(__linux__)
        return npl_true();
#else
        return npl_false();
#endif
    }
    if (platform_name_is(bytes, length, "windows")) {
#if defined(_WIN32)
        return npl_true();
#else
        return npl_false();
#endif
    }
    if (platform_name_is(bytes, length, "aarch64") ||
        platform_name_is(bytes, length, "arm64")) {
#if defined(__aarch64__) || defined(__arm64__)
        return npl_true();
#else
        return npl_false();
#endif
    }
    if (platform_name_is(bytes, length, "x86_64") ||
        platform_name_is(bytes, length, "amd64")) {
#if defined(__x86_64__) || defined(_M_X64)
        return npl_true();
#else
        return npl_false();
#endif
    }
    if (platform_name_is(bytes, length, "64-bit"))
        return sizeof(void *) == 8 ? npl_true() : npl_false();
    if (platform_name_is(bytes, length, "32-bit"))
        return sizeof(void *) == 4 ? npl_true() : npl_false();
    if (platform_name_is(bytes, length, "little-endian")) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        return npl_false();
#else
        return npl_true();
#endif
    }
    if (platform_name_is(bytes, length, "big-endian")) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        return npl_true();
#else
        return npl_false();
#endif
    }
    /* Implemented SRFI feature identifiers. Keep this and (features) in
     * lib/scheme/text-io.scm in sync; adding one here needs no seed relay
     * because cond-expand consults this bootstrap hook at compile time. */
    if (platform_name_is(bytes, length, "srfi-18"))
        return npl_true();
    if (platform_name_is(bytes, length, "srfi-230"))
        return npl_true();
    if (platform_name_is(bytes, length, "virtual-threads"))
        return npl_true();
    return npl_false();
}

NplVal npl_platform_features(void)
{
    NplVal result = npl_nil();
#if defined(__APPLE__)
    result = npl_cons(npl_symbol("darwin", 6), result);
#endif
#if defined(__linux__)
    result = npl_cons(npl_symbol("linux", 5), result);
#endif
#if defined(_WIN32)
    result = npl_cons(npl_symbol("windows", 7), result);
#endif
#if defined(__aarch64__) || defined(__arm64__)
    result = npl_cons(npl_symbol("aarch64", 7), result);
#endif
#if defined(__x86_64__) || defined(_M_X64)
    result = npl_cons(npl_symbol("x86_64", 6), result);
#endif
    if (sizeof(void *) == 8)
        result = npl_cons(npl_symbol("64-bit", 6), result);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    result = npl_cons(npl_symbol("big-endian", 10), result);
#else
    result = npl_cons(npl_symbol("little-endian", 13), result);
#endif
    return result;
}

/* Diagnostic/build triple.  It describes the target; today nplc is a native
 * compiler, so target and build host coincide. */
NplVal npl_platform_triple(void)
{
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    return npl_string("aarch64-apple-darwin", 20);
#elif defined(__APPLE__) && (defined(__x86_64__) || defined(_M_X64))
    return npl_string("x86_64-apple-darwin", 19);
#elif defined(__linux__) && (defined(__aarch64__) || defined(__arm64__))
    return npl_string("aarch64-unknown-linux-gnu", 25);
#elif defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
    return npl_string("x86_64-unknown-linux-gnu", 24);
#elif defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))
    return npl_string("x86_64-pc-windows-gnu", 21);
#else
    return npl_string("unknown-unknown-unknown", 23);
#endif
}
