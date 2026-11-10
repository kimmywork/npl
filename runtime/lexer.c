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
             c=='%' || c=='.' || c==':' || c=='$' || c=='&' ||
             c=='^' || c=='~' || c >= 128)     cls = 7; /* symch */
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
    uint32_t len = npl_string_char_count_internal(s);
    uint32_t j = (uint32_t)(jT >> 2);
    while (j < len) {
        uint32_t c = npl_string_char_at_internal(s, j);
        /* Non-ASCII characters are symbol constituents (UTF-8 source). */
        int is_sym = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') ||
                      c=='-' || c=='_' || c=='?' || c=='!' || c=='*' ||
                      c=='/' || c=='+' || c=='=' || c=='<' || c=='>' ||
                      c=='%' || c=='.' || c==':' || c=='$' || c=='&' ||
                      c=='^' || c=='~' || c >= 128);
        if (!is_sym) break;
        j++;
    }
    return npl_fixnum(j);
}

NplVal npl_lex_number(NplVal sT, NplVal iT, NplVal jT) {
    /* Reuse numeric decimal conversion; never accumulate in a host integer. */
    npl_gc_root(&sT);
    NplVal text = npl_substring(sT, iT, jT);
    npl_gc_root(&text);
    NplVal result = npl_integer_from_string(text);
    npl_gc_unroot(&text);
    npl_gc_unroot(&sT);
    return result;
}
