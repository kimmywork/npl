/* Runtime ports, external I/O, and process entry services. */

#include "npl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_BYTEVECTOR) {
        fprintf(stderr, "%s: expected a bytevector\n", who);
        exit(1);
    }
    return (NplBytevector *)npl_header(value);
}

static NplVal copy_bytevector_range(const uint8_t *bytes, uint32_t length)
{
    NplVal value = make_byte_buffer(length);
    NplBytevector *buffer = (NplBytevector *)npl_header(value);
    if (length) memcpy(buffer->data, bytes, length);
    return value;
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
    g_current_input_port = make_port(stdin,
        NPL_PORT_INPUT | NPL_PORT_TEXTUAL | NPL_PORT_BINARY);
    g_current_output_port = make_port(stdout,
        NPL_PORT_OUTPUT | NPL_PORT_TEXTUAL | NPL_PORT_BINARY);
    g_current_error_port = make_port(stderr,
        NPL_PORT_OUTPUT | NPL_PORT_TEXTUAL | NPL_PORT_BINARY);
    g_eof_object = make_eof_object();
    npl_gc_root(&g_args_list);
    npl_gc_root(&g_current_input_port);
    npl_gc_root(&g_current_output_port);
    npl_gc_root(&g_current_error_port);
    npl_gc_root(&g_eof_object);
}

/* ---- Display (for debugging) ---- */

static void display_to(FILE *stream, NplVal value)
{
    if (npl_is_fixnum(value)) {
        fprintf(stream, "%lld", (long long)npl_fixnum_value(value));
    } else if (npl_is_nil(value)) {
        fputs("()", stream);
    } else if (value == npl_true()) {
        fputs("#t", stream);
    } else if (value == npl_false()) {
        fputs("#f", stream);
    } else if (npl_is_char(value)) {
        fputc((int)npl_char_value(value), stream);
    } else if (npl_is_ptr(value)) {
        NplHeader *header = npl_header(value);
        switch (header->tag) {
        case BOX_PAIR: {
            NplPair *pair = (NplPair *)header;
            fputc('(', stream);
            display_to(stream, pair->car);
            NplVal rest = pair->cdr;
            while (!npl_is_nil(rest) && npl_is_ptr(rest) &&
                   npl_box_tag(rest) == BOX_PAIR) {
                fputc(' ', stream);
                display_to(stream, npl_car(rest));
                rest = npl_cdr(rest);
            }
            if (!npl_is_nil(rest)) {
                fputs(" . ", stream);
                display_to(stream, rest);
            }
            fputc(')', stream);
            break;
        }
        case BOX_STRING:
        case BOX_SYMBOL: {
            NplString *string = (NplString *)header;
            fwrite(string->data, 1, string->length, stream);
            break;
        }
        case BOX_FLOAT:
            fprintf(stream, "%g", ((NplFloat *)header)->value);
            break;
        case BOX_VECTOR: {
            NplVector *vector = (NplVector *)header;
            fputs("#(", stream);
            for (uint32_t i = 0; i < vector->length; i++) {
                if (i) fputc(' ', stream);
                display_to(stream, vector->data[i]);
            }
            fputc(')', stream);
            break;
        }
        case BOX_BYTEVECTOR: {
            NplBytevector *vector = (NplBytevector *)header;
            fputs("#u8(", stream);
            for (uint32_t i = 0; i < vector->length; i++) {
                if (i) fputc(' ', stream);
                fprintf(stream, "%u", (unsigned)vector->data[i]);
            }
            fputc(')', stream);
            break;
        }
        case BOX_VALUES: fputs("#<multiple-values>", stream); break;
        case BOX_PROMISE: fputs("#<promise>", stream); break;
        case BOX_PORT: fputs("#<port>", stream); break;
        case BOX_EOF: fputs("#<eof>", stream); break;
        default: fputs("#<unknown>", stream); break;
        }
    } else {
        fputs("#<unknown>", stream);
    }
}

static void write_escaped_string(FILE *stream, const NplString *string)
{
    fputc('"', stream);
    for (uint32_t i = 0; i < string->length; i++) {
        unsigned char code = (unsigned char)string->data[i];
        if (code == '"' || code == '\\') {
            fputc('\\', stream);
            fputc(code, stream);
        } else if (code == '\n') {
            fputs("\\n", stream);
        } else if (code == '\r') {
            fputs("\\r", stream);
        } else if (code == '\t') {
            fputs("\\t", stream);
        } else {
            fputc(code, stream);
        }
    }
    fputc('"', stream);
}

static void write_to(FILE *stream, NplVal value)
{
    if (npl_is_char(value)) {
        uint32_t code = npl_char_value(value);
        if (code == 32) fputs("#\\space", stream);
        else if (code == 10) fputs("#\\newline", stream);
        else if (code == 9) fputs("#\\tab", stream);
        else fprintf(stream, "#\\%c", (int)code);
        return;
    }
    if (npl_is_ptr(value) && npl_header(value)->tag == BOX_STRING) {
        write_escaped_string(stream, (NplString *)npl_header(value));
        return;
    }
    if (npl_is_ptr(value) && npl_header(value)->tag == BOX_PAIR) {
        NplPair *pair = (NplPair *)npl_header(value);
        fputc('(', stream);
        write_to(stream, pair->car);
        NplVal rest = pair->cdr;
        while (!npl_is_nil(rest) && npl_is_ptr(rest) &&
               npl_box_tag(rest) == BOX_PAIR) {
            fputc(' ', stream);
            write_to(stream, npl_car(rest));
            rest = npl_cdr(rest);
        }
        if (!npl_is_nil(rest)) {
            fputs(" . ", stream);
            write_to(stream, rest);
        }
        fputc(')', stream);
        return;
    }
    if (npl_is_ptr(value) && npl_header(value)->tag == BOX_VECTOR) {
        NplVector *vector = (NplVector *)npl_header(value);
        fputs("#(", stream);
        for (uint32_t i = 0; i < vector->length; i++) {
            if (i) fputc(' ', stream);
            write_to(stream, vector->data[i]);
        }
        fputc(')', stream);
        return;
    }
    display_to(stream, value);
}

NplVal npl_display(NplVal value)
{
    display_to(stdout, value);
    return npl_nil();
}

NplVal npl_newline(void)
{
    fputc('\n', stdout);
    return npl_nil();
}

/* ---- standard port substrate (increment-6.1) ---- */

static NplPort *port_value(NplVal value, const char *who)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_PORT) {
        fprintf(stderr, "%s: not a port\n", who);
        exit(1);
    }
    return (NplPort *)npl_header(value);
}

static NplPort *require_port(NplVal value, uint32_t access,
                             const char *who)
{
    NplPort *port = port_value(value, who);
    if ((port->mode & NPL_PORT_CLOSED) != 0) {
        fprintf(stderr, "%s: port is closed\n", who);
        exit(1);
    }
    if ((port->mode & access) == 0) {
        fprintf(stderr, "%s: wrong port direction\n", who);
        exit(1);
    }
    return port;
}

static NplPort *require_port_mode(NplVal value, uint32_t access,
                                  uint32_t kind, const char *who)
{
    NplPort *port = require_port(value, access, who);
    if ((port->mode & kind) == 0) {
        fprintf(stderr, "%s: wrong port type\n", who);
        exit(1);
    }
    return port;
}

NplVal npl_display_port(NplVal value, NplVal port_value_arg)
{
    NplPort *port = require_port_mode(port_value_arg, NPL_PORT_OUTPUT,
                                     NPL_PORT_TEXTUAL, "display");
    display_to(port->stream, value);
    return npl_nil();
}

NplVal npl_write_port(NplVal value, NplVal port_value_arg)
{
    NplPort *port = require_port_mode(port_value_arg, NPL_PORT_OUTPUT,
                                     NPL_PORT_TEXTUAL, "write");
    write_to(port->stream, value);
    return npl_nil();
}

NplVal npl_newline_port(NplVal port_value_arg)
{
    NplPort *port = require_port_mode(port_value_arg, NPL_PORT_OUTPUT,
                                     NPL_PORT_TEXTUAL, "newline");
    fputc('\n', port->stream);
    return npl_nil();
}

static NplVal open_file_port(NplVal path, const char *mode,
                             uint32_t port_mode, const char *who)
{
    const NplString *name = npl_require_bytes_internal(path, who);
    char *buffer = (char *)malloc((size_t)name->length + 1);
    if (!buffer) {
        fprintf(stderr, "%s: out of memory\n", who);
        exit(1);
    }
    memcpy(buffer, name->data, name->length);
    buffer[name->length] = '\0';
    FILE *stream = fopen(buffer, mode);
    if (!stream) {
        fprintf(stderr, "%s: cannot open '%s'\n", who, buffer);
        free(buffer);
        exit(1);
    }
    free(buffer);
    return make_port(stream, port_mode | NPL_PORT_OWNED);
}

NplVal npl_current_input_port(void) { return g_current_input_port; }
NplVal npl_current_output_port(void) { return g_current_output_port; }
NplVal npl_current_error_port(void) { return g_current_error_port; }

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
        (NPL_PORT_MEMORY | NPL_PORT_BINARY)) {
        fprintf(stderr, "get-output-bytevector: not a bytevector port\n");
        exit(1);
    }
    NplBytevector *buffer = require_bytevector(
        port->buffer, "get-output-bytevector");
    return copy_bytevector_range(buffer->data, port->length);
}

static NplVal close_port_direction(NplVal value, uint32_t access,
                                   const char *who)
{
    NplPort *port = port_value(value, who);
    if ((port->mode & access) == 0) {
        fprintf(stderr, "%s: wrong port direction\n", who);
        exit(1);
    }
    if ((port->mode & NPL_PORT_CLOSED) != 0) return npl_nil();
    if ((port->mode & NPL_PORT_OWNED) != 0 && port->stream) {
        if (fclose(port->stream) != 0) {
            fprintf(stderr, "%s: close failed\n", who);
            exit(1);
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
    if ((port->mode & NPL_PORT_MEMORY) == 0 && fflush(port->stream) != 0) {
        fprintf(stderr, "flush-output-port: flush failed\n");
        exit(1);
    }
    return npl_nil();
}

static size_t port_read_bytes(NplPort *port, uint8_t *output, size_t count)
{
    if ((port->mode & NPL_PORT_MEMORY) == 0)
        return fread(output, 1, count, port->stream);
    NplBytevector *buffer = require_bytevector(port->buffer, "read-u8");
    uint32_t available = port->position < port->length
        ? port->length - port->position : 0;
    if (count > available) count = available;
    if (count) {
        memcpy(output, buffer->data + port->position, count);
        port->position += (uint32_t)count;
    }
    return count;
}

static void ensure_memory_output(NplPort *port, uint32_t needed)
{
    NplBytevector *current = require_bytevector(port->buffer, "write-u8");
    if (needed <= current->capacity) return;
    uint32_t capacity = current->capacity ? current->capacity : 64;
    while (capacity < needed) {
        if (capacity > UINT32_MAX / 2) npl_runtime_oom_internal();
        capacity *= 2;
    }
    NplVal grown_value = make_byte_buffer(capacity);
    NplBytevector *grown = (NplBytevector *)npl_header(grown_value);
    if (port->length) memcpy(grown->data, current->data, port->length);
    port->buffer = grown_value;
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
    NplBytevector *buffer = (NplBytevector *)npl_header(port->buffer);
    if (count) memcpy(buffer->data + port->position, bytes, count);
    port->position = end;
    if (port->length < end) port->length = end;
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

NplVal npl_write_char(NplVal character, NplVal value)
{
    NplPort *port = require_port_mode(value, NPL_PORT_OUTPUT,
                                     NPL_PORT_TEXTUAL, "write-char");
    if (!npl_is_char(character) || npl_char_value(character) > UINT8_MAX) {
        fprintf(stderr, "write-char: expected an ASCII character\n");
        exit(1);
    }
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
        npl_fixnum_value(byte) > UINT8_MAX) {
        fprintf(stderr, "write-u8: expected a byte\n");
        exit(1);
    }
    uint8_t raw = (uint8_t)npl_fixnum_value(byte);
    port_write_bytes(port, &raw, 1, "write-u8");
    return npl_nil();
}

static uint32_t require_nonnegative_count(NplVal value, const char *who)
{
    if (!npl_is_fixnum(value) || npl_fixnum_value(value) < 0 ||
        (uint64_t)npl_fixnum_value(value) > UINT32_MAX) {
        fprintf(stderr, "%s: expected a nonnegative integer\n", who);
        exit(1);
    }
    return (uint32_t)npl_fixnum_value(value);
}

static void require_bytevector_range(NplBytevector *bytes, NplVal start_value,
                                     NplVal end_value, const char *who,
                                     uint32_t *start, uint32_t *end)
{
    *start = require_nonnegative_count(start_value, who);
    *end = require_nonnegative_count(end_value, who);
    if (*start > *end || *end > bytes->length) {
        fprintf(stderr, "%s: invalid bytevector range\n", who);
        exit(1);
    }
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
    if ((bytes->header.flags & NPL_FLAG_LITERAL) != 0) {
        fprintf(stderr, "read-bytevector!: cannot mutate a literal\n");
        exit(1);
    }
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
    FILE *stream;
    int lookahead;
    int has_lookahead;
} NplDatumReader;

static void read_error(const char *message)
{
    fprintf(stderr, "read: %s\n", message);
    exit(1);
}

static int reader_get(NplDatumReader *reader)
{
    if (reader->has_lookahead) {
        reader->has_lookahead = 0;
        return reader->lookahead;
    }
    return fgetc(reader->stream);
}

static int reader_peek(NplDatumReader *reader)
{
    if (!reader->has_lookahead) {
        reader->lookahead = fgetc(reader->stream);
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
        if (code == EOF) read_error("unexpected EOF in string");
        if (code == '"') break;
        if (code == '\\') {
            code = reader_get(reader);
            if (code == EOF) read_error("unexpected EOF in string escape");
            if (code == 'n') code = '\n';
            else if (code == 'r') code = '\r';
            else if (code == 't') code = '\t';
            else if (code != '"' && code != '\\')
                read_error("unknown string escape");
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

    char *end = NULL;
    long long number = strtoll(buffer, &end, 10);
    NplVal value;
    if (end && *end == '\0' && !(length == 2 && buffer[0] == '-'))
        value = npl_fixnum((int64_t)number);
    else
        value = npl_symbol(buffer, (uint32_t)(length - 1));
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

static NplVal read_hash_datum(NplDatumReader *reader)
{
    int code = reader_get(reader);
    if (code == 't') return npl_true();
    if (code == 'f') return npl_false();
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
    NplDatumReader reader = { port->stream, 0, 0 };
    NplVal value = read_datum_inner(&reader, 1);
    if (reader.has_lookahead && reader.lookahead != EOF)
        (void)ungetc(reader.lookahead, reader.stream);
    return value;
}

/* ---- process arguments and whole-file input ---- */

void npl_store_args(int argc, char **argv) {
#ifdef NPL_UNBUFFERED_STDOUT
    setvbuf(stdout, (char *)NULL, _IONBF, 0);
#endif
    g_argc_stored = argc;
    g_argv_stored = argv;
}

/* Build (once) the list of argument strings; argv[0] (program path) omitted. */
NplVal npl_program_arguments(void) {
    if (!npl_is_nil(g_args_list)) {
        return g_args_list;
    }
    NplVal acc = 2; /* nil */
    for (int i = g_argc_stored - 1; i >= 1; i--) {
        NplVal s = npl_string(g_argv_stored[i],
                              (uint32_t)strlen(g_argv_stored[i]));
        acc = npl_cons(s, acc);
    }
    g_args_list = acc;
    return acc;
}

NplVal npl_read_file(NplVal path) {
    const NplString *p = npl_require_bytes_internal(path, "read-file");
    char *buf = (char *)malloc((size_t)p->length + 1);
    if (!buf) { fprintf(stderr, "read-file: oom\n"); exit(1); }
    memcpy(buf, p->data, p->length);
    buf[p->length] = '\0';

    FILE *fp = fopen(buf, "rb");
    free(buf);
    if (!fp) {
        fprintf(stderr, "read-file: cannot open '%.*s'\n",
                (int)p->length, p->data);
        exit(1);
    }
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

void npl_exit(NplVal code) {
    int64_t v = (int64_t)(code >> 2);          /* untag fixnum */
    fflush(stdout);
    _Exit((int)(v & 255));
}
