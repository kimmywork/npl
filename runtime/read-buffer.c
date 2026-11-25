/*
 * read-buffer.c — longjmp-safe scratch buffer for the datum reader.
 * Extracted from read.c so that file stays under the project's 800-line
 * soft limit.
 */
#include "npl.h"
#include "read-buffer.h"
#include <stdlib.h>

static void free_read_buffer(void *argument)
{
    NplReadBuffer *buffer = (NplReadBuffer *)argument;
    free(buffer->data);
    free(buffer);
}

NplReadBuffer *npl_read_buffer_new(void)
{
    NplReadBuffer *buffer = (NplReadBuffer *)malloc(sizeof(*buffer));
    if (!buffer) npl_runtime_oom_internal();
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
    npl_cleanup_push(free_read_buffer, buffer);
    return buffer;
}

void npl_read_buffer_push(NplReadBuffer *buffer, int code)
{
    if (buffer->length + 5 >= buffer->capacity) {
        size_t next = buffer->capacity ? buffer->capacity * 2 : 32;
        char *grown = (char *)realloc(buffer->data, next);
        if (!grown) npl_runtime_oom_internal();
        buffer->data = grown;
        buffer->capacity = next;
    }
    if (code == 0) {
        buffer->data[buffer->length++] = '\0';
        return;
    }
    buffer->length += npl_utf8_encode_internal(
        (uint32_t)code, (uint8_t *)buffer->data + buffer->length);
}

void npl_read_buffer_finish(NplReadBuffer *buffer)
{
    free(buffer->data);
    free(buffer);
    npl_cleanup_pop();
}
