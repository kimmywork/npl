#ifndef NPL_READER_H
#define NPL_READER_H

#include "ast.h"

#include <stddef.h>

AstNode *reader_read_all(const char *source, char *error, size_t error_size);
char *reader_read_file(const char *path);

#endif
