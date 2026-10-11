/* Private interface between the runtime's port modules:
 *   port.c   port objects, byte and character I/O, memory and file ports
 *   write.c  display / write (datum output)
 *   read.c   read (datum input)
 *   io.c     process context, files, environment
 * Not part of the compiler-facing runtime ABI (npl.h). */

#ifndef NPL_PORT_H
#define NPL_PORT_H

#include "npl.h"
#include <stdio.h>

#define NPL_PORT_INPUT   1u
#define NPL_PORT_OUTPUT  2u
#define NPL_PORT_OWNED   4u
#define NPL_PORT_CLOSED  8u
#define NPL_PORT_TEXTUAL 16u
#define NPL_PORT_BINARY  32u
#define NPL_PORT_MEMORY  64u

NplPort *npl_port_value_internal(NplVal value, const char *who);
NplPort *npl_port_require_mode_internal(NplVal value, uint32_t access,
                                        uint32_t kind, const char *who);
void npl_port_write_bytes_internal(NplPort *port, const uint8_t *bytes,
                                   size_t count, const char *who);
void npl_port_unread_bytes_internal(NplPort *port, const uint8_t *bytes,
                                    size_t count);
/* One UTF-8 character as a code point, or EOF; invalid input is U+FFFD. */
int npl_port_read_char_internal(NplPort *port);
/* Write one character as UTF-8. */
void npl_port_write_char_internal(NplPort *port, uint32_t code);
NplVal npl_port_eof_internal(void);

/* C text (UTF-8) <-> strings, for C APIs. c_string results are malloc'd. */
char *npl_c_string_internal(const NplString *string);
NplVal npl_c_text_internal(const char *text, size_t length);

#endif
