/*
 * blocking_io_test.c — T1.3 blocking-call GC boundary.
 *
 * A reader thread blocks in fread() on a pipe with no writer while the main
 * thread forces full collections. The reader must publish its safe context
 * and park (npl_gc_block_begin), so the collector neither waits forever for
 * it nor loses a value reachable only from its C stack. A blocking waitpid
 * in run-process is covered by the same boundary.
 */
#define _POSIX_C_SOURCE 200809L

#include "npl.h"
#include "thread.h"
#include <pthread.h>
#include <stdio.h>
#ifdef _WIN32
#include "os-win32.h"
#include <fcntl.h>
#include <io.h>
#endif
#include <unistd.h>

static int failures = 0;

typedef struct {
    int read_fd;
    volatile int entered;
    volatile int done;
} Shared;

static void *reader(void *argument)
{
    Shared *shared = (Shared *)argument;
    NplThreadContext *context = npl_thread_attach();
    uintptr_t marker = 0;
    (void)npl_gc_swap_stack_bottom((uintptr_t)(void *)&marker + 4096);
    (void)context;

    NplVal port = npl_port_from_fd(shared->read_fd, 1, 0, 1);
    npl_gc_root(&port);
    /* Not rooted: survives the collections only if the collector scans this
     * parked thread's C stack. */
    NplVal keep = npl_cons(npl_fixnum(101), npl_fixnum(202));
    __atomic_store_n(&shared->entered, 1, __ATOMIC_RELEASE);

    NplVal character = npl_read_char(port);   /* blocks until a byte arrives */
    (void)character;

    if (npl_car(keep) != npl_fixnum(101)) failures++;
    if (npl_cdr(keep) != npl_fixnum(202)) failures++;
    npl_gc_unroot(&port);
    __atomic_store_n(&shared->done, 1, __ATOMIC_RELEASE);
    npl_thread_detach();
    return NULL;
}

int main(void)
{
#ifdef _WIN32
    npl_win32_alarm(60);
#else
    alarm(60);
#endif
    npl_runtime_init();
    int fds[2];
#ifdef _WIN32
    if (_pipe(fds, 4096, _O_BINARY) != 0) {
#else
    if (pipe(fds) != 0) {
#endif
        fprintf(stderr, "FAIL: pipe\n");
        return 1;
    }
    Shared shared = { fds[0], 0, 0 };
    pthread_t thread;
    if (pthread_create(&thread, NULL, reader, &shared) != 0) {
        fprintf(stderr, "FAIL: pthread_create\n");
        return 1;
    }
    while (!__atomic_load_n(&shared.entered, __ATOMIC_ACQUIRE)) usleep(1000);
    /* Let the reader reach fread before collecting. */
    usleep(50000);
    for (int i = 0; i < 30; i++) npl_gc_collect();
    if (write(fds[1], "A", 1) != 1) {
        fprintf(stderr, "FAIL: write\n");
        return 1;
    }
    npl_gc_exclude();
    pthread_join(thread, NULL);
    npl_gc_include();
    close(fds[1]);
    if (!__atomic_load_n(&shared.done, __ATOMIC_ACQUIRE)) failures++;
    npl_gc_collect();
    if (failures) {
        fprintf(stderr, "blocking io: %d failure(s)\n", failures);
        return 1;
    }
    puts("PASS: blocking fread parks for GC and keeps stack-only values alive");
    return 0;
}
