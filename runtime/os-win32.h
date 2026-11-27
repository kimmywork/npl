/*
 * os-win32.h — Windows host primitives for runtime/ (used only under _WIN32).
 *
 * Isolates Win32 API calls (VirtualAlloc, GetCurrentThreadStackLimits,
 * CreateProcessW, LoadLibraryW, PeekNamedPipe, temp dirs, executable path) so
 * the shared runtime files stay compact and POSIX code paths remain unchanged.
 */
#ifndef NPL_OS_WIN32_H
#define NPL_OS_WIN32_H

#ifdef _WIN32

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

void *npl_win32_map_chunk(size_t size);
void npl_win32_write_stderr(const char *msg, size_t len);
void npl_win32_stack_bounds(uintptr_t *low, uintptr_t *high);

/* Spawn argv[0] with optional stdio file redirections and wait for exit.
 * Returns 0 on success (with *exit_code set), -1 if the process could not be
 * started, or -2 if redirection setup failed. */
int npl_win32_spawn_wait(char *const *argv,
                         const char *input_path,
                         const char *output_path,
                         const char *error_path,
                         int *exit_code);

/* Newly created temporary directory path (malloc'd UTF-8 with '/' separators),
 * or NULL on failure. When root_buf is non-NULL, the chosen temp root is
 * copied into it for diagnostics without shared mutable state. */
char *npl_win32_make_temp_dir(char *root_buf, size_t root_cap);

/* UTF-8 directory removal via _wrmdir (preserves errno on failure). */
int npl_win32_rmdir(const char *path);

/* Absolute executable path (malloc'd UTF-8 with '/' separators), or NULL. */
char *npl_win32_executable_path(void);

/* Dynamic library loading and process-wide symbol lookup for runtime/ffi.c. */
int npl_win32_dlopen(const char *path, char *error_buffer, size_t error_size);
void *npl_win32_dlsym_default(const char *symbol);

/* Configure non-seekable input streams (pipes/consoles) unbuffered so
 * npl_win32_stream_ready observes all unread bytes accurately. */
void npl_win32_prepare_input_stream(FILE *stream);

/* Non-blocking readiness probe for FILE* backed by a Win32 handle. */
int npl_win32_stream_ready(FILE *stream);

/* Process-wide watchdog timer for runtime tests (analog of POSIX alarm()). */
void npl_win32_alarm(unsigned seconds);

#endif /* _WIN32 */
#endif /* NPL_OS_WIN32_H */
