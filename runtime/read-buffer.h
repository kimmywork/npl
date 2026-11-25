/*
 * read-buffer.h — longjmp-safe scratch buffer for the datum reader.
 *
 * A cancellation or primitive-error longjmp can skip the C frame that owns a
 * malloc'd reader buffer. The holder lives on the heap and is registered on
 * the thread's cleanup stack, so npl_shared_release_all frees it at the
 * unwind landing.
 */
#ifndef NPL_READ_BUFFER_H
#define NPL_READ_BUFFER_H

#include <stddef.h>

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} NplReadBuffer;

NplReadBuffer *npl_read_buffer_new(void);
void npl_read_buffer_push(NplReadBuffer *buffer, int code);
void npl_read_buffer_finish(NplReadBuffer *buffer);

#endif /* NPL_READ_BUFFER_H */
