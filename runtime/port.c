#define _DARWIN_C_SOURCE

/* Runtime ports: port objects, byte and UTF-8 character I/O, memory and
 * file ports, and the current-port parameters. See port.h for the module
 * split. */

#include "port.h"
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static NplVal g_current_input_port = 2;
static NplVal g_current_output_port = 2;
static NplVal g_current_error_port = 2;
static NplVal g_eof_object = 2;

static NplVal make_port(FILE *stream, uint32_t mode)
{
    NplPort *port = (NplPort *)npl_alloc_object(sizeof(NplPort));
    port->header.tag = BOX_PORT;
    port->stream = stream;
    port->buffer = npl_nil();
    port->mode = mode;
    port->position = 0;
    port->length = 0;
    port->pushed = 0;
    return (NplVal)(uintptr_t)port | TAG_PTR;
}

static NplVal make_memory_port(NplVal buffer, uint32_t mode,
                               uint32_t length)
{
    NplVal value = make_port(NULL, mode | NPL_PORT_MEMORY);
    NplPort *port = (NplPort *)npl_header(value);
    port->buffer = buffer;
    port->length = length;
    return value;
}

static NplVal make_byte_buffer(uint32_t capacity)
{
    NplBytevector *buffer = (NplBytevector *)npl_alloc_object(
        sizeof(NplBytevector) + (size_t)capacity);
    buffer->header.tag = BOX_BYTEVECTOR;
    buffer->header.flags = 0;
    buffer->length = capacity;
    buffer->capacity = capacity;
    return (NplVal)(uintptr_t)buffer | TAG_PTR;
}

static NplBytevector *require_bytevector(NplVal value, const char *who)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_BYTEVECTOR)
        npl_primitive_error(who, "expected a bytevector", value);
    return (NplBytevector *)npl_header(value);
}

static NplVal copy_bytevector_range(const uint8_t *bytes, uint32_t length)
{
    NplVal value = make_byte_buffer(length);
    NplBytevector *buffer = (NplBytevector *)npl_header(value);
    if (length) memcpy(buffer->data, bytes, length);
    return value;
}

static NplString *require_string(NplVal value, const char *who)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_STRING)
        npl_primitive_error(who, "expected a string", value);
    return (NplString *)npl_header(value);
}

/* A malloc'd NUL-terminated UTF-8 copy of a string, for C APIs (paths,
 * environment, process arguments); the caller frees it. */
char *npl_c_string_internal(const NplString *string)
{
    size_t length;
    int owned;
    const char *bytes = npl_string_utf8(string, &length, &owned);
    if (owned) return (char *)bytes;
    char *copy = (char *)malloc(length + 1);
    if (!copy) npl_runtime_oom_internal();
    memcpy(copy, bytes, length);
    copy[length] = '\0';
    return copy;
}

/* Text from C (UTF-8) as a string. */
NplVal npl_c_text_internal(const char *text, size_t length)
{
    return npl_string_from_utf8((const uint8_t *)text, length, BOX_STRING);
}

/* Textual ports are UTF-8 byte streams: memory string ports keep their text
 * as UTF-8 in a bytevector buffer, like binary memory ports. */
static NplVal utf8_bytevector(const NplString *string)
{
    size_t length;
    int owned;
    const char *bytes = npl_string_utf8(string, &length, &owned);
    if (length > UINT32_MAX) npl_runtime_oom_internal();
    NplVal value = copy_bytevector_range((const uint8_t *)bytes,
                                         (uint32_t)length);
    if (owned) free((void *)bytes);
    return value;
}

static NplVal make_eof_object(void)
{
    NplEof *eof = (NplEof *)npl_alloc_object(sizeof(NplEof));
    eof->header.tag = BOX_EOF;
    return (NplVal)(uintptr_t)eof | TAG_PTR;
}

void npl_port_init_internal(void)
{
    /* The current ports are parameter objects (R7RS 6.13.1); the globals
     * hold the parameters, and parameterize rebinds their values. */
    npl_gc_root(&g_current_input_port);
    npl_gc_root(&g_current_output_port);
    npl_gc_root(&g_current_error_port);
    g_current_input_port = npl_make_parameter(npl_nil(), make_port(stdin,
        NPL_PORT_INPUT | NPL_PORT_TEXTUAL | NPL_PORT_BINARY));
    g_current_output_port = npl_make_parameter(npl_nil(), make_port(stdout,
        NPL_PORT_OUTPUT | NPL_PORT_TEXTUAL | NPL_PORT_BINARY));
    g_current_error_port = npl_make_parameter(npl_nil(), make_port(stderr,
        NPL_PORT_OUTPUT | NPL_PORT_TEXTUAL | NPL_PORT_BINARY));
    g_eof_object = make_eof_object();
    npl_gc_root(&g_eof_object);
}

/* ---- standard port substrate ---- */

static NplPort *port_value(NplVal value, const char *who)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_PORT)
        npl_primitive_error(who, "expected a port", value);
    return (NplPort *)npl_header(value);
}

static NplPort *require_port(NplVal value, uint32_t access,
                             const char *who)
{
    NplPort *port = port_value(value, who);
    if ((port->mode & NPL_PORT_CLOSED) != 0)
        npl_primitive_error(who, "port is closed", value);
    if ((port->mode & access) == 0)
        npl_primitive_error(who, "wrong port direction", value);
    return port;
}

static NplPort *require_port_mode(NplVal value, uint32_t access,
                                  uint32_t kind, const char *who)
{
    NplPort *port = require_port(value, access, who);
    if ((port->mode & kind) == 0)
        npl_primitive_error(who, "wrong port type", value);
    return port;
}

/* Open a file port, or return #f when the OS refuses (missing file,
 * permission). (scheme file) turns #f into a file-error object. */
static NplVal try_open_file_port(NplVal path, const char *mode,
                                 uint32_t port_mode, const char *who)
{
    char *buffer = npl_c_string_internal(npl_require_bytes_internal(path, who));
    FILE *stream = fopen(buffer, mode);
    free(buffer);
    return stream ? make_port(stream, port_mode | NPL_PORT_OWNED)
                  : npl_false();
}

/* kind: 0 text input, 1 text output, 2 binary input, 3 binary output. */
NplVal npl_try_open_file(NplVal path, NplVal kind)
{
    switch (npl_is_fixnum(kind) ? npl_fixnum_value(kind) : -1) {
    case 0: return try_open_file_port(path, "rb",
                NPL_PORT_INPUT | NPL_PORT_TEXTUAL, "open-input-file");
    case 1: return try_open_file_port(path, "wb",
                NPL_PORT_OUTPUT | NPL_PORT_TEXTUAL, "open-output-file");
    case 2: return try_open_file_port(path, "rb",
                NPL_PORT_INPUT | NPL_PORT_BINARY, "open-binary-input-file");
    case 3: return try_open_file_port(path, "wb",
                NPL_PORT_OUTPUT | NPL_PORT_BINARY, "open-binary-output-file");
    default:
        npl_primitive_error("open file", "invalid port kind", kind);
        return npl_nil();
    }
}

static NplVal open_file_port(NplVal path, const char *mode,
                             uint32_t port_mode, const char *who)
{
    NplVal port = try_open_file_port(path, mode, port_mode, who);
    if (npl_is_false(port))
        npl_primitive_file_error(who, "cannot open file", path);
    return port;
}

NplVal npl_current_input_port(void)
{
    return npl_parameter_ref(g_current_input_port);
}
NplVal npl_current_output_port(void)
{
    return npl_parameter_ref(g_current_output_port);
}
NplVal npl_current_error_port(void)
{
    return npl_parameter_ref(g_current_error_port);
}

/* kind: 0 input, 1 output, 2 error; the parameter object itself. */
NplVal npl_port_parameter(NplVal kind)
{
    switch (npl_is_fixnum(kind) ? npl_fixnum_value(kind) : -1) {
    case 0: return g_current_input_port;
    case 1: return g_current_output_port;
    case 2: return g_current_error_port;
    default:
        npl_primitive_error("port parameter", "invalid port kind", kind);
        return npl_nil();
    }
}

NplVal npl_open_input_file(NplVal path)
{
    return open_file_port(path, "rb", NPL_PORT_INPUT | NPL_PORT_TEXTUAL,
        "open-input-file");
}

NplVal npl_open_output_file(NplVal path)
{
    return open_file_port(path, "wb", NPL_PORT_OUTPUT | NPL_PORT_TEXTUAL,
        "open-output-file");
}

NplVal npl_open_binary_input_file(NplVal path)
{
    return open_file_port(path, "rb", NPL_PORT_INPUT | NPL_PORT_BINARY,
                          "open-binary-input-file");
}

NplVal npl_open_binary_output_file(NplVal path)
{
    return open_file_port(path, "wb", NPL_PORT_OUTPUT | NPL_PORT_BINARY,
                          "open-binary-output-file");
}

NplVal npl_open_input_bytevector(NplVal value)
{
    NplBytevector *bytes = require_bytevector(value, "open-input-bytevector");
    return make_memory_port(value, NPL_PORT_INPUT | NPL_PORT_BINARY,
                            bytes->length);
}

NplVal npl_open_output_bytevector(void)
{
    return make_memory_port(make_byte_buffer(64),
                            NPL_PORT_OUTPUT | NPL_PORT_BINARY, 0);
}

NplVal npl_get_output_bytevector(NplVal value)
{
    NplPort *port = require_port(value, NPL_PORT_OUTPUT,
                                 "get-output-bytevector");
    if ((port->mode & (NPL_PORT_MEMORY | NPL_PORT_BINARY)) !=
        (NPL_PORT_MEMORY | NPL_PORT_BINARY))
        npl_primitive_error("get-output-bytevector",
                            "expected a bytevector output port", value);
    NplBytevector *buffer = require_bytevector(
        port->buffer, "get-output-bytevector");
    return copy_bytevector_range(buffer->data, port->length);
}

NplVal npl_open_input_string(NplVal value)
{
    NplString *string = require_string(value, "open-input-string");
    NplVal bytes = utf8_bytevector(string);
    return make_memory_port(bytes, NPL_PORT_INPUT | NPL_PORT_TEXTUAL,
                            ((NplBytevector *)npl_header(bytes))->length);
}

NplVal npl_open_output_string(void)
{
    return make_memory_port(make_byte_buffer(64),
                            NPL_PORT_OUTPUT | NPL_PORT_TEXTUAL, 0);
}

NplVal npl_get_output_string(NplVal value)
{
    NplPort *port = require_port(value, NPL_PORT_OUTPUT,
                                 "get-output-string");
    if ((port->mode & (NPL_PORT_MEMORY | NPL_PORT_TEXTUAL)) !=
        (NPL_PORT_MEMORY | NPL_PORT_TEXTUAL))
        npl_primitive_error("get-output-string",
                            "expected a string output port", value);
    NplBytevector *buffer = require_bytevector(port->buffer,
                                               "get-output-string");
    return npl_string_from_utf8(buffer->data, port->length, BOX_STRING);
}

static NplVal close_port_direction(NplVal value, uint32_t access,
                                   const char *who)
{
    NplPort *port = port_value(value, who);
    if ((port->mode & access) == 0)
        npl_primitive_error(who, "wrong port direction", value);
    if ((port->mode & NPL_PORT_CLOSED) != 0) return npl_nil();
    if ((port->mode & NPL_PORT_OWNED) != 0 && port->stream) {
        if (fclose(port->stream) != 0) {
            port->stream = NULL;
            port->mode |= NPL_PORT_CLOSED;
            npl_primitive_file_error(who, "close failed", value);
        }
        port->stream = NULL;
    }
    port->mode |= NPL_PORT_CLOSED;
    return npl_nil();
}

NplVal npl_close_input_port(NplVal port)
{
    return close_port_direction(port, NPL_PORT_INPUT, "close-input-port");
}

NplVal npl_close_output_port(NplVal port)
{
    return close_port_direction(port, NPL_PORT_OUTPUT, "close-output-port");
}

NplVal npl_close_port(NplVal value)
{
    NplPort *port = port_value(value, "close-port");
    uint32_t direction = port->mode & (NPL_PORT_INPUT | NPL_PORT_OUTPUT);
    return close_port_direction(value, direction, "close-port");
}

NplVal npl_flush_output_port(NplVal value)
{
    NplPort *port = require_port(value, NPL_PORT_OUTPUT, "flush-output-port");
    if ((port->mode & NPL_PORT_MEMORY) == 0 && fflush(port->stream) != 0)
        npl_primitive_file_error("flush-output-port", "flush failed", value);
    return npl_nil();
}

static const uint8_t *memory_input_data(NplPort *port, const char *who)
{
    return require_bytevector(port->buffer, who)->data;
}

static uint32_t memory_output_capacity(NplPort *port, const char *who)
{
    return require_bytevector(port->buffer, who)->capacity;
}

static uint8_t *memory_output_data(NplPort *port, const char *who)
{
    return require_bytevector(port->buffer, who)->data;
}

static size_t port_read_bytes(NplPort *port, uint8_t *output, size_t count)
{
    if ((port->mode & NPL_PORT_MEMORY) == 0) {
        size_t taken = 0;
        while (port->pushed && taken < count) {
            output[taken++] = port->pushback[0];
            memmove(port->pushback, port->pushback + 1, --port->pushed);
        }
        if (taken == count) return taken;
        return taken + fread(output + taken, 1, count - taken, port->stream);
    }
    const uint8_t *data = memory_input_data(port, "read");
    uint32_t available = port->position < port->length
        ? port->length - port->position : 0;
    if (count > available) count = available;
    if (count) {
        memcpy(output, data + port->position, count);
        port->position += (uint32_t)count;
    }
    return count;
}

static void ensure_memory_output(NplPort *port, uint32_t needed)
{
    uint32_t current_capacity = memory_output_capacity(port, "write");
    if (needed <= current_capacity) return;
    uint32_t capacity = current_capacity ? current_capacity : 64;
    while (capacity < needed) {
        if (capacity > UINT32_MAX / 2) npl_runtime_oom_internal();
        capacity *= 2;
    }
    NplVal grown_value = make_byte_buffer(capacity);
    NplVal previous = port->buffer;
    port->buffer = grown_value;
    if (port->length)
        memcpy(memory_output_data(port, "write"),
               ((NplBytevector *)npl_header(previous))->data, port->length);
}

static void port_write_bytes(NplPort *port, const uint8_t *bytes, size_t count,
                             const char *who)
{
    if ((port->mode & NPL_PORT_MEMORY) == 0) {
        if (count && fwrite(bytes, 1, count, port->stream) != count) {
            fprintf(stderr, "%s: write failed\n", who);
            exit(1);
        }
        return;
    }
    if (count > UINT32_MAX - port->position) npl_runtime_oom_internal();
    uint32_t end = port->position + (uint32_t)count;
    ensure_memory_output(port, end);
    uint8_t *buffer = memory_output_data(port, who);
    if (count) memcpy(buffer + port->position, bytes, count);
    port->position = end;
    if (port->length < end) port->length = end;
}

static int port_read_byte_raw(NplPort *port)
{
    uint8_t byte;
    return port_read_bytes(port, &byte, 1) == 1 ? byte : EOF;
}

/* Return bytes just read to the front of the input. */
static void port_unread_bytes(NplPort *port, const uint8_t *bytes,
                              size_t count)
{
    if ((port->mode & NPL_PORT_MEMORY) != 0) {
        port->position -= (uint32_t)count;
        return;
    }
    if (port->pushed + count > sizeof(port->pushback)) {
        fprintf(stderr, "read: input pushback overflow\n");
        exit(1);
    }
    memmove(port->pushback + count, port->pushback, port->pushed);
    memcpy(port->pushback, bytes, count);
    port->pushed += (uint32_t)count;
}

static int port_peek_byte_raw(NplPort *port, const char *who)
{
    (void)who;
    int byte = port_read_byte_raw(port);
    if (byte == EOF) return EOF;
    uint8_t raw = (uint8_t)byte;
    port_unread_bytes(port, &raw, 1);
    return byte;
}

NplVal npl_eof_object_p(NplVal value);

/* ---- textual input: UTF-8 characters ----
 * read_utf8_char consumes one character; peek_utf8_char reads it and
 * returns its bytes to the port (memory position or stream pushback), so
 * byte and character operations on the same port stay consistent. */
static int read_utf8_sequence(NplPort *port, uint8_t *bytes, size_t *got)
{
    int lead = port_read_byte_raw(port);
    *got = 0;
    if (lead == EOF) return EOF;
    bytes[0] = (uint8_t)lead;
    *got = 1;
    int count = npl_utf8_sequence_length_internal((uint8_t)lead);
    while ((int)*got < count) {
        int next = port_read_byte_raw(port);
        if (next == EOF) break;
        if ((next & 0xC0) != 0x80) {
            /* Not a continuation: leave it for the next character. */
            uint8_t raw = (uint8_t)next;
            port_unread_bytes(port, &raw, 1);
            break;
        }
        bytes[(*got)++] = (uint8_t)next;
    }
    size_t index = 0;
    uint32_t code = npl_utf8_decode_internal(bytes, *got, &index);
    /* A truncated or invalid sequence reads as U+FFFD (Guile, Racket) and
     * consumes only its first byte; the rest is read again. */
    if (index != *got) {
        port_unread_bytes(port, bytes + index, *got - index);
        *got = index;
    }
    return (int)code;
}

static int read_utf8_char(NplPort *port)
{
    uint8_t bytes[4];
    size_t got;
    return read_utf8_sequence(port, bytes, &got);
}

static int peek_utf8_char(NplPort *port)
{
    uint8_t bytes[4];
    size_t got;
    int code = read_utf8_sequence(port, bytes, &got);
    if (got) port_unread_bytes(port, bytes, got);
    return code;
}

NplVal npl_read_char(NplVal value)
{
    NplPort *port = require_port_mode(value, NPL_PORT_INPUT,
                                     NPL_PORT_TEXTUAL, "read-char");
    int code = read_utf8_char(port);
    return code == EOF ? g_eof_object : npl_char((uint32_t)code);
}

NplVal npl_peek_char(NplVal value)
{
    NplPort *port = require_port_mode(value, NPL_PORT_INPUT,
                                     NPL_PORT_TEXTUAL, "peek-char");
    int code = peek_utf8_char(port);
    return code == EOF ? g_eof_object : npl_char((uint32_t)code);
}

/* Whether reading one more byte from a stream port will not block: pushback
 * or the stdio buffer already holds data, or poll reports input (or end of
 * file). C has no portable query for buffered stdio input; the libc fields
 * below are read on macOS and glibc, and other hosts fall back to poll,
 * which can report "not ready" while stdio still holds bytes. */
static int stream_byte_ready(NplPort *port)
{
    if (port->pushed) return 1;
#if defined(__APPLE__)
    if (port->stream->_r > 0) return 1;
#elif defined(__GLIBC__)
    if (port->stream->_IO_read_ptr < port->stream->_IO_read_end) return 1;
#endif
    struct pollfd request;
    request.fd = fileno(port->stream);
    request.events = POLLIN;
    request.revents = 0;
    return poll(&request, 1, 0) > 0;
}

/* u8-ready?: a byte (or EOF) can be read without blocking. Memory ports are
 * always ready. */
NplVal npl_byte_ready_p(NplVal value)
{
    NplPort *port = require_port(value, NPL_PORT_INPUT, "u8-ready?");
    if ((port->mode & NPL_PORT_MEMORY) != 0) return npl_true();
    return stream_byte_ready(port) ? npl_true() : npl_false();
}

/* char-ready?: a whole character (or EOF) can be read without blocking
 * (docs/design-l6.md, Decision 3). Bytes read while checking a multibyte
 * sequence go to the port's pushback, so nothing is lost. */
NplVal npl_port_ready_p(NplVal value)
{
    NplPort *port = require_port(value, NPL_PORT_INPUT, "char-ready?");
    if ((port->mode & NPL_PORT_MEMORY) != 0) return npl_true();
    if (!stream_byte_ready(port)) return npl_false();
    uint8_t bytes[4];
    size_t got = 0;
    int ready = 1;
    int lead = port_read_byte_raw(port);
    if (lead != EOF) {
        bytes[got++] = (uint8_t)lead;
        int count = npl_utf8_sequence_length_internal((uint8_t)lead);
        while ((int)got < count) {
            if (!stream_byte_ready(port)) { ready = 0; break; }
            int next = port_read_byte_raw(port);
            if (next == EOF) break;
            bytes[got++] = (uint8_t)next;
            if ((next & 0xC0) != 0x80) break;
        }
    }
    if (got) port_unread_bytes(port, bytes, got);
    return ready ? npl_true() : npl_false();
}

NplVal npl_write_char(NplVal character, NplVal value)
{
    NplPort *port = require_port_mode(value, NPL_PORT_OUTPUT,
                                     NPL_PORT_TEXTUAL, "write-char");
    if (!npl_is_char(character))
        npl_primitive_error("write-char", "expected a character", character);
    npl_port_write_char_internal(port, npl_char_value(character));
    return npl_nil();
}

NplVal npl_read_byte(NplVal value)
{
    NplPort *port = require_port_mode(value, NPL_PORT_INPUT,
                                     NPL_PORT_BINARY, "read-u8");
    int byte = port_read_byte_raw(port);
    return byte == EOF ? g_eof_object : npl_fixnum(byte);
}

NplVal npl_peek_byte(NplVal value)
{
    NplPort *port = require_port_mode(value, NPL_PORT_INPUT,
                                     NPL_PORT_BINARY, "peek-u8");
    int byte = port_peek_byte_raw(port, "peek-u8");
    return byte == EOF ? g_eof_object : npl_fixnum(byte);
}

NplVal npl_write_byte(NplVal byte, NplVal value)
{
    NplPort *port = require_port_mode(value, NPL_PORT_OUTPUT,
                                     NPL_PORT_BINARY, "write-u8");
    if (!npl_is_fixnum(byte) || npl_fixnum_value(byte) < 0 ||
        npl_fixnum_value(byte) > UINT8_MAX)
        npl_primitive_error("write-u8", "expected a byte", byte);
    uint8_t raw = (uint8_t)npl_fixnum_value(byte);
    port_write_bytes(port, &raw, 1, "write-u8");
    return npl_nil();
}

static uint32_t require_nonnegative_count(NplVal value, const char *who)
{
    if (!npl_is_fixnum(value) || npl_fixnum_value(value) < 0 ||
        (uint64_t)npl_fixnum_value(value) > UINT32_MAX)
        npl_primitive_error(who, "expected a non-negative exact integer",
                            value);
    return (uint32_t)npl_fixnum_value(value);
}

static void require_bytevector_range(NplBytevector *bytes, NplVal start_value,
                                     NplVal end_value, const char *who,
                                     uint32_t *start, uint32_t *end)
{
    *start = require_nonnegative_count(start_value, who);
    *end = require_nonnegative_count(end_value, who);
    if (*start > *end || *end > bytes->length)
        npl_primitive_error(who, "index out of range",
                            npl_cons(start_value,
                                     npl_cons(end_value, npl_nil())));
}

NplVal npl_read_bytevector(int64_t argc, NplVal *argv)
{
    if (argc != 2) {
        fprintf(stderr, "read-bytevector: internal arity mismatch\n");
        exit(1);
    }
    uint32_t count = require_nonnegative_count(argv[0], "read-bytevector");
    NplPort *port = require_port_mode(argv[1], NPL_PORT_INPUT,
                                     NPL_PORT_BINARY, "read-bytevector");
    NplVal value = make_byte_buffer(count);
    NplBytevector *bytes = (NplBytevector *)npl_header(value);
    size_t got = port_read_bytes(port, bytes->data, count);
    if (got == 0 && count != 0) return g_eof_object;
    bytes->length = (uint32_t)got;
    bytes->capacity = (uint32_t)got;
    return value;
}

NplVal npl_read_bytevector_into(int64_t argc, NplVal *argv)
{
    if (argc != 4) {
        fprintf(stderr, "read-bytevector!: internal arity mismatch\n");
        exit(1);
    }
    NplBytevector *bytes = require_bytevector(argv[0], "read-bytevector!");
    if ((bytes->header.flags & NPL_FLAG_LITERAL) != 0)
        npl_primitive_error("read-bytevector!", "cannot mutate a literal",
                            argv[0]);
    NplPort *port = require_port_mode(argv[1], NPL_PORT_INPUT,
                                     NPL_PORT_BINARY, "read-bytevector!");
    uint32_t start;
    uint32_t end;
    require_bytevector_range(bytes, argv[2], argv[3], "read-bytevector!",
                             &start, &end);
    size_t got = port_read_bytes(port, bytes->data + start, end - start);
    if (got == 0 && end != start) return g_eof_object;
    return npl_fixnum((int64_t)got);
}

NplVal npl_write_bytevector(int64_t argc, NplVal *argv)
{
    if (argc != 4) {
        fprintf(stderr, "write-bytevector: internal arity mismatch\n");
        exit(1);
    }
    NplBytevector *bytes = require_bytevector(argv[0], "write-bytevector");
    NplPort *port = require_port_mode(argv[1], NPL_PORT_OUTPUT,
                                     NPL_PORT_BINARY, "write-bytevector");
    uint32_t start;
    uint32_t end;
    require_bytevector_range(bytes, argv[2], argv[3], "write-bytevector",
                             &start, &end);
    port_write_bytes(port, bytes->data + start, end - start,
                     "write-bytevector");
    return npl_nil();
}

NplVal npl_eof_object(void) { return g_eof_object; }
NplVal npl_eof_object_p(NplVal value)
{
    return value == g_eof_object ? npl_true() : npl_false();
}
NplVal npl_port_p(NplVal value)
{
    return (npl_is_ptr(value) && npl_header(value)->tag == BOX_PORT)
        ? npl_true() : npl_false();
}
NplVal npl_input_port_p(NplVal value)
{
    return (npl_is_ptr(value) && npl_header(value)->tag == BOX_PORT &&
            (((NplPort *)npl_header(value))->mode & NPL_PORT_INPUT) != 0)
        ? npl_true() : npl_false();
}
NplVal npl_output_port_p(NplVal value)
{
    return (npl_is_ptr(value) && npl_header(value)->tag == BOX_PORT &&
            (((NplPort *)npl_header(value))->mode & NPL_PORT_OUTPUT) != 0)
        ? npl_true() : npl_false();
}
NplVal npl_input_port_open_p(NplVal value)
{
    return (npl_is_ptr(value) && npl_header(value)->tag == BOX_PORT &&
            (((NplPort *)npl_header(value))->mode &
             (NPL_PORT_INPUT | NPL_PORT_CLOSED)) == NPL_PORT_INPUT)
        ? npl_true() : npl_false();
}
NplVal npl_output_port_open_p(NplVal value)
{
    return (npl_is_ptr(value) && npl_header(value)->tag == BOX_PORT &&
            (((NplPort *)npl_header(value))->mode &
             (NPL_PORT_OUTPUT | NPL_PORT_CLOSED)) == NPL_PORT_OUTPUT)
        ? npl_true() : npl_false();
}
NplVal npl_textual_port_p(NplVal value)
{
    return (npl_is_ptr(value) && npl_header(value)->tag == BOX_PORT &&
            (((NplPort *)npl_header(value))->mode & NPL_PORT_TEXTUAL) != 0)
        ? npl_true() : npl_false();
}
NplVal npl_binary_port_p(NplVal value)
{
    return (npl_is_ptr(value) && npl_header(value)->tag == BOX_PORT &&
            (((NplPort *)npl_header(value))->mode & NPL_PORT_BINARY) != 0)
        ? npl_true() : npl_false();
}

/* ---- private interface for write.c, read.c, io.c (port.h) ---- */

NplPort *npl_port_value_internal(NplVal value, const char *who)
{
    return port_value(value, who);
}

NplPort *npl_port_require_mode_internal(NplVal value, uint32_t access,
                                        uint32_t kind, const char *who)
{
    return require_port_mode(value, access, kind, who);
}

void npl_port_write_bytes_internal(NplPort *port, const uint8_t *bytes,
                                   size_t count, const char *who)
{
    port_write_bytes(port, bytes, count, who);
}

void npl_port_unread_bytes_internal(NplPort *port, const uint8_t *bytes,
                                    size_t count)
{
    port_unread_bytes(port, bytes, count);
}

int npl_port_read_char_internal(NplPort *port)
{
    return read_utf8_char(port);
}

void npl_port_write_char_internal(NplPort *port, uint32_t code)
{
    uint8_t bytes[4];
    size_t count = npl_utf8_encode_internal(code, bytes);
    port_write_bytes(port, bytes, count, "write");
}

NplVal npl_port_eof_internal(void)
{
    return g_eof_object;
}
