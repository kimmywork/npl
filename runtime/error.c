/* Catchable primitive errors (.agents/archive/plan-r7rs.md "Primitive Errors").
 *
 * A safe primitive that rejects an argument calls npl_primitive_error. The
 * error becomes an R7RS error object (the record layout of
 * lib/scheme/error.scm: type symbol npl-error-object; kind, message,
 * irritants) raised non-continuably through the current handler chain.
 *
 * The primitive's C frame cannot continue, so the runtime unwinds to the
 * innermost trampoline with longjmp and lets that trampoline run the
 * handler (trampoline.c). With no handler, or no trampoline yet, the error is
 * printed as "who: message: irritant" and the process exits with status 1.
 *
 * Every error a program can cause through a primitive's arguments or the
 * operating system is raised this way. The remaining fprintf-and-exit sites
 * in runtime/ are internal invariant failures (compiler-emitted calls with
 * impossible arguments, corrupted runtime frames) and the final report of an
 * uncaught raise. */

#include "npl.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static NplVal make_error_object(const char *kind, const char *who,
                                const char *message, NplVal irritant)
{
    npl_gc_root(&irritant);
    size_t who_length = strlen(who);
    size_t message_length = strlen(message);
    char *text = (char *)malloc(who_length + message_length + 3);
    if (!text) npl_runtime_oom_internal();
    memcpy(text, who, who_length);
    memcpy(text + who_length, ": ", 2);
    memcpy(text + who_length + 2, message, message_length + 1);
    NplVal message_value =
        npl_string(text, (uint32_t)(who_length + message_length + 2));
    free(text);
    npl_gc_root(&message_value);
    NplVal irritants = npl_cons(irritant, npl_nil());
    npl_gc_root(&irritants);
    NplVal fields[4];
    fields[0] = npl_symbol("npl-error-object", 16);
    fields[1] = npl_symbol(kind, (uint32_t)strlen(kind));
    fields[2] = message_value;
    fields[3] = irritants;
    NplVal object = npl_make_record(4, fields);
    npl_gc_unroot(&irritants);
    npl_gc_unroot(&message_value);
    npl_gc_unroot(&irritant);
    return object;
}

void npl_report_error_object(NplVal object)
{
    /* Mirrors the uncaught report of other raised values: message, then
     * each irritant in write form, on stderr. */
    if (npl_is_ptr(object) && npl_header(object)->tag == BOX_RECORD) {
        NplVector *record = (NplVector *)npl_header(object);
        if (record->length == 4) {
            NplVal error_port = npl_current_error_port();
            npl_display_port(record->data[2], error_port);
            for (NplVal rest = record->data[3];
                 npl_is_ptr(rest) && npl_header(rest)->tag == BOX_PAIR;
                 rest = npl_pair_ptr(rest)->cdr) {
                npl_display_port(npl_string(": ", 2), error_port);
                npl_write_port(npl_pair_ptr(rest)->car, error_port);
            }
            npl_newline_port(error_port);
            npl_flush_output_port(error_port);
            return;
        }
    }
    fprintf(stderr, "uncaught exception\n");
}

static void raise_error_object(NplVal object)
{
    if (!npl_raise_from_primitive(object)) {
        npl_report_error_object(object);
        exit(1);
    }
    /* npl_raise_from_primitive does not return when a handler exists. */
    exit(1);
}

/* (raise object) in the direct entry of a compiled function (compiler
 * frontend/direct-calls.npl): a non-continuable raise, delivered as a
 * primitive error is, so the direct frames are discarded and the innermost
 * trampoline calls the handler. Returns its argument only to give the
 * call a value; it never returns. */
NplVal npl_raise_direct(NplVal object)
{
    raise_error_object(object);
    return object;
}

void npl_primitive_error(const char *who, const char *message,
                         NplVal irritant)
{
    raise_error_object(make_error_object("error", who, message, irritant));
}

/* Raise an already constructed error record.  Core native packages use this
 * to attach their own kind (for example "posix") to the standard layout. */
void npl_raise_record(NplVal object)
{
    raise_error_object(object);
}

/* A syntax error from the datum reader: satisfies read-error?. The message
 * carries no "read:" prefix and the irritants are empty, as in the error
 * (scheme read) builds from read-result. */
void npl_primitive_read_error(const char *message)
{
    NplVal message_value = npl_string(message, (uint32_t)strlen(message));
    npl_gc_root(&message_value);
    NplVal fields[4];
    fields[0] = npl_symbol("npl-error-object", 16);
    fields[1] = npl_symbol("read", 4);
    fields[2] = message_value;
    fields[3] = npl_nil();
    NplVal object = npl_make_record(4, fields);
    npl_gc_unroot(&message_value);
    raise_error_object(object);
}

/* An operating-system refusal on a file: satisfies file-error?. */
void npl_primitive_file_error(const char *who, const char *message,
                              NplVal irritant)
{
    raise_error_object(make_error_object("file", who, message, irritant));
}

/* The symbolic name of an errno value, for posix error objects.  The list is
 * deliberately finite: unlisted codes still produce a posix error, with the
 * EUNKNOWN name. */
static const char *posix_error_name(int code)
{
    switch (code) {
#define CASE(name) case name: return #name
        CASE(EACCES); CASE(EEXIST); CASE(EINVAL); CASE(EISDIR);
        CASE(ENOENT); CASE(ENOTDIR); CASE(ENOTEMPTY); CASE(EPERM);
        CASE(ENOSPC); CASE(EROFS); CASE(EMFILE); CASE(ENFILE);
#ifdef ELOOP
        CASE(ELOOP);
#endif
#ifdef ENAMETOOLONG
        CASE(ENAMETOOLONG);
#endif
#ifdef ENOTSUP
        CASE(ENOTSUP);
#endif
        CASE(EBADF);
        CASE(EAGAIN); CASE(EINTR); CASE(ENOMEM); CASE(EXDEV);
        CASE(ESRCH); CASE(ECHILD); CASE(ENOSYS);
#undef CASE
        default: return "EUNKNOWN";
    }
}

/* An operating-system refusal with kind "posix": satisfies posix-error?
 * (lib/npl/unix.sld).  The first irritant is the symbolic errno name, as
 * SRFI 170 specifies; the caller's irritant follows. */
void npl_primitive_posix_error(const char *who, int code, NplVal irritant)
{
    const char *name = posix_error_name(code);
    const char *message = strerror(code);
    npl_gc_root(&irritant);
    NplVal name_value = npl_symbol(name, (uint32_t)strlen(name));
    npl_gc_root(&name_value);
    NplVal message_value =
        npl_string(message, (uint32_t)strlen(message));
    npl_gc_root(&message_value);
    NplVal irritants = npl_cons(name_value, npl_cons(irritant, npl_nil()));
    npl_gc_root(&irritants);
    NplVal fields[4];
    fields[0] = npl_symbol("npl-error-object", 16);
    fields[1] = npl_symbol("posix", 5);
    fields[2] = message_value;
    fields[3] = irritants;
    NplVal object = npl_make_record(4, fields);
    npl_gc_unroot(&irritants);
    npl_gc_unroot(&message_value);
    npl_gc_unroot(&name_value);
    npl_gc_unroot(&irritant);
    raise_error_object(object);
}
