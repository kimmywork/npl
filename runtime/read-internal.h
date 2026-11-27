/* Shared state and helpers for the datum reader modules. */
#ifndef NPL_READ_INTERNAL_H
#define NPL_READ_INTERNAL_H

#include "npl.h"

typedef struct {
    NplPort *port;
    int lookahead;
    int has_lookahead;
    NplVal labels;
    int fold_case;      /* #!fold-case in effect (R7RS 2.1) */
    int keyword_colon;  /* #!colon-keyword: foo: reads as a keyword */
    int neoteric;       /* #!curly-infix: f(x), e{}, e[...] */
    int in_directive;   /* reading #!(name args...) */
} NplDatumReader;

void npl_read_error(const char *message);
int npl_reader_get(NplDatumReader *reader);
int npl_reader_peek(NplDatumReader *reader);
int npl_datum_delimiter(int code);
void npl_reader_skip_space(NplDatumReader *reader);
int npl_symbol_named(NplVal value, const char *name);

NplVal npl_reverse_onto(NplVal values, NplVal tail);
NplVal npl_read_token_datum(NplDatumReader *reader, int first);
NplVal npl_read_list_datum(NplDatumReader *reader, int open);
NplVal npl_read_datum_plain(NplDatumReader *reader, int allow_eof);
NplVal npl_read_datum_inner(NplDatumReader *reader, int allow_eof);
NplVal npl_read_curly_infix(NplDatumReader *reader);
void npl_read_directive(NplDatumReader *reader);

#endif
