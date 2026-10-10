#define _DARWIN_C_SOURCE

/* Runtime ports, external I/O, and process entry services. */

#include "npl.h"
#include <fcntl.h>
#include <setjmp.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

extern char **environ;

#define NPL_PORT_INPUT   1u
#define NPL_PORT_OUTPUT  2u
#define NPL_PORT_OWNED   4u
#define NPL_PORT_CLOSED  8u
#define NPL_PORT_TEXTUAL 16u
#define NPL_PORT_BINARY  32u
#define NPL_PORT_MEMORY  64u

static int g_argc_stored = 0;
static char **g_argv_stored = NULL;
static NplVal g_args_list = 2;
static NplVal g_command_line = 2;
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
    port->reserved = 0;
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

static NplVal make_string_buffer(uint32_t capacity)
{
    NplString *buffer = (NplString *)npl_alloc_object(
        sizeof(NplString) + (size_t)capacity + 1);
    buffer->header.tag = BOX_STRING;
    buffer->header.flags = 0;
    buffer->length = 0;
    buffer->capacity = capacity;
    buffer->data[0] = '\0';
    return (NplVal)(uintptr_t)buffer | TAG_PTR;
}

static NplString *require_string(NplVal value, const char *who)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_STRING)
        npl_primitive_error(who, "expected a string", value);
    return (NplString *)npl_header(value);
}

static NplVal copy_string_range(const char *bytes, uint32_t length)
{
    return npl_string(bytes, length);
}

static NplVal make_eof_object(void)
{
    NplEof *eof = (NplEof *)npl_alloc_object(sizeof(NplEof));
    eof->header.tag = BOX_EOF;
    return (NplVal)(uintptr_t)eof | TAG_PTR;
}

void npl_io_init(void)
{
    g_args_list = npl_nil();
    g_command_line = npl_nil();
    /* The current ports are parameter objects (R7RS 6.13.1); the globals
     * hold the parameters, and parameterize rebinds their values. */
    npl_gc_root(&g_args_list);
    npl_gc_root(&g_command_line);
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

/* ---- Datum output ---- */

static void port_write_bytes(NplPort *port, const uint8_t *bytes, size_t count,
                             const char *who);
static NplPort *port_value(NplVal value, const char *who);

static void output_bytes(NplPort *port, const char *bytes, size_t count)
{
    port_write_bytes(port, (const uint8_t *)bytes, count, "write");
}

static void output_text(NplPort *port, const char *text)
{
    output_bytes(port, text, strlen(text));
}

static void output_char(NplPort *port, int code)
{
    uint8_t byte = (uint8_t)code;
    port_write_bytes(port, &byte, 1, "write");
}

static void output_integer(NplPort *port, long long value)
{
    char buffer[32];
    int length = snprintf(buffer, sizeof(buffer), "%lld", value);
    output_bytes(port, buffer, (size_t)length);
}

static void output_integer_value(NplPort *port, NplVal value)
{
    NplVal text = npl_number_to_string(value);
    npl_gc_root(&text);
    NplString *string = (NplString *)npl_header(text);
    output_bytes(port, string->data, string->length);
    npl_gc_unroot(&text);
}

static void display_to(NplPort *port, NplVal value)
{
    if (npl_is_fixnum(value)) {
        output_integer(port, (long long)npl_fixnum_value(value));
    } else if (npl_is_nil(value)) {
        output_text(port, "()");
    } else if (value == npl_true()) {
        output_text(port, "#t");
    } else if (value == npl_false()) {
        output_text(port, "#f");
    } else if (npl_is_char(value)) {
        output_char(port, (int)npl_char_value(value));
    } else if (npl_is_ptr(value)) {
        NplHeader *header = npl_header(value);
        switch (header->tag) {
        case BOX_PAIR: {
            NplPair *pair = (NplPair *)header;
            output_char(port, '(');
            display_to(port, pair->car);
            NplVal rest = pair->cdr;
            while (!npl_is_nil(rest) && npl_is_ptr(rest) &&
                   npl_box_tag(rest) == BOX_PAIR) {
                output_char(port, ' ');
                display_to(port, npl_car(rest));
                rest = npl_cdr(rest);
            }
            if (!npl_is_nil(rest)) {
                output_text(port, " . ");
                display_to(port, rest);
            }
            output_char(port, ')');
            break;
        }
        case BOX_STRING:
        case BOX_SYMBOL: {
            NplString *string = (NplString *)header;
            output_bytes(port, string->data, string->length);
            break;
        }
        case BOX_FLOAT:
        case BOX_BIGNUM:
        case BOX_RATIONAL:
        case BOX_COMPLEX:
            output_integer_value(port, value);
            break;
        case BOX_RECORD: {
            /* The type descriptor is the record type's name (a symbol). */
            NplVal type = ((NplVector *)header)->data[0];
            output_text(port, "#<");
            display_to(port, type);
            output_char(port, '>');
            break;
        }
        case BOX_VECTOR: {
            NplVector *vector = (NplVector *)header;
            output_text(port, "#(");
            for (uint32_t i = 0; i < vector->length; i++) {
                if (i) output_char(port, ' ');
                display_to(port, vector->data[i]);
            }
            output_char(port, ')');
            break;
        }
        case BOX_BYTEVECTOR: {
            NplBytevector *vector = (NplBytevector *)header;
            output_text(port, "#u8(");
            for (uint32_t i = 0; i < vector->length; i++) {
                if (i) output_char(port, ' ');
                output_integer(port, vector->data[i]);
            }
            output_char(port, ')');
            break;
        }
        case BOX_VALUES: output_text(port, "#<multiple-values>"); break;
        case BOX_PROMISE: output_text(port, "#<promise>"); break;
        case BOX_PORT: output_text(port, "#<port>"); break;
        case BOX_EOF: output_text(port, "#<eof>"); break;
        default: output_text(port, "#<unknown>"); break;
        }
    } else {
        output_text(port, "#<unknown>");
    }
}

static void write_escaped_string(NplPort *port, const NplString *string)
{
    output_char(port, '"');
    for (uint32_t i = 0; i < string->length; i++) {
        unsigned char code = (unsigned char)string->data[i];
        if (code == '"' || code == '\\') {
            output_char(port, '\\');
            output_char(port, code);
        } else if (code == '\n') {
            output_text(port, "\\n");
        } else if (code == '\r') {
            output_text(port, "\\r");
        } else if (code == '\t') {
            output_text(port, "\\t");
        } else {
            output_char(port, code);
        }
    }
    output_char(port, '"');
}

static void write_to(NplPort *port, NplVal value)
{
    if (npl_is_char(value)) {
        uint32_t code = npl_char_value(value);
        if (code == 32) output_text(port, "#\\space");
        else if (code == 10) output_text(port, "#\\newline");
        else if (code == 9) output_text(port, "#\\tab");
        else {
            output_text(port, "#\\");
            output_char(port, (int)code);
        }
        return;
    }
    if (npl_is_ptr(value) && npl_header(value)->tag == BOX_STRING) {
        write_escaped_string(port, (NplString *)npl_header(value));
        return;
    }
    if (npl_is_ptr(value) && npl_header(value)->tag == BOX_PAIR) {
        NplPair *pair = (NplPair *)npl_header(value);
        output_char(port, '(');
        write_to(port, pair->car);
        NplVal rest = pair->cdr;
        while (!npl_is_nil(rest) && npl_is_ptr(rest) &&
               npl_box_tag(rest) == BOX_PAIR) {
            output_char(port, ' ');
            write_to(port, npl_car(rest));
            rest = npl_cdr(rest);
        }
        if (!npl_is_nil(rest)) {
            output_text(port, " . ");
            write_to(port, rest);
        }
        output_char(port, ')');
        return;
    }
    if (npl_is_ptr(value) && npl_header(value)->tag == BOX_VECTOR) {
        NplVector *vector = (NplVector *)npl_header(value);
        output_text(port, "#(");
        for (uint32_t i = 0; i < vector->length; i++) {
            if (i) output_char(port, ' ');
            write_to(port, vector->data[i]);
        }
        output_char(port, ')');
        return;
    }
    display_to(port, value);
}

NplVal npl_display(NplVal value)
{
    npl_runtime_init();
    npl_gc_root(&value);
    display_to(port_value(npl_current_output_port(), "display"), value);
    npl_gc_unroot(&value);
    return npl_nil();
}

NplVal npl_newline(void)
{
    npl_runtime_init();
    output_char(port_value(npl_current_output_port(), "newline"), '\n');
    return npl_nil();
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

NplVal npl_display_port(NplVal value, NplVal port_value_arg)
{
    NplPort *port = require_port_mode(port_value_arg, NPL_PORT_OUTPUT,
                                     NPL_PORT_TEXTUAL, "display");
    npl_gc_root(&value);
    npl_gc_root(&port_value_arg);
    display_to(port, value);
    npl_gc_unroot(&port_value_arg);
    npl_gc_unroot(&value);
    return npl_nil();
}

NplVal npl_write_port(NplVal value, NplVal port_value_arg)
{
    NplPort *port = require_port_mode(port_value_arg, NPL_PORT_OUTPUT,
                                     NPL_PORT_TEXTUAL, "write");
    npl_gc_root(&value);
    npl_gc_root(&port_value_arg);
    write_to(port, value);
    npl_gc_unroot(&port_value_arg);
    npl_gc_unroot(&value);
    return npl_nil();
}

NplVal npl_newline_port(NplVal port_value_arg)
{
    NplPort *port = require_port_mode(port_value_arg, NPL_PORT_OUTPUT,
                                     NPL_PORT_TEXTUAL, "newline");
    output_char(port, '\n');
    return npl_nil();
}

/* Open a file port, or return #f when the OS refuses (missing file,
 * permission). (scheme file) turns #f into a file-error object. */
static NplVal try_open_file_port(NplVal path, const char *mode,
                                 uint32_t port_mode, const char *who)
{
    const NplString *name = npl_require_bytes_internal(path, who);
    char *buffer = (char *)malloc((size_t)name->length + 1);
    if (!buffer) npl_runtime_oom_internal();
    memcpy(buffer, name->data, name->length);
    buffer[name->length] = '\0';
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
    return make_memory_port(value, NPL_PORT_INPUT | NPL_PORT_TEXTUAL,
                            string->length);
}

NplVal npl_open_output_string(void)
{
    return make_memory_port(make_string_buffer(64),
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
    NplString *buffer = require_string(port->buffer, "get-output-string");
    return copy_string_range(buffer->data, port->length);
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
    if ((port->mode & NPL_PORT_BINARY) != 0)
        return require_bytevector(port->buffer, who)->data;
    return (const uint8_t *)require_string(port->buffer, who)->data;
}

static uint32_t memory_output_capacity(NplPort *port, const char *who)
{
    if ((port->mode & NPL_PORT_BINARY) != 0)
        return require_bytevector(port->buffer, who)->capacity;
    return require_string(port->buffer, who)->capacity;
}

static uint8_t *memory_output_data(NplPort *port, const char *who)
{
    if ((port->mode & NPL_PORT_BINARY) != 0)
        return require_bytevector(port->buffer, who)->data;
    return (uint8_t *)require_string(port->buffer, who)->data;
}

static size_t port_read_bytes(NplPort *port, uint8_t *output, size_t count)
{
    if ((port->mode & NPL_PORT_MEMORY) == 0)
        return fread(output, 1, count, port->stream);
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
    NplVal grown_value = (port->mode & NPL_PORT_BINARY) != 0
        ? make_byte_buffer(capacity) : make_string_buffer(capacity);
    NplVal previous = port->buffer;
    port->buffer = grown_value;
    if (port->length)
        memcpy(memory_output_data(port, "write"),
               (npl_header(previous)->tag == BOX_BYTEVECTOR)
                   ? ((NplBytevector *)npl_header(previous))->data
                   : (uint8_t *)((NplString *)npl_header(previous))->data,
               port->length);
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
    if ((port->mode & NPL_PORT_TEXTUAL) != 0) {
        NplString *string = require_string(port->buffer, who);
        string->length = port->length;
        string->data[port->length] = '\0';
    }
}

static int port_read_byte_raw(NplPort *port)
{
    uint8_t byte;
    return port_read_bytes(port, &byte, 1) == 1 ? byte : EOF;
}

static int port_peek_byte_raw(NplPort *port, const char *who)
{
    int byte = port_read_byte_raw(port);
    if (byte == EOF) return EOF;
    if ((port->mode & NPL_PORT_MEMORY) != 0) {
        port->position--;
    } else if (ungetc(byte, port->stream) == EOF) {
        fprintf(stderr, "%s: cannot restore input\n", who);
        exit(1);
    }
    return byte;
}

NplVal npl_eof_object_p(NplVal value);

NplVal npl_read_char(NplVal value)
{
    NplPort *port = require_port_mode(value, NPL_PORT_INPUT,
                                     NPL_PORT_TEXTUAL, "read-char");
    int byte = port_read_byte_raw(port);
    return byte == EOF ? g_eof_object : npl_char((uint32_t)byte);
}

NplVal npl_peek_char(NplVal value)
{
    NplPort *port = require_port_mode(value, NPL_PORT_INPUT,
                                     NPL_PORT_TEXTUAL, "peek-char");
    int byte = port_peek_byte_raw(port, "peek-char");
    return byte == EOF ? g_eof_object : npl_char((uint32_t)byte);
}

/* char-ready? / u8-ready?: memory ports always have data or EOF ready; a
 * stream port is ready when buffered input exists or poll reports it. */
NplVal npl_port_ready_p(NplVal value)
{
    NplPort *port = require_port(value, NPL_PORT_INPUT, "char-ready?");
    if ((port->mode & NPL_PORT_MEMORY) != 0) return npl_true();
    struct pollfd request;
    request.fd = fileno(port->stream);
    request.events = POLLIN;
    request.revents = 0;
    return poll(&request, 1, 0) > 0 ? npl_true() : npl_false();
}

NplVal npl_write_char(NplVal character, NplVal value)
{
    NplPort *port = require_port_mode(value, NPL_PORT_OUTPUT,
                                     NPL_PORT_TEXTUAL, "write-char");
    if (!npl_is_char(character) || npl_char_value(character) > UINT8_MAX)
        npl_primitive_error("write-char", "expected a character", character);
    uint8_t byte = (uint8_t)npl_char_value(character);
    port_write_bytes(port, &byte, 1, "write-char");
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

/* ---- single-datum textual reader ---- */

typedef struct {
    NplPort *port;
    int lookahead;
    int has_lookahead;
} NplDatumReader;

static jmp_buf *g_read_recovery = NULL;
static char g_read_error_message[128];

static void read_error(const char *message)
{
    if (g_read_recovery) {
        (void)snprintf(g_read_error_message, sizeof(g_read_error_message),
                       "%s", message);
        longjmp(*g_read_recovery, 1);
    }
    fprintf(stderr, "read: %s\n", message);
    exit(1);
}

static void read_buffer_error(char *buffer, const char *message)
{
    free(buffer);
    read_error(message);
}

static int reader_get(NplDatumReader *reader)
{
    if (reader->has_lookahead) {
        reader->has_lookahead = 0;
        return reader->lookahead;
    }
    return port_read_byte_raw(reader->port);
}

static int reader_peek(NplDatumReader *reader)
{
    if (!reader->has_lookahead) {
        reader->lookahead = port_read_byte_raw(reader->port);
        reader->has_lookahead = 1;
    }
    return reader->lookahead;
}

static int datum_delimiter(int code)
{
    return code == EOF || code == '(' || code == ')' || code == '"' ||
           code == ';' || code == '\'' || code == '`' || code == ',' ||
           code == ' ' || code == '\t' || code == '\n' || code == '\r';
}

static void reader_skip_space(NplDatumReader *reader)
{
    for (;;) {
        int code = reader_peek(reader);
        if (code == ' ' || code == '\t' || code == '\n' || code == '\r') {
            (void)reader_get(reader);
        } else if (code == ';') {
            while ((code = reader_get(reader)) != EOF && code != '\n') {}
        } else {
            return;
        }
    }
}

static void buffer_push(char **buffer, size_t *length, size_t *capacity,
                        int code)
{
    if (*length + 1 >= *capacity) {
        size_t next = *capacity ? *capacity * 2 : 32;
        char *grown = (char *)realloc(*buffer, next);
        if (!grown) npl_runtime_oom_internal();
        *buffer = grown;
        *capacity = next;
    }
    (*buffer)[(*length)++] = (char)code;
}

static NplVal read_datum_inner(NplDatumReader *reader, int allow_eof);

static NplVal read_string_datum(NplDatumReader *reader)
{
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    for (;;) {
        int code = reader_get(reader);
        if (code == EOF)
            read_buffer_error(buffer, "unexpected EOF in string");
        if (code == '"') break;
        if (code == '\\') {
            code = reader_get(reader);
            if (code == EOF)
                read_buffer_error(buffer, "unexpected EOF in string escape");
            if (code == 'n') code = '\n';
            else if (code == 'r') code = '\r';
            else if (code == 't') code = '\t';
            else if (code != '"' && code != '\\')
                read_buffer_error(buffer, "unknown string escape");
        }
        buffer_push(&buffer, &length, &capacity, code);
    }
    NplVal value = npl_string(buffer, (uint32_t)length);
    free(buffer);
    return value;
}

static NplVal read_token_datum(NplDatumReader *reader, int first)
{
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    buffer_push(&buffer, &length, &capacity, first);
    while (!datum_delimiter(reader_peek(reader)))
        buffer_push(&buffer, &length, &capacity, reader_get(reader));
    buffer_push(&buffer, &length, &capacity, 0);

    NplVal token = npl_string(buffer, (uint32_t)(length - 1));
    NplVal value = npl_number_from_string(token);
    if (value == npl_false()) {
        int numeric_start = buffer[0] >= '0' && buffer[0] <= '9';
        if ((buffer[0] == '+' || buffer[0] == '-') && length > 2)
            numeric_start = buffer[1] >= '0' && buffer[1] <= '9';
        if (numeric_start) read_buffer_error(buffer, "invalid exact number");
        value = npl_symbol(buffer, (uint32_t)(length - 1));
    }
    free(buffer);
    return value;
}

static int symbol_named(NplVal value, const char *name)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_SYMBOL) return 0;
    NplString *symbol = (NplString *)npl_header(value);
    size_t length = strlen(name);
    return symbol->length == length &&
           memcmp(symbol->data, name, length) == 0;
}

static NplVal reverse_onto(NplVal values, NplVal tail)
{
    while (!npl_is_nil(values)) {
        tail = npl_cons(npl_car(values), tail);
        values = npl_cdr(values);
    }
    return tail;
}

static NplVal read_list_datum(NplDatumReader *reader)
{
    NplVal reversed = npl_nil();
    for (;;) {
        reader_skip_space(reader);
        int code = reader_peek(reader);
        if (code == EOF) read_error("unexpected EOF in list");
        if (code == ')') {
            (void)reader_get(reader);
            return reverse_onto(reversed, npl_nil());
        }
        NplVal value = read_datum_inner(reader, 0);
        if (symbol_named(value, ".")) {
            if (npl_is_nil(reversed)) read_error("misplaced dot");
            NplVal tail = read_datum_inner(reader, 0);
            reader_skip_space(reader);
            if (reader_get(reader) != ')')
                read_error("expected ) after dotted datum");
            return reverse_onto(reversed, tail);
        }
        reversed = npl_cons(value, reversed);
    }
}

static NplVal quoted_datum(NplVal value)
{
    return npl_cons(npl_symbol("quote", 5), npl_cons(value, npl_nil()));
}

static NplVal read_character_datum(NplDatumReader *reader)
{
    char buffer[16];
    size_t length = 0;
    while (!datum_delimiter(reader_peek(reader))) {
        if (length + 1 >= sizeof(buffer)) read_error("character name too long");
        buffer[length++] = (char)reader_get(reader);
    }
    if (length == 0) read_error("empty character literal");
    if (length == 1) return npl_char((unsigned char)buffer[0]);
    buffer[length] = '\0';
    if (strcmp(buffer, "space") == 0) return npl_char(' ');
    if (strcmp(buffer, "newline") == 0) return npl_char('\n');
    if (strcmp(buffer, "tab") == 0) return npl_char('\t');
    if (strcmp(buffer, "return") == 0) return npl_char('\r');
    read_error("unknown character name");
    return npl_nil();
}

static NplVal bytevector_from_list(NplVal list)
{
    size_t count = 0;
    for (NplVal rest = list; !npl_is_nil(rest); rest = npl_cdr(rest)) count++;
    NplVal *items = count ? (NplVal *)malloc(count * sizeof(NplVal)) : NULL;
    if (count && !items) npl_runtime_oom_internal();
    size_t index = 0;
    for (NplVal rest = list; !npl_is_nil(rest); rest = npl_cdr(rest))
        items[index++] = npl_car(rest);
    NplVal value = npl_make_bytevector((int64_t)count, items);
    free(items);
    return value;
}

static NplVal read_hash_radix_datum(NplDatumReader *reader, int prefix)
{
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    buffer_push(&buffer, &length, &capacity, '#');
    buffer_push(&buffer, &length, &capacity, prefix);
    while (!datum_delimiter(reader_peek(reader)))
        buffer_push(&buffer, &length, &capacity, reader_get(reader));
    NplVal text = npl_string(buffer, (uint32_t)length);
    NplVal value = npl_number_from_string(text);
    free(buffer);
    if (value == npl_false()) read_error("invalid radix number");
    return value;
}

static NplVal read_hash_datum(NplDatumReader *reader)
{
    int code = reader_get(reader);
    if (code == 't' || code == 'f') {
        if (!datum_delimiter(reader_peek(reader)))
            read_error("invalid boolean literal");
        return code == 't' ? npl_true() : npl_false();
    }
    if (code == 'b' || code == 'B' || code == 'o' || code == 'O' ||
        code == 'd' || code == 'D' || code == 'x' || code == 'X')
        return read_hash_radix_datum(reader, code);
    if (code == '\\') return read_character_datum(reader);
    if (code == '(') return npl_list_to_vector(read_list_datum(reader));
    if (code == 'u') {
        if (reader_get(reader) != '8' || reader_get(reader) != '(')
            read_error("malformed bytevector");
        return bytevector_from_list(read_list_datum(reader));
    }
    read_error("unknown # syntax");
    return npl_nil();
}

static NplVal read_datum_inner(NplDatumReader *reader, int allow_eof)
{
    reader_skip_space(reader);
    int code = reader_get(reader);
    if (code == EOF) {
        if (allow_eof) return g_eof_object;
        read_error("unexpected EOF");
    }
    if (code == '(') return read_list_datum(reader);
    if (code == ')') read_error("unexpected )");
    if (code == '"') return read_string_datum(reader);
    if (code == '\'') return quoted_datum(read_datum_inner(reader, 0));
    if (code == '#') return read_hash_datum(reader);
    return read_token_datum(reader, code);
}

NplVal npl_read_datum(NplVal port_value_arg)
{
    NplPort *port = require_port_mode(port_value_arg, NPL_PORT_INPUT,
                                     NPL_PORT_TEXTUAL, "read");
    NplDatumReader reader = { port, 0, 0 };
    NplVal value = read_datum_inner(&reader, 1);
    if (reader.has_lookahead && reader.lookahead != EOF) {
        if ((port->mode & NPL_PORT_MEMORY) != 0)
            port->position--;
        else
            (void)ungetc(reader.lookahead, port->stream);
    }
    return value;
}

NplVal npl_read_result(NplVal port_value)
{
    jmp_buf recovery;
    jmp_buf *previous = g_read_recovery;
    g_read_recovery = &recovery;
    NplVal items[2];
    if (setjmp(recovery) == 0) {
        items[0] = npl_true();
        items[1] = npl_read_datum(port_value);
    } else {
        items[0] = npl_false();
        items[1] = npl_string(g_read_error_message,
                              (uint32_t)strlen(g_read_error_message));
    }
    g_read_recovery = previous;
    return npl_make_vector(2, items);
}

/* ---- process arguments and whole-file input ---- */

void npl_store_args(int argc, char **argv) {
#ifdef NPL_UNBUFFERED_STDOUT
    setvbuf(stdout, (char *)NULL, _IONBF, 0);
#endif
    g_argc_stored = argc;
    g_argv_stored = argv;
}

static NplVal argument_list(int first)
{
    NplVal acc = npl_nil();
    npl_gc_root(&acc);
    for (int i = g_argc_stored - 1; i >= first; i--) {
        NplVal s = npl_string(g_argv_stored[i],
                              (uint32_t)strlen(g_argv_stored[i]));
        acc = npl_cons(s, acc);
    }
    npl_gc_unroot(&acc);
    return acc;
}

/* Arguments after the program name (argv[1..]); compiler/driver use. */
NplVal npl_program_arguments(void) {
    if (!npl_is_nil(g_args_list)) return g_args_list;
    g_args_list = argument_list(1);
    return g_args_list;
}

/* R7RS command-line: the command name followed by its arguments. A runner
 * such as npli replaces it with the script name and script arguments. */
NplVal npl_command_line(void) {
    if (npl_is_nil(g_command_line)) g_command_line = argument_list(0);
    return g_command_line;
}

NplVal npl_set_command_line(NplVal arguments) {
    g_command_line = arguments;
    return npl_nil();
}

NplVal npl_read_file(NplVal path) {
    const NplString *p = npl_require_bytes_internal(path, "read-file");
    char *buf = (char *)malloc((size_t)p->length + 1);
    if (!buf) { fprintf(stderr, "read-file: oom\n"); exit(1); }
    memcpy(buf, p->data, p->length);
    buf[p->length] = '\0';

    FILE *fp = fopen(buf, "rb");
    free(buf);
    if (!fp) npl_primitive_file_error("read-file", "cannot open file", path);
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size < 0) { fclose(fp); exit(1); }
    char *data = (char *)malloc((size_t)size + 1);
    size_t got = fread(data, 1, (size_t)size, fp);
    fclose(fp);
    data[got] = '\0';
    NplVal out = npl_string(data, (uint32_t)got);
    free(data);
    return out;
}

static const NplString *process_string(NplVal value, const char *who)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_STRING)
        npl_primitive_error(who, "expected a string", value);
    return (const NplString *)npl_header(value);
}

static size_t process_argument_count(NplVal arguments)
{
    size_t count = 0;
    while (!npl_is_nil(arguments)) {
        if (!npl_is_ptr(arguments) || npl_header(arguments)->tag != BOX_PAIR)
            npl_primitive_error("run-process", "expected a proper list",
                                arguments);
        (void)process_string(npl_car(arguments), "run-process");
        arguments = npl_cdr(arguments);
        count++;
    }
    if (count == 0)
        npl_primitive_error("run-process", "expected a program name",
                            npl_nil());
    return count;
}

NplVal npl_process_run(NplVal arguments, NplVal output_path)
{
    size_t count = process_argument_count(arguments);
    char **argv = (char **)malloc((count + 1) * sizeof(char *));
    if (!argv) npl_runtime_oom_internal();
    NplVal rest = arguments;
    for (size_t index = 0; index < count; index++) {
        argv[index] = (char *)process_string(
            npl_car(rest), "run-process")->data;
        rest = npl_cdr(rest);
    }
    argv[count] = NULL;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_t *actions_pointer = NULL;
    int action_status = 0;
    if (output_path != npl_false()) {
        const NplString *path = process_string(output_path, "run-process");
        action_status = posix_spawn_file_actions_init(&actions);
        if (action_status == 0) {
            action_status = posix_spawn_file_actions_addopen(
                &actions, STDOUT_FILENO, path->data,
                O_WRONLY | O_CREAT | O_TRUNC, 0666);
            actions_pointer = &actions;
        }
    }
    if (action_status != 0) {
        free(argv);
        fprintf(stderr, "run-process: cannot configure output\n");
        exit(1);
    }

    pid_t child = 0;
    int spawn_status = posix_spawnp(
        &child, argv[0], actions_pointer, NULL, argv, environ);
    if (actions_pointer) posix_spawn_file_actions_destroy(actions_pointer);
    free(argv);
    if (spawn_status != 0) return npl_fixnum(127);

    int status = 0;
    if (waitpid(child, &status, 0) < 0) {
        fprintf(stderr, "run-process: wait failed\n");
        exit(1);
    }
    if (WIFEXITED(status)) return npl_fixnum(WEXITSTATUS(status));
    if (WIFSIGNALED(status)) return npl_fixnum(128 + WTERMSIG(status));
    return npl_fixnum(1);
}

NplVal npl_get_environment_variable(NplVal name_value)
{
    const NplString *name = process_string(
        name_value, "get-environment-variable");
    const char *value = getenv(name->data);
    return value ? npl_string(value, (uint32_t)strlen(value)) : npl_false();
}

NplVal npl_get_environment_variables(void)
{
    NplVal variables = npl_nil();
    for (size_t index = 0; environ[index]; index++) {
        const char *entry = environ[index];
        const char *separator = strchr(entry, '=');
        if (!separator) continue;
        NplVal name = npl_string(entry, (uint32_t)(separator - entry));
        NplVal value = npl_string(separator + 1,
                                  (uint32_t)strlen(separator + 1));
        variables = npl_cons(npl_cons(name, value), variables);
    }
    return variables;
}

NplVal npl_file_exists(NplVal path_value)
{
    const NplString *path = process_string(path_value, "file-exists?");
    return access(path->data, F_OK) == 0 ? npl_true() : npl_false();
}

NplVal npl_delete_file(NplVal path_value)
{
    const NplString *path = process_string(path_value, "delete-file");
    if (unlink(path->data) != 0)
        npl_primitive_file_error("delete-file", "cannot delete file",
                                 path_value);
    return npl_nil();
}

NplVal npl_make_temporary_directory(void)
{
    const char *root = getenv("TMPDIR");
    if (!root || root[0] == '\0') root = "/tmp";
    size_t length = strlen(root) + sizeof("/npl-driver.XXXXXX");
    char *path = (char *)malloc(length);
    if (!path) npl_runtime_oom_internal();
    (void)snprintf(path, length, "%s/npl-driver.XXXXXX", root);
    if (!mkdtemp(path)) {
        free(path);
        npl_primitive_file_error("make-temporary-directory",
                                 "cannot create directory",
                                 npl_string(root, (uint32_t)strlen(root)));
    }
    NplVal value = npl_string(path, (uint32_t)strlen(path));
    free(path);
    return value;
}

NplVal npl_delete_directory(NplVal path_value)
{
    const NplString *path = process_string(path_value, "delete-directory");
    if (rmdir(path->data) != 0)
        npl_primitive_file_error("delete-directory", "cannot delete directory",
                                 path_value);
    return npl_nil();
}

/* Absolute path of the running executable, or #f when the OS cannot say.
 * Tool and library lookup policy built on it lives in NPL. */
NplVal npl_executable_path(void)
{
    char raw[PATH_MAX];
    char resolved[PATH_MAX];
#ifdef __APPLE__
    uint32_t size = (uint32_t)sizeof(raw);
    if (_NSGetExecutablePath(raw, &size) != 0) return npl_false();
#else
    ssize_t length = readlink("/proc/self/exe", raw, sizeof(raw) - 1);
    if (length < 0) return npl_false();
    raw[length] = '\0';
#endif
    if (!realpath(raw, resolved)) return npl_false();
    return npl_string(resolved, (uint32_t)strlen(resolved));
}

void npl_exit(NplVal code) {
    int64_t v = (int64_t)(code >> 2);          /* untag fixnum */
    fflush(stdout);
    _Exit((int)(v & 255));
}
