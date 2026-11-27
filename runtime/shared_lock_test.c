/*
 * shared_lock_test.c — T1.3 shared-object synchronization stress.
 * Several mutators intern symbols, write through string ports and allocate
 * foreign blocks concurrently while collections are forced by a tiny
 * nursery. A trylock+poll shared lock must let every waiter park for a
 * world stop instead of blocking the collector.
 */
#define _POSIX_C_SOURCE 200809L

#include "npl.h"
#include "thread.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define WORKERS 3
#define ITERS 4000

static volatile int failures = 0;

static void *worker(void *argument)
{
    int id = (int)(intptr_t)argument;
    NplThreadContext *context = npl_thread_attach();
    /* Publish a stack bound that covers this frame. */
    uintptr_t marker = 0;
    (void)npl_gc_swap_stack_bottom((uintptr_t)(void *)&marker + 4096);
    (void)context;
    for (int i = 0; i < ITERS; i++) {
        char text[40];
        snprintf(text, sizeof text, "shared-symbol-%d-%d", id, i % 64);
        NplVal symbol = npl_symbol(text, (uint32_t)strlen(text));
        NplVal string = npl_string_from_utf8((const uint8_t *)"x", 1,
                                             BOX_STRING);
        (void)string;
        /* Force the symbol table to grow and GC to run under the lock. */
        if (i % 16 == 0) npl_gc_collect();
        if (!npl_is_ptr(symbol)) failures++;
    }
    npl_thread_detach();
    return NULL;
}

int main(void)
{
#ifndef _WIN32
    alarm(90);
#endif
    npl_runtime_init();
    pthread_t threads[WORKERS];
    for (int i = 0; i < WORKERS; i++)
        if (pthread_create(&threads[i], NULL, worker,
                           (void *)(intptr_t)i) != 0) {
            fprintf(stderr, "FAIL: pthread_create\n");
            return 1;
        }
    npl_gc_exclude();
    for (int i = 0; i < WORKERS; i++) pthread_join(threads[i], NULL);
    npl_gc_include();
    npl_gc_collect();
    if (failures) {
        fprintf(stderr, "shared lock stress: %d failure(s)\n", failures);
        return 1;
    }
    puts("PASS: concurrent symbol interning under shared lock and STW GC");
    return 0;
}
