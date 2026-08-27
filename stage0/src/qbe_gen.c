/*
 * qbe_gen.c — QBE IL code generator for stage0 compiler.
 *
 * Phase 2: Minimal subset (fixnum arithmetic, if, define, lambda, strings).
 *
 * The generated QBE IL uses:
 *   - w (word/32-bit) for integers
 *   - l (long/64-bit) for pointers
 *   - data for string constants
 *   - function definitions with proper ABI
 *
 * Example:
 *   (define (square x) (* x x))
 *   generates:
 *     function w $square(w %x) {
 *     @start
 *         %r =w mul %x, %x
 *         ret %r
 *     }
 */

#include "qbe_gen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* Forward declarations */
static void gen_function(QbeCtx *ctx, const char *name, AstNode *params, AstNode *body);
static void gen_function_to(QbeCtx *ctx, const char *name, AstNode *params, AstNode *body, FILE *out);
static const char *gen_expr(QbeCtx *ctx, AstNode *expr, int depth);
static AstNode *collect_free_vars_impl(AstNode *expr, SymTab *symtab, AstNode *exclude_params);
static AstNode *collect_free_vars_seq(AstNode *exprs, SymTab *symtab, AstNode *exclude);

/* Helper: output with indentation */
static void emit(QbeCtx *ctx, const char *fmt, ...)
{
    va_list ap;
    for (int i = 0; i < ctx->depth; i++) fprintf(ctx->out, "    ");
    va_start(ap, fmt);
    vfprintf(ctx->out, fmt, ap);
    va_end(ap);
    fprintf(ctx->out, "\n");
}

/* Helper: output without newline */
static void emit_raw(QbeCtx *ctx, const char *fmt, ...)
{
    va_list ap;
    for (int i = 0; i < ctx->depth; i++) fprintf(ctx->out, "    ");
    va_start(ap, fmt);
    vfprintf(ctx->out, fmt, ap);
    va_end(ap);
}

void qbe_ctx_init(QbeCtx *ctx, FILE *out, SymTab *symtab)
{
    ctx->out = out;
    ctx->lambda_out = tmpfile();  /* buffer for lambda definitions */
    ctx->data_out = tmpfile();    /* buffer for static data definitions */
    ctx->init_out = tmpfile();    /* buffer for runtime global initializers */
    ctx->has_init = 0;
    ctx->symtab = symtab;
    ctx->next_tmp = 0;
    ctx->next_label = 0;
    ctx->depth = 0;
    ctx->in_tail = 0;
    ctx->current_func[0] = '\0';
}

/* Pool of buffers for temporaries and labels (to avoid static buffer reuse) */
#define MAX_TMP 256
static char tmp_pool[MAX_TMP][32];
static int tmp_idx = 0;

const char *qbe_gen_tmp(QbeCtx *ctx)
{
    char *buf = tmp_pool[tmp_idx % MAX_TMP];
    tmp_idx++;
    snprintf(buf, 32, "%%t%d", ctx->next_tmp++);
    return buf;
}

static char label_pool[MAX_TMP][32];
static int label_idx = 0;

const char *qbe_gen_label(QbeCtx *ctx)
{
    char *buf = label_pool[label_idx % MAX_TMP];
    label_idx++;
    snprintf(buf, 32, "@L%d", ctx->next_label++);
    return buf;
}

/* Check if a symbol name is a builtin arithmetic/comparison operator */
static int is_arith_op(const char *name)
{
    return (strcmp(name, "+") == 0 ||
            strcmp(name, "-") == 0 ||
            strcmp(name, "*") == 0 ||
            strcmp(name, "/") == 0 ||
            strcmp(name, "%") == 0);
}

static int is_cmp_op(const char *name)
{
    return (strcmp(name, "<") == 0 ||
            strcmp(name, ">") == 0 ||
            strcmp(name, "=") == 0 ||
            strcmp(name, "<=") == 0 ||
            strcmp(name, ">=") == 0 ||
            strcmp(name, "not=") == 0);
}

static const char *qbe_arith_op(const char *name)
{
    if (strcmp(name, "+") == 0) return "add";
    if (strcmp(name, "-") == 0) return "sub";
    if (strcmp(name, "*") == 0) return "mul";
    if (strcmp(name, "/") == 0) return "div";
    if (strcmp(name, "%") == 0) return "rem";
    return NULL;
}

static const char *qbe_cmp_op(const char *name)
{
    if (strcmp(name, "<") == 0) return "csltw";
    if (strcmp(name, ">") == 0) return "csgtw";
    if (strcmp(name, "=") == 0) return "ceqw";
    if (strcmp(name, "<=") == 0) return "cslew";
    if (strcmp(name, ">=") == 0) return "csgew";
    if (strcmp(name, "not=") == 0) return "cnew";
    return NULL;
}

/* Fatal error during codegen: never silently emit wrong code (fail-fast) */
static void codegen_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "codegen error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

/* Check if a name is one of the built-in functions the compiler knows.
 * Used by free-variable collection so builtins are not captured as env vars. */
static int is_builtin_name(const char *name)
{
    return (is_arith_op(name) || is_cmp_op(name) ||
            strcmp(name, "display") == 0 || strcmp(name, "puts") == 0 ||
            strcmp(name, "newline") == 0 ||
            strcmp(name, "cons") == 0 || strcmp(name, "car") == 0 ||
            strcmp(name, "cdr") == 0 || strcmp(name, "null?") == 0 ||
            strcmp(name, "not") == 0 || strcmp(name, "eq?") == 0 ||
            strcmp(name, "pair?") == 0 || strcmp(name, "number?") == 0 ||
            strcmp(name, "string-append") == 0 ||
            strcmp(name, "string-length") == 0 ||
            strcmp(name, "string-ref") == 0 ||
            strcmp(name, "string=?") == 0 ||
            strcmp(name, "number->string") == 0 ||
            strcmp(name, "symbol->string") == 0 ||
            strcmp(name, "substring") == 0 ||
            strcmp(name, "int->string") == 0 ||
            strcmp(name, "program-arguments") == 0 ||
            strcmp(name, "read-file") == 0 || strcmp(name, "exit") == 0 ||
            strcmp(name, "set-car!") == 0 || strcmp(name, "set-cdr!") == 0);
}

/* Get the QBE name for a parameter */
static const char *param_qbe_name(AstNode *param)
{
    static char buf[64];
    if (param->tag == AST_SYMBOL) {
        qbe_sanitize_name(param->as.symbol, buf, sizeof(buf));
        /* Prepend % if not already there */
        if (buf[0] != '%') {
            memmove(buf + 1, buf, strlen(buf) + 1);
            buf[0] = '%';
        }
        return buf;
    }
    return "%arg";
}

/*
 * Value representation constants — the single source of truth is
 * runtime/npl.h.  Duplicated here because stage0 does not include it.
 *   fixnum : v << 2 | 0        boxed : ptr | 1        imm : enc << 2 | 2
 *   nil=2  #t=6  #f=10         BOX_PAIR=0 BOX_STRING=1 BOX_VECTOR=2
 *   BOX_SYMBOL=3 BOX_CLOSURE=4 BOX_FLOAT=5
 */
#define NPL_IMM_NIL    2
#define NPL_IMM_TRUE   6
#define NPL_IMM_FALSE  10
#define NPL_BOX_PAIR   0

/* Generate QBE IL for a literal expression (number, boolean, nil) */
static const char *gen_literal(QbeCtx *ctx, AstNode *expr)
{
    char *buf = tmp_pool[tmp_idx % MAX_TMP];
    tmp_idx++;
    switch (expr->tag) {
    case AST_FIXNUM:
        /* Fixnum: value << 2 | TAG_FIXNUM (which is 0) */
        snprintf(buf, 32, "%lld", (long long)(expr->as.fixnum << 2));
        return buf;
    case AST_BOOL:
        return expr->as.bool_val ? "6" : "10";  /* #t / #f immediates */
    case AST_NIL:
        return "2";
    default:
        codegen_error("unsupported literal");
        return "0";
    }
}

/* Re-tag a raw int64 into a fixnum (v << 2). */
static const char *retag_fixnum(QbeCtx *ctx, const char *raw)
{
    const char *result = qbe_gen_tmp(ctx);
    emit(ctx, "%s =l shl %s, 2", result, raw);
    return result;
}

/* Re-tag a raw 0/1 comparison result (:w) into a proper bool.
 * Encodings are NOT affine in {#t=6, #f=10} because immediates interleave
 * with nil (enc<<2 | TAG_IMM, nil enc=0, TRUE enc=1, FALSE enc=2).
 * Result = (#FALSE_BASE) - raw<<2  → raw=0 → 10 (#f); raw=1 → 6 (#t). */
static const char *retag_bool(QbeCtx *ctx, const char *raw)
{
    const char *wide = qbe_gen_tmp(ctx);
    const char *shifted = qbe_gen_tmp(ctx);
    const char *base = qbe_gen_tmp(ctx);
    const char *result = qbe_gen_tmp(ctx);
    emit(ctx, "%s =l extsw %s", wide, raw);
    emit(ctx, "%s =l shl %s, 2", shifted, wide);
    emit(ctx, "%s =l copy %d", base, NPL_IMM_FALSE);
    emit(ctx, "%s =l sub %s, %s", result, base, shifted);
    return result;
}

/* ---- Static data emission (quote / string / global constants) ---- */

static char datum_pool[MAX_TMP][64];
static int datum_idx = 0;
static int next_datum_id = 0;

static void qbe_data_escape(const char *src, char *dst, size_t dst_size)
{
    size_t o = 0;
    for (; *src && o + 2 < dst_size - 1; src++) {
        unsigned char c = (unsigned char)*src;
        if (c == '"' || c == '\\') {
            dst[o++] = '\\'; dst[o++] = c;
        } else if (c == '\n') {
            dst[o++] = '\\'; dst[o++] = 'n';
        } else if (c == '\t') {
            dst[o++] = '\\'; dst[o++] = 't';
        } else if (c >= 32 && c < 127) {
            dst[o++] = c;
        } else {
            /* other control bytes: skip (stage0 restriction) */
        }
    }
    dst[o] = '\0';
}

/* Emit a boxed string-or-symbol object into the data section.
 * box_tag: BOX_STRING (1) or BOX_SYMBOL (3). Layout mirrors runtime/npl.h:
 *   struct { NplHeader{tag,flags}; uint32 length; uint32 capacity; char data[]; } */
static const char *emit_string_box(QbeCtx *ctx, const char *s, int box_tag)
{
    char label_buf[64];
    snprintf(label_buf, sizeof(label_buf), "$boxed_%d", next_datum_id++);
    /* pool entry is write-once; keep the string stable for the process life */
    char *label = datum_pool[datum_idx % MAX_TMP];
    strcpy(label, label_buf);
    datum_idx++;

    char escaped[4096];
    qbe_data_escape(s, escaped, sizeof(escaped));
    size_t len = strlen(s);

    fprintf(ctx->data_out,
            "data %s = { w %d, w 0, w %zu, w %zu, b \"%s\", b 0 }\n",
            label, box_tag, len, len + 1, escaped);
    return label;
}

/* Wrap a static label reference with TAG_PTR ("$lbl+1"); numerics pass through. */
static const char *tagged_data_ref(AstNode *expr, const char *repr)
{
    /* Only heap-object references (labels) need the tag bit. Literals for
     * fixnum/bool/nil already carry their own encoding and contain no '$'. */
    if (repr[0] != '$') return repr;

    static char ref_buf[16][80];
    static int rb = 0;
    char *buf = ref_buf[rb % 16];
    rb++;
    snprintf(buf, 80, "%s+1", repr);
    (void)expr;
    return buf;
}

/* Emit a static representation of a quoted datum.
 * Returns the QBE value string: an immediate constant for numbers/bools,
 * or a "$label" reference for boxed objects. Fails fast on unsupported. */
/* Dedup table for already-emitted symbol boxes.
 * Returns a stable pointer to the canonical QBE label ("$sym_NAME");
 * *is_new is set to 1 the first time a given symbol name is seen. */
#define MAX_EMITTED_SYMS 1024
#define SYM_LABEL_MAX 128
static char sym_bare_names[MAX_EMITTED_SYMS][SYM_LABEL_MAX];
static char sym_qbe_labels[MAX_EMITTED_SYMS][SYM_LABEL_MAX];
static int emitted_sym_count = 0;

static const char *intern_symbol_label(const char *safe_name, int *is_new)
{
    *is_new = 0;
    for (int i = 0; i < emitted_sym_count; i++) {
        if (strcmp(sym_bare_names[i], safe_name) == 0) {
            return sym_qbe_labels[i];
        }
    }
    if (emitted_sym_count >= MAX_EMITTED_SYMS) {
        codegen_error("too many distinct quoted symbols (max %d)", MAX_EMITTED_SYMS);
    }
    snprintf(sym_bare_names[emitted_sym_count], SYM_LABEL_MAX, "%s", safe_name);
    char *label_slot = sym_qbe_labels[emitted_sym_count++];
    snprintf(label_slot, SYM_LABEL_MAX, "$sym_%s", safe_name);
    *is_new = 1;
    return label_slot;
}

static const char *emit_static_datum(QbeCtx *ctx, AstNode *expr)
{
    switch (expr->tag) {
    case AST_QUOTE:
        /* nested quote in global initializers: unwrap */
        return emit_static_datum(ctx, AST_CAR(expr));

    case AST_FIXNUM:
    case AST_BOOL:
    case AST_NIL:
        return gen_literal(ctx, expr);

    case AST_SYMBOL: {
        /* Interned symbols are deduplicated: same name -> same label,
         * one data definition, eq? works by construction. */
        char safe_name[128];
        qbe_sanitize_name(expr->as.symbol, safe_name, sizeof(safe_name));
        int is_new = 0;
        const char *label = intern_symbol_label(safe_name, &is_new);
        if (is_new) {
            fprintf(ctx->data_out,
                    "data %s = { w 3, w 0, w %zu, w %zu, b \"%s\", b 0 }\n",
                    label, strlen(expr->as.symbol), strlen(expr->as.symbol) + 1,
                    expr->as.symbol);
        }
        return label;
    }

    case AST_STRING:
        return emit_string_box(ctx, expr->as.string, 1);

    case AST_PAIR: {
        /* Pair: recursively emit car and cdr. Lists terminate in nil.
         * Heap-object references carry TAG_PTR directly in the data. */
        char *label = datum_pool[datum_idx % MAX_TMP];
        datum_idx++;
        snprintf(label, 64, "$pair_%d", next_datum_id++);
        const char *car_r = tagged_data_ref(AST_CAR(expr),
            emit_static_datum(ctx, AST_CAR(expr)));
        const char *cdr_val = expr->as.pair.cdr && !AST_IS_NIL(expr->as.pair.cdr)
                            ? emit_static_datum(ctx, expr->as.pair.cdr)
                            : "2";
        const char *cdr_r = expr->as.pair.cdr && !AST_IS_NIL(expr->as.pair.cdr)
                            ? tagged_data_ref(expr->as.pair.cdr, cdr_val)
                            : cdr_val;
        fprintf(ctx->data_out,
                "data %s = { w 0, w 0, l %s, l %s }\n", label, car_r, cdr_r);
        return label;
    }

    case AST_LAMBDA:
        codegen_error("cannot quote a lambda");
        return "0";

    default:
        codegen_error("unsupported quoted datum (tag=%d)", expr->tag);
        return "0";
    }
}

/* Forward declaration */
static const char *gen_expr(QbeCtx *ctx, AstNode *expr, int depth);



/* Produce a tagged pointer (| TAG_PTR) for a static data label. */
static const char *tagged_ptr(QbeCtx *ctx, const char *label)
{
    const char *addr = qbe_gen_tmp(ctx);
    const char *val = qbe_gen_tmp(ctx);
    emit(ctx, "%s =l copy %s", addr, label);
    emit(ctx, "%s =l add %s, 1", val, addr);
    return val;
}

/* Generate code for (quote <datum>).
 * Immediates need no address; boxed data are emitted statically then tagged. */
static const char *gen_quote(QbeCtx *ctx, AstNode *datum)
{
    if (!datum || AST_IS_NIL(datum)) return "2";
    switch (datum->tag) {
    case AST_FIXNUM:
    case AST_BOOL:
        return gen_literal(ctx, datum);
    default:
        return tagged_ptr(ctx, emit_static_datum(ctx, datum));
    }
}

/* Forward declaration */
static const char *gen_expr(QbeCtx *ctx, AstNode *expr, int depth);

/* Generate QBE IL for a body sequence with internal defines.
 * Handles both (lambda ...) bodies and (let ...) bodies.
 *
 *   body := (define name expr)* expr+     (R7RS letrec*-like order)
 *
 * Each internal define binds its name BEFORE evaluating its value,
 * giving letrec* visibility; however names defined this way must not be
 * captured by lambdas inside the same body (see design doc §2.4). */
static const char *gen_body(QbeCtx *ctx, AstNode *body, int depth)
{
    const char *last_val = "0";
    int has_value = 0;

    while (body && !AST_IS_NIL(body)) {
        AstNode *e = AST_CAR(body);

        if (e && e->tag == AST_DEFINE) {
            /* Internal define: bind name first, then evaluate value
             * into its slot (letrec* ordering). */
            AstNode *name_or_sig = AST_CAR(e);
            AstNode *value = AST_CDR(e);

            if (name_or_sig->tag == AST_PAIR) {
                /* (define (f args...) body...) inside a body is not
                 * supported at stage0; use (define f (lambda ...)). */
                codegen_error("internal function shorthand (define (f ...) ...) "
                              "is only valid at top level");
            }
            if (!name_or_sig || name_or_sig->tag != AST_SYMBOL) {
                codegen_error("malformed internal define");
            }
            Symbol *sym = symtab_add_local(ctx->symtab, name_or_sig->as.symbol);
            const char *val = gen_expr(ctx, value, depth);
            emit(ctx, "%s =l copy %s", sym->as.local.qbe_name, val);
        } else {
            last_val = gen_expr(ctx, e, depth);
            has_value = 1;
        }
        body = AST_CDR(body);
    }

    return has_value ? last_val : "0";
}

/* Generate QBE IL for an expression */
static const char *gen_expr(QbeCtx *ctx, AstNode *expr, int depth)
{
    if (!expr) return "2";

    switch (expr->tag) {
    case AST_FIXNUM:
    case AST_BOOL:
    case AST_NIL:
        return gen_literal(ctx, expr);

    case AST_SYMBOL: {
        /* Look up in symbol table */
        Symbol *sym = symtab_lookup(ctx->symtab, expr->as.symbol);
        if (sym && sym->kind == SYM_LOCAL) {
            return sym->as.local.qbe_name;
        }
        if (sym && sym->kind == SYM_GLOBAL_FUNC) {
            /* Global function reference — create a closure object */
            char safe_name[128];
            qbe_sanitize_name(expr->as.symbol, safe_name, sizeof(safe_name));
            const char *closure = qbe_gen_tmp(ctx);
            const char *func_ptr = qbe_gen_tmp(ctx);
            const char *env_slot = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l alloc8 16", closure);
            emit(ctx, "%s =l copy $%s", func_ptr, safe_name);
            emit(ctx, "storel %s, %s", func_ptr, closure);
            emit(ctx, "%s =l add %s, 8", env_slot, closure);
            emit(ctx, "storel 0, %s", env_slot);
            return closure;
        }
        if (sym && sym->kind == SYM_GLOBAL_VAR) {
            /* Global variable: the data slot already holds a fully-tagged
             * NplVal (heap refs stored as "$label+1"), so just load it. */
            char safe_name[128];
            qbe_sanitize_name(sym->name, safe_name, sizeof(safe_name));
            const char *val = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l loadl $gv_%s", val, safe_name);
            return val;
        }
        if (is_builtin_name(expr->as.symbol)) {
            codegen_error("builtin '%s' used as a value is not supported", expr->as.symbol);
        }
        codegen_error("unbound symbol '%s'", expr->as.symbol);
        return "0";
    }

    case AST_STRING:
        return tagged_ptr(ctx, emit_string_box(ctx, expr->as.string, 1));

    case AST_QUOTE:
        return gen_quote(ctx, AST_CAR(expr));

    case AST_IF: {
        /* (if test consequent alternate) */
#ifdef NPL_DEBUG
        fprintf(stderr, "[gen_expr] AST_IF at depth %d\n", depth);
#endif
        AstNode *test   = AST_CAR(expr);
        AstNode *conseq = AST_CAR(AST_CDR(expr));
        AstNode *alt    = AST_CAR(AST_CDR(AST_CDR(expr)));

        /* The result crosses basic blocks (assigned in both branches,
         * read after the merge). Per doc/il.txt ~ Phi we pass it through
         * a stack slot instead of relying on cross-block temporaries. */
        const char *slot = qbe_gen_tmp(ctx);
        emit(ctx, "%s =l alloc8 8", slot);

        const char *ltrue  = qbe_gen_label(ctx);
        const char *lfalse = qbe_gen_label(ctx);
        const char *ldone  = qbe_gen_label(ctx);

        /* Generate test condition */
        const char *test_val = gen_expr(ctx, test, depth + 1);

        /* R7RS: only #f is false. Compare against the #f immediate. */
        const char *truthy = qbe_gen_tmp(ctx);
        emit(ctx, "%s =w cnel %s, %d", truthy, test_val, NPL_IMM_FALSE);
        emit(ctx, "jnz %s, %s, %s", truthy, ltrue, lfalse);

        /* True branch */
        fprintf(ctx->out, "%s\n", ltrue);
        const char *true_val = gen_expr(ctx, conseq, depth + 1);
        emit(ctx, "storel %s, %s", true_val, slot);
        emit(ctx, "jmp %s", ldone);

        /* False branch */
        fprintf(ctx->out, "%s\n", lfalse);
        const char *false_val = gen_expr(ctx, alt, depth + 1);
        emit(ctx, "storel %s, %s", false_val, slot);
        emit(ctx, "jmp %s", ldone);

        /* Merge */
        fprintf(ctx->out, "%s\n", ldone);
        const char *result = qbe_gen_tmp(ctx);
        emit(ctx, "%s =l loadl %s", result, slot);
        return result;
    }

    case AST_PAIR: {
        /* Function application: (f arg1 arg2 ...) */
        AstNode *func = AST_CAR(expr);
        AstNode *args = AST_CDR(expr);

        if (func->tag != AST_SYMBOL) {
            /* Lambda application or complex expression */
            /* Generate the function expression first */
            const char *func_val = gen_expr(ctx, func, depth + 1);
            if (!func_val || strcmp(func_val, "0") == 0) return "0";

            /* Generate arguments */
            int ac = 0;
            char av[16][64];
            AstNode *al = args;
            while (al && !AST_IS_NIL(al) && ac < 16) {
                const char *v = gen_expr(ctx, AST_CAR(al), depth + 1);
                strncpy(av[ac], v, 63);
                av[ac][63] = '\0';
                ac++;
                al = AST_CDR(al);
            }

            /* Extract func_ptr and env from closure */
            const char *fp = qbe_gen_tmp(ctx);
            const char *ep = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l loadl %s", fp, func_val);
            const char *es = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l add %s, 8", es, func_val);
            emit(ctx, "%s =l loadl %s", ep, es);

            /* Call */
            const char *result = qbe_gen_tmp(ctx);
            if (ac == 0) {
                emit(ctx, "%s =l call %s(l %s)", result, fp, ep);
            } else if (ac == 1) {
                emit(ctx, "%s =l call %s(l %s, l %s)", result, fp, ep, av[0]);
            } else if (ac == 2) {
                emit(ctx, "%s =l call %s(l %s, l %s, l %s)", result, fp, ep, av[0], av[1]);
            } else {
                emit_raw(ctx, "%s =l call %s(l %s, l %s", result, fp, ep, av[0]);
                for (int i = 1; i < ac; i++) fprintf(ctx->out, ", l %s", av[i]);
                fprintf(ctx->out, ")\n");
            }
            return result;
        }

        const char *fname = func->as.symbol;

        /* Check for built-in arithmetic */
        if (is_arith_op(fname)) {
            /* Binary arithmetic: (+ a b) */
            AstNode *arg1 = AST_CAR(args);
            AstNode *arg2 = AST_CAR(AST_CDR(args));

            const char *v1 = gen_expr(ctx, arg1, depth + 1);
            const char *v2 = gen_expr(ctx, arg2, depth + 1);
            const char *result = qbe_gen_tmp(ctx);

            const char *op = qbe_arith_op(fname);

            if (strcmp(fname, "+") == 0 || strcmp(fname, "-") == 0) {
                /* For add/sub: tagged pointer arithmetic works directly */
                /* (a << 2) + (b << 2) = (a + b) << 2 */
                emit(ctx, "%s =l %s %s, %s", result, op, v1, v2);
            } else {
                /* For mul/div/rem: extract values, operate, then re-tag */
                const char *tmp1 = qbe_gen_tmp(ctx);
                const char *tmp2 = qbe_gen_tmp(ctx);
                const char *tmp3 = qbe_gen_tmp(ctx);
                emit(ctx, "%s =l sar %s, 2", tmp1, v1);  /* extract v1 */
                emit(ctx, "%s =l sar %s, 2", tmp2, v2);  /* extract v2 */
                emit(ctx, "%s =l %s %s, %s", tmp3, op, tmp1, tmp2);  /* operate */
                emit(ctx, "%s =l shl %s, 2", result, tmp3);  /* re-tag */
            }
            return result;
        }

        if (is_cmp_op(fname)) {
            /* Comparison: (< a b) — result re-tagged as proper bool */
            AstNode *arg1 = AST_CAR(args);
            AstNode *arg2 = AST_CAR(AST_CDR(args));

            const char *v1 = gen_expr(ctx, arg1, depth + 1);
            const char *v2 = gen_expr(ctx, arg2, depth + 1);

            const char *op = qbe_cmp_op(fname);
            const char *tmp1 = qbe_gen_tmp(ctx);
            const char *tmp2 = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l sar %s, 2", tmp1, v1);  /* extract v1 */
            emit(ctx, "%s =l sar %s, 2", tmp2, v2);  /* extract v2 */
            const char *raw = qbe_gen_tmp(ctx);
            emit(ctx, "%s =w %s %s, %s", raw, op, tmp1, tmp2);
            return retag_bool(ctx, raw);
        }

        /* Check for display/puts — route everything through the runtime
         * printer (handles fixnum/bool/nil/pair/string/symbol). */
        if (strcmp(fname, "display") == 0 || strcmp(fname, "puts") == 0) {
            AstNode *arg = AST_CAR(args);
            const char *val = gen_expr(ctx, arg, depth + 1);
            const char *result = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l call $npl_display(l %s)", result, val);
            return result;
        }

        /* (newline): print a single '\n' */
        if (strcmp(fname, "newline") == 0) {
            const char *result = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l call $npl_newline()", result);
            return result;
        }

        /* Check for cons (runtime function) */
        if (strcmp(fname, "cons") == 0) {
            AstNode *arg1 = AST_CAR(args);
            AstNode *arg2 = AST_CAR(AST_CDR(args));
            const char *v1 = gen_expr(ctx, arg1, depth + 1);
            const char *v2 = gen_expr(ctx, arg2, depth + 1);
            const char *result = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l call $npl_cons(l %s, l %s)", result, v1, v2);
            return result;
        }

        /* Check for car (runtime function) */
        if (strcmp(fname, "car") == 0) {
            AstNode *arg = AST_CAR(args);
            const char *val = gen_expr(ctx, arg, depth + 1);
            const char *result = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l call $npl_car(l %s)", result, val);
            return result;
        }

        /* Check for cdr (runtime function) */
        if (strcmp(fname, "cdr") == 0) {
            AstNode *arg = AST_CAR(args);
            const char *val = gen_expr(ctx, arg, depth + 1);
            const char *result = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l call $npl_cdr(l %s)", result, val);
            return result;
        }

        /* Check for null? (check if nil) — returns proper bool */
        if (strcmp(fname, "null?") == 0) {
            AstNode *arg = AST_CAR(args);
            const char *val = gen_expr(ctx, arg, depth + 1);
            const char *raw = qbe_gen_tmp(ctx);
            emit(ctx, "%s =w ceql %s, %d", raw, val, NPL_IMM_NIL);
            return retag_bool(ctx, raw);
        }

        /* (number? x): low tag bits == TAG_FIXNUM(0) */
        if (strcmp(fname, "number?") == 0) {
            AstNode *arg = AST_CAR(args);
            const char *val = gen_expr(ctx, arg, depth + 1);
            const char *low = qbe_gen_tmp(ctx);
            emit(ctx, "%s =w and %s, 3", low, val);
            const char *raw = qbe_gen_tmp(ctx);
            emit(ctx, "%s =w ceqw %s, 0", raw, low);
            return retag_bool(ctx, raw);
        }

        /* (not x): #t iff x is exactly #f */
        if (strcmp(fname, "not") == 0) {
            AstNode *arg = AST_CAR(args);
            const char *val = gen_expr(ctx, arg, depth + 1);
            const char *raw = qbe_gen_tmp(ctx);
            emit(ctx, "%s =w ceql %s, %d", raw, val, NPL_IMM_FALSE);
            return retag_bool(ctx, raw);
        }

        /* (eq? a b): pointer-level identity. Fixnums compare by value
         * (v<<2 tag is exact); boxed values compare by address; the two
         * immediates #t/#f/nil are unique constants. */
        if (strcmp(fname, "eq?") == 0) {
            AstNode *arg1 = AST_CAR(args);
            AstNode *arg2 = AST_CAR(AST_CDR(args));
            const char *v1 = gen_expr(ctx, arg1, depth + 1);
            const char *v2 = gen_expr(ctx, arg2, depth + 1);
            const char *raw = qbe_gen_tmp(ctx);
            emit(ctx, "%s =w ceql %s, %s", raw, v1, v2);
            return retag_bool(ctx, raw);
        }

        /* (pair? x): low-2-bits == TAG_PTR and header.tag == BOX_PAIR.
         * Two branches to avoid loading the header of a non-pointer.
         * The pointer must be untagged (& ~3) before the header load. */
        if (strcmp(fname, "pair?") == 0) {
            AstNode *arg = AST_CAR(args);
            const char *val = gen_expr(ctx, arg, depth + 1);
            const char *low = qbe_gen_tmp(ctx);
            emit(ctx, "%s =w and %s, 3", low, val);
            const char *isp = qbe_gen_tmp(ctx);
            emit(ctx, "%s =w ceqw %s, 1", isp, low);

            const char *lptr = qbe_gen_label(ctx);
            const char *lnoptr = qbe_gen_label(ctx);
            const char *ldone = qbe_gen_label(ctx);
            const char *raw = qbe_gen_tmp(ctx);

            emit(ctx, "jnz %s, %s, %s", isp, lptr, lnoptr);
            fprintf(ctx->out, "%s\n", lptr);
            const char *addr = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l add %s, -4", addr, val);  /* strip tag: ptr & ~3 */
            const char *hdr = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l loadsw %s", hdr, addr);
            emit(ctx, "%s =w ceqw %s, %d", raw, hdr, NPL_BOX_PAIR);
            emit(ctx, "jmp %s", ldone);
            fprintf(ctx->out, "%s\n", lnoptr);
            emit(ctx, "%s =l copy 0", raw);
            fprintf(ctx->out, "%s\n", ldone);
            return retag_bool(ctx, raw);
        }

        /* ---- string & symbol builtins (increment-2) ---- */
        /* (string-append s ...) — R7RS variadic: fold left to right */
        if (strcmp(fname, "string-append") == 0) {
            int n = 0;
            AstNode *a0 = args;
            while (a0 && !AST_IS_NIL(a0)) { n++; a0 = AST_CDR(a0); }
            if (n < 2) {
                codegen_error("string-append requires at least 2 arguments");
            }
            const char *acc = gen_expr(ctx, AST_CAR(args), depth + 1);
            AstNode *rest = AST_CDR(args);
            while (rest && !AST_IS_NIL(rest)) {
                const char *b = gen_expr(ctx, AST_CAR(rest), depth + 1);
                const char *result = qbe_gen_tmp(ctx);
                emit(ctx, "%s =l call $npl_string_append(l %s, l %s)",
                     result, acc, b);
                acc = result;
                rest = AST_CDR(rest);
            }
            return acc;
        }
        if (strcmp(fname, "string-length") == 0) {
            const char *a = gen_expr(ctx, AST_CAR(args), depth + 1);
            const char *len = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l call $npl_string_length(l %s)", len, a);
            return retag_fixnum(ctx, len);
        }
        if (strcmp(fname, "string-ref") == 0) {
            const char *a = gen_expr(ctx, AST_CAR(args), depth + 1);
            const char *i = gen_expr(ctx, AST_CAR(AST_CDR(args)), depth + 1);
            const char *result = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l call $npl_string_ref(l %s, l %s)", result, a, i);
            return result;
        }
        if (strcmp(fname, "string=?") == 0) {
            const char *a = gen_expr(ctx, AST_CAR(args), depth + 1);
            const char *b = gen_expr(ctx, AST_CAR(AST_CDR(args)), depth + 1);
            const char *raw = qbe_gen_tmp(ctx);
            emit(ctx, "%s =w call $npl_string_equal(l %s, l %s)", raw, a, b);
            return retag_bool(ctx, raw);
        }
        if (strcmp(fname, "number->string") == 0) {
            const char *a = gen_expr(ctx, AST_CAR(args), depth + 1);
            const char *result = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l call $npl_number_to_string(l %s)", result, a);
            return result;
        }
        if (strcmp(fname, "substring") == 0) {
            const char *a = gen_expr(ctx, AST_CAR(args), depth + 1);
            const char *i = gen_expr(ctx, AST_CAR(AST_CDR(args)), depth + 1);
            const char *j = gen_expr(ctx, AST_CAR(AST_CDR(AST_CDR(args))), depth + 1);
            const char *result = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l call $npl_substring(l %s, l %s, l %s)", result, a, i, j);
            return result;
        }
        if (strcmp(fname, "int->string") == 0) {
            const char *a = gen_expr(ctx, AST_CAR(args), depth + 1);
            const char *result = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l call $npl_int_to_string_char(l %s)", result, a);
            return result;
        }
        if (strcmp(fname, "symbol->string") == 0) {
            const char *a = gen_expr(ctx, AST_CAR(args), depth + 1);
            const char *result = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l call $npl_symbol_to_string(l %s)", result, a);
            return result;
        }

        /* ---- bootstrap I/O & control (increment-4) ---- */
        if (strcmp(fname, "program-arguments") == 0) {
            const char *result = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l call $npl_program_arguments()", result);
            return result;
        }
        if (strcmp(fname, "read-file") == 0) {
            const char *a = gen_expr(ctx, AST_CAR(args), depth + 1);
            const char *result = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l call $npl_read_file(l %s)", result, a);
            return result;
        }
        if (strcmp(fname, "exit") == 0) {
            const char *a = gen_expr(ctx, AST_CAR(args), depth + 1);
            emit(ctx, "call $npl_exit(l %s)", a);
            return "2";
        }
        if (strcmp(fname, "set-car!") == 0) {
            const char *p = gen_expr(ctx, AST_CAR(args), depth + 1);
            const char *v = gen_expr(ctx, AST_CAR(AST_CDR(args)), depth + 1);
            emit(ctx, "call $npl_set_car(l %s, l %s)", p, v);
            return "2";
        }
        if (strcmp(fname, "set-cdr!") == 0) {
            const char *p = gen_expr(ctx, AST_CAR(args), depth + 1);
            const char *v = gen_expr(ctx, AST_CAR(AST_CDR(args)), depth + 1);
            emit(ctx, "call $npl_set_cdr(l %s, l %s)", p, v);
            return "2";
        }

        /* User-defined function call */
        /* Check for self-recursive tail call optimization */
        {
            char safe_fname[128];
            qbe_sanitize_name(fname, safe_fname, sizeof(safe_fname));
            if (ctx->in_tail && strcmp(safe_fname, ctx->current_func) == 0) {
                /* Self-recursive tail call!
                 * 1. Generate argument values into temporaries
                 * 2. Assign temps to parameter names
                 * 3. jmp @start (loop instead of call)
                 */
                /* Collect argument values */
                int tc = 0;
                const char *tmp_vals[16];
                AstNode *al = args;
                while (al && !AST_IS_NIL(al) && tc < 16) {
                    tmp_vals[tc] = gen_expr(ctx, AST_CAR(al), depth + 1);
                    tc++;
                    al = AST_CDR(al);
                }

                /* Re-assign parameters */
                /* We need the parameter names. For now use %arg0, %arg1... */
                /* Actually, the params are stored in the symbol table */
                /* Just emit copy instructions for each arg */
                /* TODO: proper param name tracking */

                /* For now, just generate a regular call since proper
                 * tail call requires tracking param names */
                const char *result = qbe_gen_tmp(ctx);
                if (tc == 0) {
                    emit(ctx, "%s =l call $%s()", result, safe_fname);
                } else if (tc == 1) {
                    emit(ctx, "%s =l call $%s(l %s)", result, safe_fname, tmp_vals[0]);
                } else if (tc == 2) {
                    emit(ctx, "%s =l call $%s(l %s, l %s)", result, safe_fname, tmp_vals[0], tmp_vals[1]);
                } else {
                    emit_raw(ctx, "%s =l call $%s(l %s", result, safe_fname, tmp_vals[0]);
                    for (int i = 1; i < tc; i++) {
                        fprintf(ctx->out, ", l %s", tmp_vals[i]);
                    }
                    fprintf(ctx->out, ")\n");
                }
                return result;
            }
        }

        const char *result = qbe_gen_tmp(ctx);
        /* Collect arguments */
        AstNode *arg_list = args;
        int arg_count = 0;
        char arg_bufs[16][64];
        const char *arg_vals[16];

        while (arg_list && !AST_IS_NIL(arg_list) && arg_count < 16) {
            arg_vals[arg_count] = gen_expr(ctx, AST_CAR(arg_list), depth + 1);
            strncpy(arg_bufs[arg_count], arg_vals[arg_count], 63);
            arg_bufs[arg_count][63] = '\0';
            arg_list = AST_CDR(arg_list);
            arg_count++;
        }

        /* Generate call */
        {
            char safe_fname[128];
            qbe_sanitize_name(fname, safe_fname, sizeof(safe_fname));

            /* Check if fname is a local variable (function pointer) */
            Symbol *sym = symtab_lookup(ctx->symtab, fname);
            int is_local = (sym && sym->kind == SYM_LOCAL);

            if (is_local) {
                /* Call via closure or function pointer */
                /* Extract function pointer and env from closure object */
                const char *qbe_name = sym->as.local.qbe_name;

                /* Try to load function pointer and env from closure object */
                /* Closure layout: [func_ptr(8 bytes), env_ptr(8 bytes)] */
                const char *func_ptr = qbe_gen_tmp(ctx);
                const char *env_ptr = qbe_gen_tmp(ctx);
                emit(ctx, "%s =l loadl %s", func_ptr, qbe_name);
                const char *env_slot = qbe_gen_tmp(ctx);
                emit(ctx, "%s =l add %s, 8", env_slot, qbe_name);
                emit(ctx, "%s =l loadl %s", env_ptr, env_slot);

                /* Call with env as first argument */
                if (arg_count == 0) {
                    emit(ctx, "%s =l call %s(l %s)", result, func_ptr, env_ptr);
                } else if (arg_count == 1) {
                    emit(ctx, "%s =l call %s(l %s, l %s)", result, func_ptr, env_ptr, arg_bufs[0]);
                } else if (arg_count == 2) {
                    emit(ctx, "%s =l call %s(l %s, l %s, l %s)", result, func_ptr, env_ptr, arg_bufs[0], arg_bufs[1]);
                } else {
                    emit_raw(ctx, "%s =l call %s(l %s, l %s", result, func_ptr, env_ptr, arg_bufs[0]);
                    for (int i = 1; i < arg_count; i++) {
                        fprintf(ctx->out, ", l %s", arg_bufs[i]);
                    }
                    fprintf(ctx->out, ")\n");
                }
            } else {
                /* Call global function: %r =l call $f(...) */
                if (arg_count == 0) {
                    emit(ctx, "%s =l call $%s()", result, safe_fname);
                } else if (arg_count == 1) {
                    emit(ctx, "%s =l call $%s(l %s)", result, safe_fname, arg_bufs[0]);
                } else if (arg_count == 2) {
                    emit(ctx, "%s =l call $%s(l %s, l %s)", result, safe_fname, arg_bufs[0], arg_bufs[1]);
                } else {
                    emit_raw(ctx, "%s =l call $%s(l %s", result, safe_fname, arg_bufs[0]);
                    for (int i = 1; i < arg_count; i++) {
                        fprintf(ctx->out, ", l %s", arg_bufs[i]);
                    }
                    fprintf(ctx->out, ")\n");
                }
            }
        }
        return result;
    }

    case AST_DEFINE:
        /* Defines are handled by qbe_gen_program (top level) and
         * gen_body (internal). Reaching here means a misplaced define. */
        codegen_error("misplaced define: only valid at top level or body position");

    case AST_LAMBDA: {
        /* Lambda expression: (lambda (params...) body...)
         *
         * Closure conversion: lambda generates a function that takes
         * an env pointer as first arg, followed by regular params.
         * The env pointer points to captured free variables.
         *
         * At the call site, the env is loaded from env pointer + offset.
         */

        AstNode *params = AST_CAR(expr);
        AstNode *body = AST_CDR(expr);

        /* Generate a unique function name */
        static int lambda_count = 0;
        char lambda_name[64];
        snprintf(lambda_name, sizeof(lambda_name), "_lambda_%d", lambda_count++);

        /* Names defined inside this body are not captured as free vars
         * (stage0 restriction: internal defines cannot be closed over) */
        AstNode *excluded = params;
        {
            AstNode *b = body;
            while (b && !AST_IS_NIL(b)) {
                AstNode *e = AST_CAR(b);
                if (e && e->tag == AST_DEFINE && AST_CAR(e)->tag == AST_SYMBOL) {
                    AstNode *cell = ast_pair(AST_CAR(e), ast_nil());
                    cell->as.pair.cdr = excluded;
                    excluded = cell;
                }
                b = AST_CDR(b);
            }
        }

        /* Collect free variables from the body sequence, excluding lambda
         * parameters and internal define names */
        AstNode *free_vars = collect_free_vars_seq(body, ctx->symtab, excluded);

        /* Count free variables */
        int n_free = 0;
        {
            AstNode *fv = free_vars;
            while (fv && !AST_IS_NIL(fv)) { n_free++; fv = AST_CDR(fv); }
        }

        /* ---- Generate the lambda function into lambda buffer ---- */
        FILE *saved_out = ctx->out;
        ctx->out = ctx->lambda_out;

        /* Function header: env pointer + regular params */
        fprintf(ctx->out, "function l $%s(l %%env", lambda_name);

        /* Add regular parameters */
        AstNode *p = params;
        while (p && !AST_IS_NIL(p)) {
            AstNode *param = AST_CAR(p);
            fprintf(ctx->out, ", l %s", param_qbe_name(param));
            if (param->tag == AST_SYMBOL) {
                symtab_add_local(ctx->symtab, param->as.symbol);
            }
            p = AST_CDR(p);
        }
        fprintf(ctx->out, ") {\n@start\n");
        ctx->depth = 1;

        /* Load captured variables from env */
        {
            AstNode *fv = free_vars;
            int idx = 0;
            while (fv && !AST_IS_NIL(fv)) {
                AstNode *var = AST_CAR(fv);
                if (var->tag == AST_SYMBOL) {
                    Symbol *sym = symtab_add_local(ctx->symtab, var->as.symbol);
                    const char *tmp = qbe_gen_tmp(ctx);
                    /* Load from env + idx*8 */
                    if (idx == 0) {
                        emit(ctx, "%s =l loadl %%env", tmp);
                    } else {
                        const char *addr = qbe_gen_tmp(ctx);
                        emit(ctx, "%s =l add %%env, %d", addr, idx * 8);
                        emit(ctx, "%s =l loadl %s", tmp, addr);
                    }
                    emit(ctx, "%s =l copy %s", sym->as.local.qbe_name, tmp);
                }
                idx++;
                fv = AST_CDR(fv);
            }
        }

        /* Generate body (may contain internal defines and multiple exprs) */
        const char *result = gen_body(ctx, body, 1);
        emit(ctx, "ret %s", result ? result : "2");

        fprintf(ctx->out, "}\n\n");
        ctx->depth = 0;
        ctx->out = saved_out;

        /* ---- Create closure object at the call site ---- */
        /* Always create a closure object: [func_ptr, env_ptr] */
        {
            const char *env_ptr = qbe_gen_tmp(ctx);
            if (n_free > 0) {
                emit(ctx, "%s =l alloc8 %d", env_ptr, n_free * 8);

                /* Store captured values into env */
                AstNode *fv = free_vars;
                int idx = 0;
                while (fv && !AST_IS_NIL(fv)) {
                    AstNode *var = AST_CAR(fv);
                    if (var->tag == AST_SYMBOL) {
                        /* Look up the variable in current scope */
                        Symbol *sym = symtab_lookup(ctx->symtab, var->as.symbol);
                        if (sym && sym->kind == SYM_LOCAL) {
                            if (idx == 0) {
                                emit(ctx, "storel %s, %s", sym->as.local.qbe_name, env_ptr);
                            } else {
                                const char *addr = qbe_gen_tmp(ctx);
                                emit(ctx, "%s =l add %s, %d", addr, env_ptr, idx * 8);
                                emit(ctx, "storel %s, %s", sym->as.local.qbe_name, addr);
                            }
                        } else {
                            /* Free var not found — store 0 */
                            if (idx == 0) {
                                emit(ctx, "storel 0, %s", env_ptr);
                            } else {
                                const char *addr = qbe_gen_tmp(ctx);
                                emit(ctx, "%s =l add %s, %d", addr, env_ptr, idx * 8);
                                emit(ctx, "storel 0, %s", addr);
                            }
                        }
                    }
                    idx++;
                    fv = AST_CDR(fv);
                }
            } else {
                /* No free variables — store 0 as env */
                emit(ctx, "%s =l copy 0", env_ptr);
            }

            /* Create closure object: [func_ptr, env_ptr] */
            const char *closure = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l alloc8 16", closure);
            const char *func_ptr = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l copy $%s", func_ptr, lambda_name);
            emit(ctx, "storel %s, %s", func_ptr, closure);
            const char *env_slot = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l add %s, 8", env_slot, closure);
            emit(ctx, "storel %s, %s", env_ptr, env_slot);

            /* Return closure pointer */
            return closure;
        }
    }

    case AST_LET: {
        /* (let ((name val) ...) body) */
        AstNode *bindings = AST_CAR(expr);
        AstNode *body = AST_CDR(expr);

        /* Process bindings */
        while (bindings && !AST_IS_NIL(bindings)) {
            AstNode *binding = AST_CAR(bindings);
            AstNode *name = AST_CAR(binding);
            AstNode *val = AST_CAR(AST_CDR(binding));

            /* Generate value */
            const char *val_str = gen_expr(ctx, val, depth + 1);

            /* Store in symbol table */
            if (name->tag == AST_SYMBOL) {
                Symbol *sym = symtab_add_local(ctx->symtab, name->as.symbol);
                emit(ctx, "%s =l copy %s", sym->as.local.qbe_name, val_str);
            }

            bindings = AST_CDR(bindings);
        }

        /* Generate body (may contain internal defines and multiple exprs) */
        if (body && !AST_IS_NIL(body)) {
            return gen_body(ctx, body, depth + 1);
        }
        return "0";
    }

    case AST_BEGIN: {
        /* (begin exprs...) */
        AstNode *exprs = AST_CAR(expr);
        const char *result = "2";

        while (exprs && !AST_IS_NIL(exprs)) {
            result = gen_expr(ctx, AST_CAR(exprs), depth + 1);
            exprs = AST_CDR(exprs);
        }
        return result;
    }

    case AST_SET:
        codegen_error("set! is not supported in stage0 (bindings are immutable)");

    default:
        codegen_error("unsupported construct (AST tag=%d)", expr->tag);
        return "0";
    }
}

/* Collect free variables referenced in an expression */
/* Returns a list of symbols that are not defined locally */
/* exclude_params: list of parameter names to exclude */
static AstNode *collect_free_vars_impl(AstNode *expr, SymTab *symtab, AstNode *exclude_params)
{
    if (!expr) return ast_nil();

    switch (expr->tag) {
    case AST_SYMBOL: {
        /* Check if this symbol is in the exclude list (lambda's own params) */
        AstNode *p = exclude_params;
        while (p && !AST_IS_NIL(p)) {
            AstNode *param = AST_CAR(p);
            if (param->tag == AST_SYMBOL && strcmp(param->as.symbol, expr->as.symbol) == 0) {
                return ast_nil(); /* This is a parameter, not free */
            }
            p = AST_CDR(p);
        }
        /* Check if this is a known global function (defined at top level) */
        Symbol *sym = symtab_lookup(symtab, expr->as.symbol);
        if (sym && sym->kind == SYM_GLOBAL_FUNC) {
            return ast_nil(); /* Global function, not free */
        }
        /* Check for built-in operators and functions */
        if (is_builtin_name(expr->as.symbol)) {
            return ast_nil(); /* Built-in, not free */
        }
        /* Everything else is potentially a free variable */
        return ast_pair(expr, ast_nil());
    }
    case AST_PAIR: {
        /* Function application: (f arg1 arg2 ...) */
        AstNode *func = AST_CAR(expr);
        AstNode *args = AST_CDR(expr);

        /* Collect from function and arguments */
        AstNode *vars = collect_free_vars_impl(func, symtab, exclude_params);
        while (args && !AST_IS_NIL(args)) {
            AstNode *arg_vars = collect_free_vars_impl(AST_CAR(args), symtab, exclude_params);
            /* Append arg_vars to vars */
            if (AST_IS_NIL(vars)) {
                vars = arg_vars;
            } else {
                AstNode *tail = vars;
                while (!AST_IS_NIL(AST_CDR(tail))) {
                    tail = AST_CDR(tail);
                }
                AST_CDR(tail) = arg_vars;
            }
            args = AST_CDR(args);
        }
        return vars;
    }
    case AST_IF: {
        AstNode *test = AST_CAR(expr);
        AstNode *conseq = AST_CAR(AST_CDR(expr));
        AstNode *alt = AST_CAR(AST_CDR(AST_CDR(expr)));
        AstNode *vars = collect_free_vars_impl(test, symtab, exclude_params);
        /* Append conseq vars */
        AstNode *conseq_vars = collect_free_vars_impl(conseq, symtab, exclude_params);
        if (AST_IS_NIL(vars)) vars = conseq_vars;
        else {
            AstNode *tail = vars;
            while (!AST_IS_NIL(AST_CDR(tail))) tail = AST_CDR(tail);
            AST_CDR(tail) = conseq_vars;
        }
        /* Append alt vars */
        AstNode *alt_vars = collect_free_vars_impl(alt, symtab, exclude_params);
        if (AST_IS_NIL(vars)) vars = alt_vars;
        else {
            AstNode *tail = vars;
            while (!AST_IS_NIL(AST_CDR(tail))) tail = AST_CDR(tail);
            AST_CDR(tail) = alt_vars;
        }
        return vars;
    }
    case AST_LAMBDA: {
        /* For nested lambda, exclude its own parameters */
        AstNode *nested_params = AST_CAR(expr);
        AstNode *nested_body = AST_CDR(expr);
        return collect_free_vars_impl(nested_body, symtab, nested_params);
    }
    default:
        return ast_nil();
    }
}

/* Collect free variables across a body sequence (list of expressions).
 * Simply unions the free vars of each expression; the spine is a plain
 * list, NOT an application. */
static AstNode *collect_free_vars_seq(AstNode *exprs, SymTab *symtab, AstNode *exclude)
{
    AstNode *vars = ast_nil();
    while (exprs && !AST_IS_NIL(exprs)) {
        AstNode *item_vars = collect_free_vars_impl(exprs->as.pair.car, symtab, exclude);
        if (AST_IS_NIL(vars)) {
            vars = item_vars;
        } else if (!AST_IS_NIL(item_vars)) {
            AstNode *tail = vars;
            while (!AST_IS_NIL(AST_CDR(tail))) tail = AST_CDR(tail);
            AST_CDR(tail) = item_vars;
        }
        exprs = exprs->as.pair.cdr;
    }
    return vars;
}

/* Sanitize a name for use as a QBE function/variable name */
static void qbe_emit_name(QbeCtx *ctx, const char *name)
{
    char buf[128];
    qbe_sanitize_name(name, buf, sizeof(buf));
    fprintf(ctx->out, "%s", buf);
}

/* Generate a function definition */
static void gen_function_to(QbeCtx *ctx, const char *name, AstNode *params, AstNode *body, FILE *out)
{
    /* Set current function name for tail call detection */
    char saved_func[128];
    strncpy(saved_func, ctx->current_func, sizeof(saved_func));
    qbe_sanitize_name(name, ctx->current_func, sizeof(ctx->current_func));

    /* Generate function header */
    if (strcmp(name, "main") == 0) {
        fprintf(out, "export ");
    }
    /* Use 'l' (long/64-bit) for all values since we use tagged pointers */
    fprintf(out, "function l $");
    qbe_emit_name(ctx, name);
    if (strcmp(name, "main") == 0) {
        /* Unix C runtime passes (argc, argv) in the first two argument
         * registers; forward them to the runtime before anything runs. */
        fprintf(out, "(l %%npl_argc, l %%npl_argv) {\n");
        fprintf(out, "@start\n");
        fprintf(out, "    call $npl_store_args(l %%npl_argc, l %%npl_argv)\n");
        if (ctx->has_init)
            fprintf(out, "    call $npl_global_inits()\n");
        ctx->depth = 1;
        ctx->in_tail = 1;
        const char *result = gen_body(ctx, body, 1);
        emit(ctx, "ret %s", result ? result : "2");
        fprintf(out, "}\n\n");
        strncpy(ctx->current_func, saved_func, sizeof(ctx->current_func));
        return;
    }
    fprintf(out, "(");

    /* Generate parameters */
    AstNode *p = params;
    int first = 1;
    while (p && !AST_IS_NIL(p)) {
        AstNode *param = AST_CAR(p);
        if (!first) fprintf(out, ", ");
        fprintf(out, "l %s", param_qbe_name(param));
        /* Add to symbol table as local */
        if (param->tag == AST_SYMBOL) {
            symtab_add_local(ctx->symtab, param->as.symbol);
        }
        first = 0;
        p = AST_CDR(p);
    }
    fprintf(out, ") {\n");

    /* Generate body: sequence of expressions with optional internal defines */
    fprintf(out, "@start\n");
    ctx->depth = 1;
    ctx->in_tail = 1;  /* Body is in tail position */

    {
        const char *result = gen_body(ctx, body, 1);
        emit(ctx, "ret %s", result ? result : "2");
    }

    fprintf(out, "}\n\n");
    ctx->depth = 0;

    /* Restore previous function name */
    strncpy(ctx->current_func, saved_func, sizeof(ctx->current_func));
}

/* Wrapper for main function definitions */
static void gen_function(QbeCtx *ctx, const char *name, AstNode *params, AstNode *body)
{
    gen_function_to(ctx, name, params, body, ctx->out);
}

/* Check whether a top-level variable initializer is a static constant
 * supported by data-section initialization. */
static int is_static_const(AstNode *e)
{
    if (!e) return 0;
    switch (e->tag) {
    case AST_FIXNUM:
    case AST_BOOL:
    case AST_NIL:
    case AST_STRING:
        return 1;
    case AST_QUOTE:
        /* quoted symbol / string / number / (list ...) literal */
        return e->as.pair.car != NULL;
    default:
        return 0;
    }
}

/* Emit the data definition for a global variable slot.
 * Heap-object values are stored pre-tagged ("($label+1)") so loads
 * yield a valid NplVal directly. */
static void emit_global_var(QbeCtx *ctx, const char *name, AstNode *value)
{
    char safe_name[128];
    qbe_sanitize_name(name, safe_name, sizeof(safe_name));

    int boxed = !(value->tag == AST_FIXNUM ||
                  value->tag == AST_BOOL ||
                  value->tag == AST_NIL);
    const char *repr = emit_static_datum(ctx, value);
    if (boxed) {
        repr = tagged_data_ref(value, repr);
    }
    fprintf(ctx->data_out, "data $gv_%s = { l %s }\n", safe_name, repr);
}

/* Main entry point: generate QBE IL for entire program */
void qbe_gen_program(QbeCtx *ctx, AstNode *program)
{
    int has_main = 0;

    /* Pass 1: register ALL top-level definitions in the symbol table
     * (so cross-references and forward references resolve correctly),
     * and emit global variable data slots. */
    {
        AstNode *cur = program;
        while (cur && !AST_IS_NIL(cur)) {
            AstNode *expr = AST_CAR(cur);

            if (expr->tag == AST_DEFINE) {
                AstNode *name_or_sig = AST_CAR(expr);
                AstNode *body = AST_CDR(expr);

                if (name_or_sig && name_or_sig->tag == AST_SYMBOL) {
                    char safe_vn[128];
                    qbe_sanitize_name(name_or_sig->as.symbol, safe_vn,
                                      sizeof(safe_vn));
                    symtab_add_global_var(ctx->symtab, name_or_sig->as.symbol,
                                          body);
                    if (is_static_const(body)) {
                        emit_global_var(ctx, name_or_sig->as.symbol, body);
                    } else {
                        /* runtime-initialized global: writable slot filled by
                         * the synthesized $npl_global_inits function */
                        fprintf(ctx->data_out,
                                "data $gv_%s = { l 2 }\n", safe_vn);
                        if (!ctx->has_init) {
                            fprintf(ctx->init_out,
                                    "function l $npl_global_inits() {\n");
                            fprintf(ctx->init_out, "@start\n");
                            ctx->has_init = 1;
                        }
                        {   /* route generated code into the init body */
                            FILE *saved = ctx->out;
                            ctx->out = ctx->init_out;
                            ctx->depth = 1;
                            const char *vtmp = gen_expr(ctx, body, 1);
                            ctx->out = saved;
                            ctx->depth = 0;
                            fprintf(ctx->init_out, "    storel %s, $gv_%s\n",
                                    vtmp, safe_vn);
                        }
                    }
                } else if (name_or_sig && name_or_sig->tag == AST_PAIR) {
                    AstNode *name_node = AST_CAR(name_or_sig);
                    AstNode *params = AST_CDR(name_or_sig);
                    if (name_node->tag == AST_SYMBOL) {
                        symtab_add_global_func(ctx->symtab, name_node->as.symbol,
                                               params, body);
                        if (strcmp(name_node->as.symbol, "main") == 0) {
                            has_main = 1;
                        }
                    } else {
                        codegen_error("malformed top-level define");
                    }
                }
            }
            cur = AST_CDR(cur);
        }
    }

    /* Pass 2: generate function definitions */
    {
        AstNode *cur = program;
        while (cur && !AST_IS_NIL(cur)) {
            AstNode *expr = AST_CAR(cur);
            if (expr->tag == AST_DEFINE) {
                AstNode *name_or_sig = AST_CAR(expr);
                AstNode *body = AST_CDR(expr);
                if (name_or_sig->tag == AST_PAIR) {
                    AstNode *name_node = AST_CAR(name_or_sig);
                    AstNode *params = AST_CDR(name_or_sig);
                    gen_function(ctx, name_node->as.symbol, params, body);
                }
            }
            cur = AST_CDR(cur);
        }
    }

    if (ctx->has_init) {
        fprintf(ctx->init_out, "    ret 2\n}\n\n");
        /* every program entry must run initializers first: hook into both
         * the emitted user main and our fallback main via a preamble we
         * prepend right here. Simplest: define a wrapper the linker sees:
         *   real work happens because gen_function_to already ran for main.
         * To keep order correct we re-open: instead we make nplc programs
         * call the initializer from the entry; for stage0 simplicity the
         * initializer is invoked from $main prologue text appended now. */
    }

    /* Generate main if not defined */
    if (!has_main) {
        fprintf(ctx->out, "export function w $main() {\n");
        fprintf(ctx->out, "@start\n");
        if (ctx->has_init) fprintf(ctx->out, "    call $npl_global_inits()\n");
        fprintf(ctx->out, "    ret 0\n");
        fprintf(ctx->out, "}\n");
    }

    /* Flush static data section first, then lambda definitions.
     * The runtime entry stub calls npl_runtime_init before anything runs. */
    rewind(ctx->data_out);
    {
        char buf[4096];
        while (fgets(buf, sizeof(buf), ctx->data_out)) {
            fprintf(ctx->out, "%s", buf);
        }
    }
    fclose(ctx->data_out);

    if (ctx->has_init) {
        rewind(ctx->init_out);
        {
            char ib[4096];
            while (fgets(ib, sizeof(ib), ctx->init_out))
                fprintf(ctx->out, "%s", ib);
        }
    }
    fclose(ctx->init_out);

    rewind(ctx->lambda_out);
    {
        char buf[4096];
        while (fgets(buf, sizeof(buf), ctx->lambda_out)) {
            fprintf(ctx->out, "%s", buf);
        }
    }
    fclose(ctx->lambda_out);
}
