/* port-ready.c — byte/character readiness for stream ports.
 *
 * Readiness needs the stdio buffer fields on macOS/glibc and poll(2) as a
 * fallback, which is the only part of the port substrate that touches the
 * host's polling interface. */

#define _DARWIN_C_SOURCE
#define _GNU_SOURCE

#include "port.h"
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

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
    /* Like npl_port_ready_p: validate the port and read its FILE* under the
     * shared lock, so a concurrent close cannot release the stream between
     * the check and stream_byte_ready(). */
    NPL_SHARED_GUARD();
    NplPort *port = npl_port_require_mode_internal(value, NPL_PORT_INPUT,
                                   NPL_PORT_INPUT, "u8-ready?");
    if ((port->mode & NPL_PORT_MEMORY) != 0) return npl_true();
    return stream_byte_ready(port) ? npl_true() : npl_false();
}

/* char-ready?: a whole character (or EOF) can be read without blocking
 * (.agents/archive/design/design-l6.md, Decision 3). Bytes read while checking a multibyte
 * sequence go to the port's pushback, so nothing is lost. */
NplVal npl_port_ready_p(NplVal value)
{
    NPL_SHARED_GUARD();
    NplPort *port = npl_port_require_mode_internal(value, NPL_PORT_INPUT,
                                   NPL_PORT_INPUT, "char-ready?");
    if ((port->mode & NPL_PORT_MEMORY) != 0) return npl_true();
    if (!stream_byte_ready(port)) return npl_false();
    uint8_t bytes[4];
    size_t got = 0;
    int ready = 1;
    int lead = npl_port_read_byte_raw_internal(port);
    if (lead != EOF) {
        bytes[got++] = (uint8_t)lead;
        int count = npl_utf8_sequence_length_internal((uint8_t)lead);
        while ((int)got < count) {
            if (!stream_byte_ready(port)) { ready = 0; break; }
            int next = npl_port_read_byte_raw_internal(port);
            if (next == EOF) break;
            bytes[got++] = (uint8_t)next;
            if ((next & 0xC0) != 0x80) break;
        }
    }
    if (got) npl_port_unread_bytes_internal(port, bytes, got);
    return ready ? npl_true() : npl_false();
}
