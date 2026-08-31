/* Bootstrap lexer primitives used by self-hosted compiler readers. */

#include "npl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- bootstrap lexer hooks (increment-4) ----
 * These keep the self-hosted lexer out of the tagged/raw numeric-domain
 * swamp: character classification and decimal scanning happen here in C. */

static const NplString *lex_str(NplVal s) {
    return npl_require_bytes_internal(s, "lex");
}

/* Returns a PROPER tagged boolean (#f=10/#t=6) so the self-hosted caller
 * can feed it straight into `if`; a raw int return would bypass retagging. */
static inline NplVal npl_bool(int x) { return x ? (NplVal)6 : (NplVal)10; }

NplVal npl_lex_class_w(NplVal cT, NplVal wantT) {
    long c = (long)(cT >> 2);
    long want = (long)(wantT >> 2);
    int cls;
    if (c == 32 || c == 9 || c == 10 || c == 13)      cls = 0; /* ws    */
    else if (c == ';')                                 cls = 1;
    else if (c == '(')                                 cls = 2;
    else if (c == ')')                                 cls = 3;
    else if (c == '\'')                                cls = 4;
    else if (c == '"')                                 cls = 5;
    else if (c >= '0' && c <= '9')                     cls = 6; /* digit */
    else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') ||
             c=='-' || c=='_' || c=='?' || c=='!' || c=='*' ||
             c=='/' || c=='+' || c=='=' || c=='<' || c=='>' ||
             c=='%' || c=='.' || c==':')                 cls = 7; /* symch */
    else if (c == '#')                                 cls = 9; /* hash */
    else if (c == '`')                                 cls = 11;
    else if (c == ',')                                 cls = 12;
    else                                               cls = 8; /* bad  */
    return npl_bool(cls == want);
}

/* Print the TAGGED bits of a fixnum as decimal text ("copy <v>" literal). */
NplVal npl_tagged_num_to_string(NplVal v) {
    if (!npl_is_fixnum(v)) {
        fprintf(stderr, "tagged->string: not a fixnum (v=%lu)\n", (unsigned long)v);
        exit(1);
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)v);
    return npl_box_bytes_internal(buf, (uint32_t)strlen(buf), BOX_STRING);
}

NplVal npl_lex_scan_sym_end(NplVal sT, NplVal jT) {
    const NplString *s = lex_str(sT);
    uint32_t len = s->length;
    uint32_t j = (uint32_t)(jT >> 2);
    while (j < len) {
        unsigned char c = (unsigned char)s->data[j];
        int is_sym = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') ||
                      c=='-' || c=='_' || c=='?' || c=='!' || c=='*' ||
                      c=='/' || c=='+' || c=='=' || c=='<' || c=='>' ||
                      c=='%' || c=='.' || c==':');
        if (!is_sym) break;
        j++;
    }
    return npl_fixnum(j);
}

NplVal npl_lex_number(NplVal sT, NplVal iT, NplVal jT) {
    /* [i,j) may start with '-'; everything raw inside C */
    const NplString *s = lex_str(sT);
    uint32_t i = (uint32_t)(iT >> 2);
    uint32_t j = (uint32_t)(jT >> 2);
    long acc = 0;
    int neg = 0;
    if (i < j && s->data[i] == '-') { neg = 1; i++; }
    while (i < j) {
        if (s->data[i] >= '0' && s->data[i] <= '9')
            acc = acc * 10 + (s->data[i] - '0');
        i++;
    }
    return npl_fixnum(neg ? -acc : acc);
}

static int lex_symbol_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '?' ||
           c == '!' || c == '*' || c == '/' || c == '+' || c == '=' ||
           c == '<' || c == '>' || c == '%' || c == '.' || c == ':';
}

static NplVal lex_token(int kind, NplVal payload)
{
    return npl_cons(npl_fixnum(kind), payload);
}

static NplVal reverse_token_stream(NplVal stream)
{
    NplVal result = npl_nil();
    while (!npl_is_nil(stream)) {
        NplVal next = npl_cdr(stream);
        npl_set_cdr(stream, result);
        result = stream;
        stream = next;
    }
    return result;
}

NplVal npl_lex_tokens(NplVal source)
{
    const NplString *string = lex_str(source);
    const unsigned char *data = (const unsigned char *)string->data;
    uint32_t length = string->length;
    uint32_t i = 0;
    NplVal tokens = npl_nil();

    while (i < length) {
        unsigned char c = data[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            i++;
            continue;
        }
        if (c == ';') {
            while (i < length && data[i] != '\n') i++;
            continue;
        }
        if (c == '(' || c == ')' || c == '\'') {
            int kind = c == '(' ? 1 : (c == ')' ? 2 : 3);
            tokens = npl_cons(lex_token(kind, npl_nil()), tokens);
            i++;
            continue;
        }
        if (c == '"') {
            uint32_t start = ++i;
            while (i < length && data[i] != '"') i++;
            if (i == length) {
                fprintf(stdout, "nplc1: unterminated string literal\n");
                fflush(stdout);
                _Exit(20);
            }
            NplVal text = npl_box_bytes_internal((const char *)data + start,
                                    i - start, BOX_STRING);
            tokens = npl_cons(lex_token(6, text), tokens);
            i++;
            continue;
        }
        if (c == '#') {
            if (i + 1 >= length || (data[i + 1] != 't' && data[i + 1] != 'f')) {
                fprintf(stdout, "nplc1: bad boolean literal\n");
                fflush(stdout);
                _Exit(20);
            }
            int kind = data[i + 1] == 't' ? 8 : 9;
            tokens = npl_cons(lex_token(kind, npl_nil()), tokens);
            i += 2;
            continue;
        }
        if ((c >= '0' && c <= '9') ||
            (c == '-' && i + 1 < length &&
             data[i + 1] >= '0' && data[i + 1] <= '9')) {
            uint32_t start = i;
            int negative = c == '-';
            if (negative) i++;
            int64_t number = 0;
            while (i < length && data[i] >= '0' && data[i] <= '9') {
                number = number * 10 + (data[i] - '0');
                i++;
            }
            if (i < length && data[i] == '.' && i + 1 < length &&
                data[i + 1] >= '0' && data[i + 1] <= '9') {
                i++;
                while (i < length && data[i] >= '0' && data[i] <= '9') i++;
                NplVal text = npl_box_bytes_internal((const char *)data + start,
                                        i - start, BOX_STRING);
                tokens = npl_cons(lex_token(10, text), tokens);
            } else {
                if (negative) number = -number;
                tokens = npl_cons(lex_token(5, npl_fixnum(number)), tokens);
            }
            continue;
        }
        if (lex_symbol_char(c)) {
            uint32_t start = i++;
            while (i < length && lex_symbol_char(data[i])) i++;
            NplVal symbol = npl_box_bytes_internal((const char *)data + start,
                                      i - start, BOX_STRING);
            tokens = npl_cons(lex_token(4, symbol), tokens);
            continue;
        }

        fprintf(stdout, "nplc1: bad character byte %u\n", (unsigned)c);
        fflush(stdout);
        _Exit(20);
    }

    tokens = npl_cons(lex_token(7, npl_nil()), tokens);
    return reverse_token_stream(tokens);
}
