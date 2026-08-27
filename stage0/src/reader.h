#ifndef NPL_READER_H
#define NPL_READER_H

#include "ast.h"
#include <stddef.h>

/*
 * reader.h — Public interface for the NPL reader (lexer + parser).
 */

/* Read one expression from a string.  Returns NULL on error (message in errbuf). */
AstNode *reader_read(const char *input, char *errbuf, size_t errbuf_size);

/* Read all top-level expressions from a string, returned as a list of pairs. */
AstNode *reader_read_all(const char *input, char *errbuf, size_t errbuf_size);

/* Read an entire file into a malloc'd buffer.  Caller must free(). */
char *reader_read_file(const char *path);

#endif /* NPL_READER_H */
