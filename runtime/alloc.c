/*
 * alloc.c — NPL runtime memory allocator.
 *
 * Uses malloc for Phase 3 (mmap can be added later).
 * Provides allocation for boxed values (pairs, strings, vectors, floats).
 *
 * Memory model:
 *   - All allocations go through npl_alloc()
 *   - GC handles deallocation via mark-sweep
 *   - Free list for recycled objects
 */

#include "npl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- Global state ---- */

/* Head of allocated object list (for GC traversal) */
static NplHeader *g_alloc_head = NULL;

/* GC thresholds */
static size_t g_allocated = 0;
static size_t g_threshold = 1024 * 1024;  /* 1MB initial threshold */

/* GC roots */
#define MAX_ROOTS 1024
static NplVal *g_roots[MAX_ROOTS];
static int g_root_count = 0;

/* ---- Internal allocation ---- */

static void *npl_alloc(size_t size) {
    void *ptr = calloc(1, size);
    if (!ptr) {
        fprintf(stderr, "npl_alloc: out of memory\n");
        exit(1);
    }
    g_allocated += size;
    return ptr;
}

/* ---- Runtime initialization ---- */

void npl_runtime_init(void) {
    g_alloc_head = NULL;
    g_allocated = 0;
    g_threshold = 1024 * 1024;
    g_root_count = 0;
}

/* ---- Value constructors ---- */

NplVal npl_cons(NplVal car, NplVal cdr) {
    NplPair *p = (NplPair *)npl_alloc(sizeof(NplPair));
    p->header.tag = BOX_PAIR;
    p->header.flags = 0;
    p->car = car;
    p->cdr = cdr;

    /* Link into allocation list */
    p->header.flags = (uint32_t)(uintptr_t)g_alloc_head;
    g_alloc_head = &p->header;

    return (NplVal)(uintptr_t)p | TAG_PTR;
}

NplVal npl_string(const char *s, uint32_t len) {
    size_t size = sizeof(NplString) + len + 1;
    NplString *str = (NplString *)npl_alloc(size);
    str->header.tag = BOX_STRING;
    str->header.flags = 0;
    str->length = len;
    str->capacity = len;
    if (s) {
        memcpy(str->data, s, len);
    }
    str->data[len] = '\0';

    /* Link into allocation list */
    str->header.flags = (uint32_t)(uintptr_t)g_alloc_head;
    g_alloc_head = &str->header;

    return (NplVal)(uintptr_t)str | TAG_PTR;
}

NplVal npl_float(double v) {
    NplFloat *f = (NplFloat *)npl_alloc(sizeof(NplFloat));
    f->header.tag = BOX_FLOAT;
    f->header.flags = 0;
    f->value = v;

    /* Link into allocation list */
    f->header.flags = (uint32_t)(uintptr_t)g_alloc_head;
    g_alloc_head = &f->header;

    return (NplVal)(uintptr_t)f | TAG_PTR;
}

unsigned long npl_runtime_anchor(void) {
    return (unsigned long)(uintptr_t)&g_alloc_head;
}

void npl_tracev(unsigned long v) {
    fprintf(stderr, "[V=%lu]", v);
}

void npl_trace2(unsigned long tag, unsigned long v) {
    fprintf(stderr, "[T%lu=%lu]", tag, v);
}

#include <execinfo.h>

void npl_dump_addrs(void) {
    void *bt[32];
    int n = backtrace(bt, 32);
    fprintf(stderr, "BACKTRACE:\n");
    for (int i = 0; i < n; i++)
        fprintf(stderr, "  #%d %p\n", i, bt[i]);
}

/* ---- Value accessors ---- */

NplVal npl_car(NplVal v) {
    if (!npl_is_ptr(v)) {
        fprintf(stderr, "npl_car: not a pair (v=%lu)\n", (unsigned long)v);
        exit(1);
    }
    NplPair *p = npl_pair_ptr(v);
    if (p->header.tag != BOX_PAIR) {
        fprintf(stderr, "npl_car: not a pair (v=%lu)\n", (unsigned long)v);
        exit(1);
    }
    return p->car;
}

NplVal npl_cdr(NplVal v) {
#ifdef NPL_TRACE_APPEND
    extern int g_trace_depth;
#endif
    if (!npl_is_ptr(v)) {
#ifdef NPL_TRACE_APPEND
        fprintf(stderr, "[cdr-on-nonptr v=%lu]\n", (unsigned long)v);
#endif
        fprintf(stderr, "npl_cdr: NONPTR v=%lu\n", (unsigned long)v);
        {
            extern void npl_dump_addrs(void);
            npl_dump_addrs();
        }
        exit(1);
    }
    NplPair *p = npl_pair_ptr(v);
    if (p->header.tag != BOX_PAIR) {
        void *lr1b = __builtin_return_address(0);
        fprintf(stderr, "npl_cdr: badtag v=%lu from=%p\n",
                (unsigned long)v, lr1b);
        exit(1);
    }
    return p->cdr;
}

void npl_set_car(NplVal pair, NplVal val) {
    if (!npl_is_ptr(pair)) return;
    NplPair *p = npl_pair_ptr(pair);
    if (p->header.tag != BOX_PAIR) return;
    p->car = val;
}

void npl_set_cdr(NplVal pair, NplVal val) {
    if (!npl_is_ptr(pair)) return;
    NplPair *p = npl_pair_ptr(pair);
    if (p->header.tag != BOX_PAIR) return;
    p->cdr = val;
}

/* ---- GC root management ---- */

void npl_gc_root(NplVal *root) {
    if (g_root_count < MAX_ROOTS) {
        g_roots[g_root_count++] = root;
    }
}

void npl_gc_unroot(NplVal *root) {
    for (int i = 0; i < g_root_count; i++) {
        if (g_roots[i] == root) {
            g_roots[i] = g_roots[--g_root_count];
            return;
        }
    }
}

/* ---- GC stats ---- */

size_t npl_gc_allocated(void) {
    return g_allocated;
}

size_t npl_gc_threshold(void) {
    return g_threshold;
}

/* ---- Display (for debugging) ---- */

void npl_display(NplVal v) {
    if (npl_is_fixnum(v)) {
        printf("%lld", (long long)npl_fixnum_value(v));
    } else if (npl_is_nil(v)) {
        printf("()");
    } else if (v == npl_true()) {
        printf("#t");
    } else if (v == npl_false()) {
        printf("#f");
    } else if (npl_is_ptr(v)) {
        NplHeader *h = npl_header(v);
        switch (h->tag) {
        case BOX_PAIR: {
            NplPair *p = (NplPair *)h;
            printf("(");
            npl_display(p->car);
            NplVal rest = p->cdr;
            while (!npl_is_nil(rest) && npl_is_ptr(rest) && npl_box_tag(rest) == BOX_PAIR) {
                printf(" ");
                npl_display(npl_car(rest));
                rest = npl_cdr(rest);
            }
            if (!npl_is_nil(rest)) {
                printf(" . ");
                npl_display(rest);
            }
            printf(")");
            break;
        }
        case BOX_STRING: {
            NplString *s = (NplString *)h;
            /* R7RS display prints strings without quotes */
            printf("%.*s", (int)s->length, s->data);
            break;
        }
        case BOX_SYMBOL: {
            NplSymbolLike *s = (NplSymbolLike *)h;
            printf("%.*s", (int)s->length, s->data);
            break;
        }
        case BOX_FLOAT: {
            NplFloat *f = (NplFloat *)h;
            printf("%g", f->value);
            break;
        }
        default:
            printf("<unknown>");
        }
    } else {
        printf("<unknown>");
    }
}

void npl_newline(void) {
    printf("\n");
}

/* ---- String & symbol builtins (increment-2) ---- */

static NplVal box_bytes(const char *bytes, uint32_t len, uint32_t box_tag) {
    size_t size = sizeof(NplString) + (size_t)len + 1;
    NplString *str = (NplString *)npl_alloc(size);
    str->header.tag = box_tag;
    str->header.flags = 0;
    str->length = len;
    str->capacity = len;
    if (len && bytes) memcpy(str->data, bytes, len);
    str->data[len] = '\0';
    str->header.flags = (uint32_t)(uintptr_t)g_alloc_head;
    g_alloc_head = &str->header;
    return (NplVal)(uintptr_t)str | TAG_PTR;
}

static const NplString *require_bytes(NplVal v, const char *who) {
    if (!npl_is_ptr(v)) {
        fprintf(stderr, "%s: not a string/symbol\n", who);
        exit(1);
    }
    uint32_t tag = npl_header(v)->tag;
    if (tag != BOX_STRING && tag != BOX_SYMBOL) {
        fprintf(stderr, "%s: not a string/symbol\n", who);
        exit(1);
    }
    return (const NplString *)npl_header(v);
}

NplVal npl_string_append(NplVal a, NplVal b) {
#ifdef NPL_TRACE_APPEND
    fprintf(stderr, "[append a=%lu b=%lu]", (unsigned long)a, (unsigned long)b);
#endif
    const NplString *sa = require_bytes(a, "string-append");
    const NplString *sb = require_bytes(b, "string-append");
#ifdef NPL_TRACE_APPEND
    fprintf(stderr, "[alen=%u blen=%u]", sa->length, sb->length);
#endif
    uint32_t len = sa->length + sb->length;
    char *tmp = (char *)malloc((size_t)len + 1);
    if (!tmp) { fprintf(stderr, "string-append: oom\n"); exit(1); }
    memcpy(tmp, sa->data, sa->length);
    memcpy(tmp + sa->length, sb->data, sb->length);
    tmp[len] = '\0';
    NplVal out = npl_string(tmp, len);
    free(tmp);
    return out;
}

int64_t npl_string_length(NplVal s) {
    return (int64_t)require_bytes(s, "string-length")->length;
}

/* Returns the byte at index i as a fixnum (v1: single-byte strings). */
NplVal npl_string_ref(NplVal s, NplVal i) {
    const NplString *st = require_bytes(s, "string-ref");
    int64_t idx = (int64_t)(i >> 2);   /* expect tagged fixnum */
    if (idx < 0 || idx >= (int64_t)st->length) {
        fprintf(stderr, "string-ref: index out of range\n");
        exit(1);
    }
    return npl_fixnum((unsigned char)st->data[idx]);
}

int npl_string_equal(NplVal a, NplVal b) {
    const NplString *sa = require_bytes(a, "string=?");
    const NplString *sb = require_bytes(b, "string=?");
    if (sa->length != sb->length) return 0;
    return memcmp(sa->data, sb->data, sa->length) == 0;
}

NplVal npl_number_to_string(NplVal v) {
#ifdef NPL_TRACE_APPEND
    fprintf(stderr, "[num2str v=%lu]", (unsigned long)v);
#endif
    if (!npl_is_fixnum(v)) {
        fprintf(stderr, "number->string: not a fixnum (v=%lu)\n", (unsigned long)v);
        exit(1);
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)npl_fixnum_value(v));
    return npl_string(buf, (uint32_t)strlen(buf));
}

/* Copy bytes into a box with a different header tag (symbol<->string). */
static NplVal retag_box(NplVal v, uint32_t new_tag) {
    const NplString *src = (const NplString *)npl_header(v);
    return box_bytes(src->data, src->length, new_tag);
}

NplVal npl_symbol_to_string(NplVal s) {
    require_bytes(s, "symbol->string");
    return retag_box(s, BOX_STRING);
}

/* ---- bootstrap I/O & control (increment-4) ---- */

static int g_argc_stored = 0;
static char **g_argv_stored = NULL;
static NplVal g_args_list = 2;   /* nil until first program-arguments call */

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
    const NplString *p = require_bytes(path, "read-file");
    char *buf = (char *)malloc((size_t)p->length + 1);
    if (!buf) { fprintf(stderr, "read-file: oom\n"); exit(1); }
    memcpy(buf, p->data, p->length);
    buf[p->length] = '\0';

#ifdef NPL_TRACE_APPEND
    fprintf(stderr, "[readfile '%s']", buf);
#endif
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
#ifdef NPL_TRACE_APPEND
    fprintf(stderr, "[size=%zu]", got);
#endif
    NplVal out = npl_string(data, (uint32_t)got);
    free(data);
    return out;
}

NplVal npl_substring(NplVal s, NplVal i, NplVal j) {
    const NplString *st = require_bytes(s, "substring");
    int64_t a = (int64_t)(i >> 2);
    int64_t b = (int64_t)(j >> 2);
    if (a < 0 || b < a || b > (int64_t)st->length) {
        fprintf(stderr, "substring: bad range\n");
        exit(1);
    }
    return box_bytes(st->data + a, (uint32_t)(b - a), st->header.tag);
}

NplVal npl_int_to_string_char(NplVal code) {
    int64_t v = (int64_t)(code >> 2);
    if (v < 1 || v > 255) {
        fprintf(stderr, "int->string: byte out of range\n");
        exit(1);
    }
    char one = (char)v;
    return box_bytes(&one, 1, BOX_STRING);
}

/* ---- bootstrap lexer hooks (increment-4) ----
 * These keep the self-hosted lexer out of the tagged/raw numeric-domain
 * swamp: character classification and decimal scanning happen here in C. */

static const NplString *lex_str(NplVal s) {
    return require_bytes(s, "lex");
}

/* Returns a PROPER tagged boolean (#f=10/#t=6) so the self-hosted caller
 * can feed it straight into `if`; a raw int return would bypass retagging. */
static inline NplVal npl_bool(int x) { return x ? (NplVal)6 : (NplVal)10; }

NplVal npl_lex_class_w(NplVal cT, NplVal wantT) {
    long c = (long)(cT >> 2);
    long want = (long)(wantT >> 2);
    int cls;
#ifdef NPL_TRACE_APPEND
    fprintf(stderr, "[class c=%ld want=%ld]", c, want);
#endif
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
             c=='%' || c=='.')                          cls = 7; /* symch */
    else if (c == '#')                                 cls = 9; /* hash */
    else if (c == '+')                                 cls = 10;
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
    return box_bytes(buf, (uint32_t)strlen(buf), BOX_STRING);
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
                      c=='%' || c=='.');
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

void npl_exit(NplVal code) {
    int64_t v = (int64_t)(code >> 2);          /* untag fixnum */
    fflush(stdout);
    _Exit((int)(v & 255));
}


/* ---- Helper: fixnum to string ---- */

char *npl_fixnum_to_string(int64_t v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)v);
    return strdup(buf);
}
