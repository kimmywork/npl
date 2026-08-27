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

typedef struct {
    const char *value;
    Type *type;
} TypedValue;

/* Forward declarations */
static void gen_function(QbeCtx *ctx, const char *name, AstNode *params, AstNode *body);
static void gen_function_to(QbeCtx *ctx, const char *name, AstNode *params, AstNode *body, FILE *out);
static const char *gen_expr(QbeCtx *ctx, AstNode *expr, int depth);
static AstNode *collect_free_vars_impl(AstNode *expr, SymTab *symtab, AstNode *exclude_params);
static AstNode *collect_free_vars_seq(AstNode *exprs, SymTab *symtab, AstNode *exclude);
static TypedValue gen_typed_lambda(QbeCtx *ctx, AstNode *expr, TypeEnv *env);
static int unboxed_lambda(TypedLambda *lambda);

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
    ctx->types = NULL;
    ctx->next_tmp = 0;
    ctx->next_label = 0;
    ctx->depth = 0;
    ctx->in_tail = 0;
    ctx->expr_terminated = 0;
    ctx->trampoline_body = 0;
    ctx->current_typed = NULL;
    ctx->current_typed_params = NULL;
    ctx->current_func[0] = '\0';
    ctx->current_params = NULL;
    ctx->if_slot_base[0] = '\0';
    ctx->next_if_slot = 0;
}

void qbe_ctx_set_types(QbeCtx *ctx, TypeProgram *types)
{
    ctx->types = types;
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
            strcmp(name, "call/cc") == 0 ||
            strcmp(name, "call-with-current-continuation") == 0 ||
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

/* Forward declarations for position-aware expression generation. */
static const char *gen_expr(QbeCtx *ctx, AstNode *expr, int depth);
static const char *gen_body(QbeCtx *ctx, AstNode *body, int depth);

static const char *gen_expr_at(QbeCtx *ctx, AstNode *expr, int depth, int tail)
{
    int saved_tail = ctx->in_tail;
    ctx->in_tail = tail;
    const char *result = gen_expr(ctx, expr, depth);
    ctx->in_tail = saved_tail;
    return result;
}

static const char *gen_body_at(QbeCtx *ctx, AstNode *body, int depth, int tail)
{
    int saved_tail = ctx->in_tail;
    ctx->in_tail = tail;
    const char *result = gen_body(ctx, body, depth);
    ctx->in_tail = saved_tail;
    return result;
}

static const char *gen_closure_call(QbeCtx *ctx, const char *closure,
                                    AstNode *args, int depth, int tail)
{
    int argc = 0;
    char values[16][64];
    AstNode *arg = args;
    while (arg && !AST_IS_NIL(arg)) {
        if (argc >= 16) codegen_error("closure call supports at most 16 arguments");
        const char *value = gen_expr_at(ctx, AST_CAR(arg), depth + 1, 0);
        snprintf(values[argc], sizeof(values[argc]), "%s", value);
        argc++;
        arg = AST_CDR(arg);
    }

    const char *argv = "0";
    if (argc > 0) {
        argv = qbe_gen_tmp(ctx);
        emit(ctx, "%s =l alloc8 %d", argv, argc * 8);
        for (int i = 0; i < argc; i++) {
            const char *slot = argv;
            if (i > 0) {
                slot = qbe_gen_tmp(ctx);
                emit(ctx, "%s =l add %s, %d", slot, argv, i * 8);
            }
            emit(ctx, "storel %s, %s", values[i], slot);
        }
    }

    if (tail && ctx->trampoline_body) {
        emit(ctx, "call $npl_tail_request_closure(l %s, l %d, l %s)",
             closure, argc, argv);
        emit(ctx, "ret 2");
        ctx->expr_terminated = 1;
        return "2";
    }

    const char *result = qbe_gen_tmp(ctx);
    emit(ctx, "%s =l call $npl_apply(l %s, l %d, l %s)",
         result, closure, argc, argv);
    return result;
}

/* Nested lambdas own their slots and are emitted as separate functions. */
static int count_if_nodes(AstNode *node)
{
    if (!node || AST_IS_NIL(node)) return 0;
    if (node->tag == AST_LAMBDA) return 0;
    if (!AST_IS_PAIR(node)) return 0;
    return (node->tag == AST_IF ? 1 : 0) +
           count_if_nodes(AST_CAR(node)) + count_if_nodes(AST_CDR(node));
}

static void prepare_if_slots(QbeCtx *ctx, AstNode *body, FILE *out)
{
    int count = count_if_nodes(body);
    ctx->next_if_slot = 0;
    ctx->if_slot_base[0] = '\0';
    if (count > 0) {
        const char *base = qbe_gen_tmp(ctx);
        snprintf(ctx->if_slot_base, sizeof(ctx->if_slot_base), "%s", base);
        fprintf(out, "    %s =l alloc8 %d\n", ctx->if_slot_base, count * 8);
    }
}

static void next_if_slot(QbeCtx *ctx, char *slot, size_t slot_size)
{
    if (ctx->if_slot_base[0] == '\0') {
        const char *tmp = qbe_gen_tmp(ctx);
        snprintf(slot, slot_size, "%s", tmp);
        emit(ctx, "%s =l alloc8 8", slot);
        return;
    }

    int index = ctx->next_if_slot++;
    if (index == 0) {
        snprintf(slot, slot_size, "%s", ctx->if_slot_base);
    } else {
        const char *tmp = qbe_gen_tmp(ctx);
        snprintf(slot, slot_size, "%s", tmp);
        emit(ctx, "%s =l add %s, %d", slot, ctx->if_slot_base, index * 8);
    }
}

static Symbol *lookup_global_function(QbeCtx *ctx, const char *name)
{
    const char *interned = sym_intern(name);
    for (int i = ctx->symtab->count - 1; i >= 0; i--) {
        Symbol *symbol = &ctx->symtab->entries[i];
        if (symbol->kind == SYM_GLOBAL_FUNC && symbol->name == interned)
            return symbol;
    }
    return NULL;
}

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
    int body_is_tail = ctx->in_tail;
    int has_value = 0;
    ctx->expr_terminated = 0;

    while (body && !AST_IS_NIL(body)) {
        AstNode *e = AST_CAR(body);
        int is_last = AST_IS_NIL(AST_CDR(body));

        if (e && e->tag == AST_DEFINE) {
            /* Internal define: bind name first, then evaluate value
             * into its slot (letrec* ordering). */
            AstNode *name_or_sig = AST_CAR(e);
            AstNode *value = AST_CDR(e);

            if (name_or_sig->tag == AST_PAIR) {
                /* (define (f args...) body...) inside a body is not
                 * supported at stage0; use (define f (lambda ...)). */
                codegen_error("internal function shorthand in '%s' is only "
                              "valid at top level", ctx->current_func);
            }
            if (!name_or_sig || name_or_sig->tag != AST_SYMBOL) {
                codegen_error("malformed internal define");
            }
            Symbol *sym = symtab_add_local(ctx->symtab, name_or_sig->as.symbol);
            const char *val = gen_expr_at(ctx, value, depth, 0);
            emit(ctx, "%s =l copy %s", sym->as.local.qbe_name, val);
        } else {
            last_val = gen_expr_at(ctx, e, depth, body_is_tail && is_last);
            has_value = 1;
            if (ctx->expr_terminated) return last_val;
        }
        body = AST_CDR(body);
    }

    ctx->expr_terminated = 0;
    return has_value ? last_val : "0";
}

/* Generate QBE IL for an expression */
static const char *gen_expr(QbeCtx *ctx, AstNode *expr, int depth)
{
    int expr_is_tail = ctx->in_tail;
    ctx->in_tail = 0;  /* children are non-tail unless explicitly propagated */
    ctx->expr_terminated = 0;
    if (!expr) return "2";

    switch (expr->tag) {
    case AST_FIXNUM:
    case AST_BOOL:
    case AST_NIL:
        return gen_literal(ctx, expr);

    case AST_FLONUM: {
        const char *result = qbe_gen_tmp(ctx);
        char literal[64];
        snprintf(literal, sizeof(literal), "d_%a", expr->as.flonum);
        emit(ctx, "%s =l call $npl_float(d %s)", result, literal);
        return result;
    }

    case AST_SYMBOL: {
        /* Look up in symbol table */
        Symbol *sym = symtab_lookup(ctx->symtab, expr->as.symbol);
        if (sym && sym->kind == SYM_LOCAL) {
            return sym->as.local.qbe_name;
        }
        if (sym && sym->kind == SYM_GLOBAL_FUNC) {
            char safe_name[128];
            qbe_sanitize_name(expr->as.symbol, safe_name, sizeof(safe_name));
            int arity = 0;
            for (AstNode *p = sym->as.func.params;
                 p && !AST_IS_NIL(p); p = AST_CDR(p)) {
                arity++;
            }
            const char *closure = qbe_gen_tmp(ctx);
            emit(ctx,
                 "%s =l call $npl_make_closure(l $%s__body, l 2, l %d)",
                 closure, safe_name, arity);
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
        AstNode *test   = AST_CAR(expr);
        AstNode *conseq = AST_CAR(AST_CDR(expr));
        AstNode *alt    = AST_CAR(AST_CDR(AST_CDR(expr)));

        /* Non-terminating branches pass their value through a stack slot.
         * A self-tail branch ends in jmp @tail and emits no merge write. */
        char slot_buf[32];
        next_if_slot(ctx, slot_buf, sizeof(slot_buf));
        const char *slot = slot_buf;

        const char *ltrue  = qbe_gen_label(ctx);
        const char *lfalse = qbe_gen_label(ctx);
        const char *ldone  = qbe_gen_label(ctx);

        const char *test_val = gen_expr_at(ctx, test, depth + 1, 0);
        const char *truthy = qbe_gen_tmp(ctx);
        emit(ctx, "%s =w cnel %s, %d", truthy, test_val, NPL_IMM_FALSE);
        emit(ctx, "jnz %s, %s, %s", truthy, ltrue, lfalse);

        fprintf(ctx->out, "%s\n", ltrue);
        const char *true_val = gen_expr_at(ctx, conseq, depth + 1, expr_is_tail);
        int true_terminated = ctx->expr_terminated;
        if (!true_terminated) {
            emit(ctx, "storel %s, %s", true_val, slot);
            emit(ctx, "jmp %s", ldone);
        }

        fprintf(ctx->out, "%s\n", lfalse);
        const char *false_val = gen_expr_at(ctx, alt, depth + 1, expr_is_tail);
        int false_terminated = ctx->expr_terminated;
        if (!false_terminated) {
            emit(ctx, "storel %s, %s", false_val, slot);
            emit(ctx, "jmp %s", ldone);
        }

        if (true_terminated && false_terminated) {
            ctx->expr_terminated = 1;
            return "2";
        }

        fprintf(ctx->out, "%s\n", ldone);
        const char *result = qbe_gen_tmp(ctx);
        emit(ctx, "%s =l loadl %s", result, slot);
        ctx->expr_terminated = 0;
        return result;
    }

    case AST_PAIR: {
        /* Function application: (f arg1 arg2 ...) */
        AstNode *func = AST_CAR(expr);
        AstNode *args = AST_CDR(expr);

        if (func->tag != AST_SYMBOL) {
            const char *func_val = gen_expr_at(ctx, func, depth + 1, 0);
            if (!func_val || strcmp(func_val, "0") == 0) return "0";
            return gen_closure_call(ctx, func_val, args, depth,
                                    expr_is_tail);
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
        if (strcmp(fname, "call/cc") == 0 ||
            strcmp(fname, "call-with-current-continuation") == 0) {
            if (!args || AST_IS_NIL(args) ||
                !AST_IS_NIL(AST_CDR(args)))
                codegen_error("%s expects exactly one argument", fname);
            const char *procedure = gen_expr_at(
                ctx, AST_CAR(args), depth + 1, 0);
            const char *result = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l call $npl_call_cc(l %s)", result, procedure);
            return result;
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

        Symbol *callee = symtab_lookup(ctx->symtab, fname);
        if (callee && callee->kind == SYM_LOCAL)
            return gen_closure_call(ctx, callee->as.local.qbe_name, args,
                                    depth, expr_is_tail);

        /* A direct self-call in tail position becomes parameter rebinding
         * followed by a jump to the function's loop header. */
        {
            char safe_fname[128];
            qbe_sanitize_name(fname, safe_fname, sizeof(safe_fname));
            if (expr_is_tail && ctx->current_params &&
                strcmp(safe_fname, ctx->current_func) == 0) {
                int argc = 0;
                char frozen[16][64];
                AstNode *al = args;
                while (al && !AST_IS_NIL(al)) {
                    if (argc >= 16)
                        codegen_error("self-tail call supports at most 16 arguments");
                    const char *value = gen_expr_at(ctx, AST_CAR(al), depth + 1, 0);
                    const char *tmp = qbe_gen_tmp(ctx);
                    emit(ctx, "%s =l copy %s", tmp, value);
                    snprintf(frozen[argc], sizeof(frozen[argc]), "%s", tmp);
                    argc++;
                    al = AST_CDR(al);
                }

                int paramc = 0;
                AstNode *p = ctx->current_params;
                while (p && !AST_IS_NIL(p)) {
                    if (paramc >= argc)
                        codegen_error("too few arguments in self-tail call to '%s'", fname);
                    AstNode *param = AST_CAR(p);
                    if (!param || param->tag != AST_SYMBOL)
                        codegen_error("malformed parameter in '%s'", fname);
                    emit(ctx, "%s =l copy %s", param_qbe_name(param), frozen[paramc]);
                    paramc++;
                    p = AST_CDR(p);
                }
                if (paramc != argc)
                    codegen_error("too many arguments in self-tail call to '%s'", fname);

                emit(ctx, "jmp @tail");
                ctx->expr_terminated = 1;
                return "2";
            }
        }

        /* Any other direct tail call transfers to the generic dispatcher.
         * npl_tail_request copies argv synchronously before this frame returns. */
        Symbol *tail_target = lookup_global_function(ctx, fname);
        if (expr_is_tail && ctx->trampoline_body && tail_target) {
            int argc = 0;
            char values[16][64];
            AstNode *al = args;
            while (al && !AST_IS_NIL(al)) {
                if (argc >= 16)
                    codegen_error("tail call supports at most 16 arguments");
                const char *value = gen_expr_at(ctx, AST_CAR(al), depth + 1, 0);
                snprintf(values[argc], sizeof(values[argc]), "%s", value);
                argc++;
                al = AST_CDR(al);
            }

            int expected = 0;
            for (AstNode *p = tail_target->as.func.params;
                 p && !AST_IS_NIL(p); p = AST_CDR(p)) {
                expected++;
            }
            if (argc != expected)
                codegen_error("tail call to '%s' expects %d arguments, got %d",
                              fname, expected, argc);

            const char *argv = "0";
            if (argc > 0) {
                argv = qbe_gen_tmp(ctx);
                emit(ctx, "%s =l alloc8 %d", argv, argc * 8);
                for (int i = 0; i < argc; i++) {
                    const char *slot = argv;
                    if (i > 0) {
                        slot = qbe_gen_tmp(ctx);
                        emit(ctx, "%s =l add %s, %d", slot, argv, i * 8);
                    }
                    emit(ctx, "storel %s, %s", values[i], slot);
                }
            }

            char safe_fname[128];
            qbe_sanitize_name(fname, safe_fname, sizeof(safe_fname));
            emit(ctx,
                 "call $npl_tail_request(l $%s__body, l 2, l %d, l %s)",
                 safe_fname, argc, argv);
            emit(ctx, "ret 2");
            ctx->expr_terminated = 1;
            return "2";
        }

        const char *result = qbe_gen_tmp(ctx);
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
        TypedLambda *typed_lambda = ctx->types
            ? type_program_lambda(ctx->types, expr) : NULL;
        if (unboxed_lambda(typed_lambda)) {
            TypeEnv typed_env;
            type_env_init(&typed_env);
            return gen_typed_lambda(ctx, expr, &typed_env).value;
        }
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
        if (n_free > 16)
            codegen_error("lambda captures at most 16 variables");

        /* ---- Generate the lambda function into lambda buffer ---- */
        FILE *saved_out = ctx->out;
        FILE *definition_out = tmpfile();
        if (!definition_out) codegen_error("cannot create lambda buffer");
        int saved_depth = ctx->depth;
        char saved_func[128];
        char saved_if_slot_base[32];
        int saved_next_if_slot = ctx->next_if_slot;
        int saved_trampoline_body = ctx->trampoline_body;
        int saved_symtab_count = ctx->symtab->count;
        int saved_next_local = ctx->symtab->next_local;
        AstNode *saved_params = ctx->current_params;
        snprintf(saved_func, sizeof(saved_func), "%s", ctx->current_func);
        snprintf(saved_if_slot_base, sizeof(saved_if_slot_base), "%s",
                 ctx->if_slot_base);
        ctx->out = definition_out;

        /* Lambdas use the same generic (env, argc, argv) ABI as the
         * cross-function dispatcher. */
        fprintf(ctx->out,
                "function l $%s(l %%npl_env, l %%npl_argc, l %%npl_argv) {\n",
                lambda_name);
        fprintf(ctx->out, "@start\n");
        ctx->depth = 1;
        ctx->trampoline_body = 1;
        prepare_if_slots(ctx, body, ctx->out);

        AstNode *p = params;
        int param_index = 0;
        while (p && !AST_IS_NIL(p)) {
            AstNode *param = AST_CAR(p);
            if (!param || param->tag != AST_SYMBOL)
                codegen_error("malformed lambda parameter");
            symtab_add_local(ctx->symtab, param->as.symbol);
            const char *address = "%npl_argv";
            if (param_index > 0) {
                address = qbe_gen_tmp(ctx);
                emit(ctx, "%s =l add %%npl_argv, %d",
                     address, param_index * 8);
            }
            emit(ctx, "%s =l loadl %s", param_qbe_name(param), address);
            param_index++;
            p = AST_CDR(p);
        }

        /* Load captured variables from the managed environment vector. */
        {
            AstNode *fv = free_vars;
            int idx = 0;
            while (fv && !AST_IS_NIL(fv)) {
                AstNode *var = AST_CAR(fv);
                if (var->tag == AST_SYMBOL) {
                    Symbol *sym = symtab_add_local(ctx->symtab, var->as.symbol);
                    const char *tmp = qbe_gen_tmp(ctx);
                    emit(ctx,
                         "%s =l call $npl_environment_ref(l %%npl_env, l %d)",
                         tmp, idx);
                    emit(ctx, "%s =l copy %s", sym->as.local.qbe_name, tmp);
                }
                idx++;
                fv = AST_CDR(fv);
            }
        }

        /* Captured values are loaded once; parameter rebindings loop here. */
        emit(ctx, "jmp @tail");
        fprintf(ctx->out, "@tail\n");
        snprintf(ctx->current_func, sizeof(ctx->current_func), "%s", lambda_name);
        ctx->current_params = params;

        const char *result = gen_body_at(ctx, body, 1, 1);
        if (!ctx->expr_terminated)
            emit(ctx, "ret %s", result ? result : "2");

        fprintf(ctx->out, "}\n\n");
        fflush(definition_out);
        rewind(definition_out);
        {
            char line[4096];
            while (fgets(line, sizeof(line), definition_out))
                fputs(line, ctx->lambda_out);
        }
        fclose(definition_out);
        ctx->out = saved_out;
        ctx->symtab->count = saved_symtab_count;
        ctx->symtab->next_local = saved_next_local;
        ctx->depth = saved_depth;
        snprintf(ctx->current_func, sizeof(ctx->current_func), "%s", saved_func);
        ctx->current_params = saved_params;
        snprintf(ctx->if_slot_base, sizeof(ctx->if_slot_base), "%s",
                 saved_if_slot_base);
        ctx->next_if_slot = saved_next_if_slot;
        ctx->trampoline_body = saved_trampoline_body;
        ctx->expr_terminated = 0;

        /* Build a managed environment and closure at the call site. */
        {
            const char *captures = "0";
            if (n_free > 0) {
                captures = qbe_gen_tmp(ctx);
                emit(ctx, "%s =l alloc8 %d", captures, n_free * 8);
                AstNode *fv = free_vars;
                int idx = 0;
                while (fv && !AST_IS_NIL(fv)) {
                    AstNode *var = AST_CAR(fv);
                    const char *value = "2";
                    if (var->tag == AST_SYMBOL) {
                        Symbol *sym = symtab_lookup(ctx->symtab, var->as.symbol);
                        if (sym && sym->kind == SYM_LOCAL)
                            value = sym->as.local.qbe_name;
                    }
                    const char *slot = captures;
                    if (idx > 0) {
                        slot = qbe_gen_tmp(ctx);
                        emit(ctx, "%s =l add %s, %d", slot, captures, idx * 8);
                    }
                    emit(ctx, "storel %s, %s", value, slot);
                    idx++;
                    fv = AST_CDR(fv);
                }
            }

            const char *environment = qbe_gen_tmp(ctx);
            emit(ctx,
                 "%s =l call $npl_make_environment(l %d, l %s)",
                 environment, n_free, captures);
            const char *closure = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l call $npl_make_closure(l $%s, l %s, l %d)",
                 closure, lambda_name, environment, param_index);
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
            const char *val_str = gen_expr_at(ctx, val, depth + 1, 0);

            /* Store in symbol table */
            if (name->tag == AST_SYMBOL) {
                Symbol *sym = symtab_add_local(ctx->symtab, name->as.symbol);
                emit(ctx, "%s =l copy %s", sym->as.local.qbe_name, val_str);
            }

            bindings = AST_CDR(bindings);
        }

        /* Generate body (may contain internal defines and multiple exprs) */
        if (body && !AST_IS_NIL(body)) {
            return gen_body_at(ctx, body, depth + 1, expr_is_tail);
        }
        return "0";
    }

    case AST_BEGIN: {
        /* (begin exprs...): only its final expression inherits tail position. */
        return gen_body_at(ctx, AST_CAR(expr), depth + 1, expr_is_tail);
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
        /* A nested lambda can reference bindings from every enclosing lambda.
         * Exclude all enclosing and nested parameters when determining what
         * the enclosing closure itself must capture. */
        AstNode *combined = exclude_params;
        AstNode *nested_params = AST_CAR(expr);
        for (AstNode *p = nested_params; p && !AST_IS_NIL(p); p = AST_CDR(p))
            combined = ast_pair(AST_CAR(p), combined);
        return collect_free_vars_seq(AST_CDR(expr), symtab, combined);
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

static int unboxed_type(Type *type)
{
    type = type_resolve(type);
    return type && (type->kind == TYPE_INTEGER || type->kind == TYPE_BOOL ||
                    type->kind == TYPE_FLOAT || type->kind == TYPE_FUNC ||
                    type->kind == TYPE_PAIR);
}

static const char *typed_qbe_type(Type *type)
{
    type = type_resolve(type);
    if (type && type->kind == TYPE_FLOAT) return "d";
    if (type && (type->kind == TYPE_FUNC || type->kind == TYPE_PAIR ||
                 type->kind == TYPE_STRING)) return "l";
    return "w";
}

static const char *typed_guard(Type *type)
{
    type = type_resolve(type);
    if (type && type->kind == TYPE_BOOL) return "npl_guard_bool";
    if (type && type->kind == TYPE_FLOAT) return "npl_guard_float";
    if (type && type->kind == TYPE_PAIR) return "npl_guard_pair";
    if (type && type->kind == TYPE_FUNC) return "npl_guard_closure";
    return "npl_guard_integer";
}

static const char *typed_box(Type *type)
{
    type = type_resolve(type);
    if (type && type->kind == TYPE_BOOL) return "npl_box_bool";
    if (type && type->kind == TYPE_FLOAT) return "npl_box_float";
    if (type && (type->kind == TYPE_PAIR || type->kind == TYPE_FUNC))
        return "npl_box_identity";
    return "npl_box_integer";
}

static const char *typed_store(Type *type)
{
    type = type_resolve(type);
    if (type && type->kind == TYPE_FLOAT) return "stored";
    if (type && (type->kind == TYPE_PAIR || type->kind == TYPE_FUNC))
        return "storel";
    return "storew";
}

static const char *typed_load(Type *type)
{
    type = type_resolve(type);
    if (type && type->kind == TYPE_FLOAT) return "loadd";
    if (type && (type->kind == TYPE_PAIR || type->kind == TYPE_FUNC))
        return "loadl";
    return type && type->kind == TYPE_BOOL ? "loaduw" : "loadsw";
}

static const char *typed_cmp_op(const char *name, Type *type)
{
    type = type_resolve(type);
    if (!type || type->kind != TYPE_FLOAT) return qbe_cmp_op(name);
    if (strcmp(name, "<") == 0) return "cltd";
    if (strcmp(name, ">") == 0) return "cgtd";
    if (strcmp(name, "=") == 0) return "ceqd";
    if (strcmp(name, "<=") == 0) return "cled";
    if (strcmp(name, ">=") == 0) return "cged";
    if (strcmp(name, "not=") == 0) return "cned";
    return NULL;
}

static int unboxed_function(TypedFunction *function)
{
    if (!function || !function->result_annotated ||
        !unboxed_type(function->result)) return 0;
    for (int i = 0; i < function->arity; i++) {
        if (!function->param_annotated[i] ||
            !unboxed_type(function->params[i])) return 0;
    }
    return 1;
}

static int unboxed_lambda(TypedLambda *lambda)
{
    if (!lambda) return 0;
    int explicitly_typed = lambda->result_annotated;
    for (int i = 0; i < lambda->arity; i++)
        if (lambda->param_annotated[i]) explicitly_typed = 1;
    if (!lambda->in_typed_context && !explicitly_typed) return 0;

    lambda->result = type_resolve(lambda->result);
    if (!lambda->result ||
        (lambda->result->kind != TYPE_INTEGER &&
         lambda->result->kind != TYPE_BOOL)) return 0;
    for (int i = 0; i < lambda->arity; i++) {
        lambda->params[i] = type_resolve(lambda->params[i]);
        if (!lambda->params[i] ||
            (lambda->params[i]->kind != TYPE_INTEGER &&
             lambda->params[i]->kind != TYPE_BOOL)) return 0;
    }
    return 1;
}

static Type *typed_lambda_function_type(TypedLambda *lambda)
{
    Type *type = lambda->result;
    for (int i = lambda->arity - 1; i >= 0; i--)
        type = type_func(lambda->params[i], type);
    return type;
}

static TypedValue gen_typed_expr_at(QbeCtx *ctx, AstNode *expr,
                                    TypeEnv *env, int tail,
                                    int *terminated);

static TypedValue gen_typed_expr(QbeCtx *ctx, AstNode *expr, TypeEnv *env)
{
    int terminated = 0;
    return gen_typed_expr_at(ctx, expr, env, 0, &terminated);
}

static TypedValue typed_value(const char *value, Type *type)
{
    TypedValue result = { value, type };
    return result;
}

static TypedValue gen_typed_closure_call(QbeCtx *ctx, const char *closure,
                                         Type *function_type, AstNode *args,
                                         TypeEnv *env, int tail,
                                         int *terminated)
{
    char boxed[16][64];
    int argc = 0;
    Type *remaining = type_resolve(function_type);
    for (AstNode *arg = args; arg && !AST_IS_NIL(arg); arg = AST_CDR(arg)) {
        remaining = type_resolve(remaining);
        if (!remaining || remaining->kind != TYPE_FUNC || argc >= 16)
            codegen_error("typed closure received too many arguments");
        TypedValue value = gen_typed_expr(ctx, AST_CAR(arg), env);
        if (!type_unify(value.type, remaining->as.func.param))
            codegen_error("typed closure argument %d has wrong type", argc + 1);
        const char *boxed_value = qbe_gen_tmp(ctx);
        const char *box = typed_box(value.type);
        emit(ctx, "%s =l call $%s(%s %s)", boxed_value, box,
             typed_qbe_type(value.type), value.value);
        snprintf(boxed[argc], sizeof(boxed[argc]), "%s", boxed_value);
        argc++;
        remaining = remaining->as.func.result;
    }
    remaining = type_resolve(remaining);
    if (!remaining || remaining->kind == TYPE_FUNC)
        codegen_error("partial typed closure application is not supported");
    if (!unboxed_type(remaining))
        codegen_error("typed closure result is not unboxable");

    const char *argv = "0";
    if (argc > 0) {
        argv = qbe_gen_tmp(ctx);
        emit(ctx, "%s =l alloc8 %d", argv, argc * 8);
        for (int i = 0; i < argc; i++) {
            const char *slot = argv;
            if (i > 0) {
                slot = qbe_gen_tmp(ctx);
                emit(ctx, "%s =l add %s, %d", slot, argv, i * 8);
            }
            emit(ctx, "storel %s, %s", boxed[i], slot);
        }
    }
    if (tail) {
        emit(ctx, "call $npl_tail_request_closure(l %s, l %d, l %s)",
             closure, argc, argv);
        emit(ctx, "ret %s", remaining->kind == TYPE_FLOAT ? "d_0" : "0");
        *terminated = 1;
        return typed_value("0", remaining);
    }

    const char *tagged = qbe_gen_tmp(ctx);
    emit(ctx, "%s =l call $npl_apply(l %s, l %d, l %s)",
         tagged, closure, argc, argv);
    const char *result = qbe_gen_tmp(ctx);
    const char *guard = typed_guard(remaining);
    emit(ctx, "%s =%s call $%s(l %s)", result,
         typed_qbe_type(remaining), guard, tagged);
    return typed_value(result, remaining);
}

static TypedValue gen_typed_call(QbeCtx *ctx, AstNode *expr, TypeEnv *env,
                                 int tail, int *terminated)
{
    AstNode *head = AST_CAR(expr);
    AstNode *args = AST_CDR(expr);
    if (!head) codegen_error("missing typed call head");
    if (head->tag != AST_SYMBOL) {
        TypedValue closure = gen_typed_expr(ctx, head, env);
        if (!closure.type || type_resolve(closure.type)->kind != TYPE_FUNC)
            codegen_error("typed call head is not a function");
        return gen_typed_closure_call(ctx, closure.value, closure.type, args,
                                      env, tail, terminated);
    }
    const char *name = head->as.symbol;
    Type *local_type = type_env_lookup(env, name);
    if (local_type && type_resolve(local_type)->kind == TYPE_FUNC) {
        Symbol *symbol = symtab_lookup(ctx->symtab, name);
        if (!symbol || symbol->kind != SYM_LOCAL)
            codegen_error("missing typed closure binding '%s'", name);
        return gen_typed_closure_call(ctx, symbol->as.local.qbe_name,
                                      local_type, args, env, tail,
                                      terminated);
    }

    if (is_arith_op(name)) {
        if (!args || AST_IS_NIL(args) || !AST_CDR(args) ||
            AST_IS_NIL(AST_CDR(args)) ||
            !AST_IS_NIL(AST_CDR(AST_CDR(args))))
            codegen_error("typed arithmetic '%s' expects two arguments", name);
        TypedValue a = gen_typed_expr(ctx, AST_CAR(args), env);
        TypedValue b = gen_typed_expr(ctx, AST_CAR(AST_CDR(args)), env);
        Type *a_type = type_resolve(a.type);
        Type *b_type = type_resolve(b.type);
        if (!a_type || !b_type || !type_equal(a_type, b_type) ||
            (a_type->kind != TYPE_INTEGER && a_type->kind != TYPE_FLOAT))
            codegen_error("typed arithmetic '%s' requires matching numeric operands", name);
        const char *result = qbe_gen_tmp(ctx);
        emit(ctx, "%s =%s %s %s, %s", result, typed_qbe_type(a_type),
             qbe_arith_op(name), a.value, b.value);
        return typed_value(result, a_type);
    }

    if (is_cmp_op(name)) {
        if (!args || AST_IS_NIL(args) || !AST_CDR(args) ||
            AST_IS_NIL(AST_CDR(args)) ||
            !AST_IS_NIL(AST_CDR(AST_CDR(args))))
            codegen_error("typed comparison '%s' expects two arguments", name);
        TypedValue a = gen_typed_expr(ctx, AST_CAR(args), env);
        TypedValue b = gen_typed_expr(ctx, AST_CAR(AST_CDR(args)), env);
        Type *a_type = type_resolve(a.type);
        Type *b_type = type_resolve(b.type);
        if (!a_type || !b_type || !type_equal(a_type, b_type) ||
            (a_type->kind != TYPE_INTEGER && a_type->kind != TYPE_FLOAT))
            codegen_error("typed comparison '%s' requires matching numeric operands", name);
        const char *result = qbe_gen_tmp(ctx);
        emit(ctx, "%s =w %s %s, %s", result, typed_cmp_op(name, a_type),
             a.value, b.value);
        return typed_value(result, type_bool());
    }

    if (strcmp(name, "cons") == 0) {
        if (!args || AST_IS_NIL(args) || !AST_CDR(args) ||
            AST_IS_NIL(AST_CDR(args)) ||
            !AST_IS_NIL(AST_CDR(AST_CDR(args))))
            codegen_error("typed cons expects two arguments");
        TypedValue car = gen_typed_expr(ctx, AST_CAR(args), env);
        TypedValue cdr = gen_typed_expr(ctx, AST_CAR(AST_CDR(args)), env);
        if (!unboxed_type(car.type) || !unboxed_type(cdr.type))
            codegen_error("typed cons elements must have concrete types");
        const char *boxed_car = qbe_gen_tmp(ctx);
        const char *boxed_cdr = qbe_gen_tmp(ctx);
        emit(ctx, "%s =l call $%s(%s %s)", boxed_car, typed_box(car.type),
             typed_qbe_type(car.type), car.value);
        emit(ctx, "%s =l call $%s(%s %s)", boxed_cdr, typed_box(cdr.type),
             typed_qbe_type(cdr.type), cdr.value);
        const char *pair = qbe_gen_tmp(ctx);
        emit(ctx, "%s =l call $npl_cons(l %s, l %s)", pair,
             boxed_car, boxed_cdr);
        return typed_value(pair, type_pair(car.type, cdr.type));
    }

    if (strcmp(name, "car") == 0 || strcmp(name, "cdr") == 0) {
        if (!args || AST_IS_NIL(args) || !AST_IS_NIL(AST_CDR(args)))
            codegen_error("typed %s expects one argument", name);
        TypedValue pair = gen_typed_expr(ctx, AST_CAR(args), env);
        Type *pair_type = type_resolve(pair.type);
        if (!pair_type || pair_type->kind != TYPE_PAIR)
            codegen_error("typed %s requires a Pair", name);
        Type *element = strcmp(name, "car") == 0
            ? pair_type->as.pair.car : pair_type->as.pair.cdr;
        element = type_resolve(element);
        if (!unboxed_type(element))
            codegen_error("typed %s element is not concrete", name);
        const char *tagged = qbe_gen_tmp(ctx);
        emit(ctx, "%s =l call $npl_%s(l %s)", tagged, name, pair.value);
        const char *result = qbe_gen_tmp(ctx);
        emit(ctx, "%s =%s call $%s(l %s)", result,
             typed_qbe_type(element), typed_guard(element), tagged);
        return typed_value(result, element);
    }

    TypedFunction *callee = ctx->types
        ? type_program_function(ctx->types, name) : NULL;
    if (!unboxed_function(callee))
        codegen_error("typed function cannot call Dynamic function '%s' yet", name);

    int argc = 0;
    char values[16][64];
    for (AstNode *arg = args; arg && !AST_IS_NIL(arg); arg = AST_CDR(arg)) {
        if (argc >= callee->arity)
            codegen_error("typed call to '%s' has too many arguments", name);
        TypedValue value = gen_typed_expr(ctx, AST_CAR(arg), env);
        if (!type_equal(value.type, callee->params[argc]))
            codegen_error("typed call to '%s' has incompatible argument %d",
                          name, argc + 1);
        snprintf(values[argc], sizeof(values[argc]), "%s", value.value);
        argc++;
    }
    if (argc != callee->arity)
        codegen_error("typed call to '%s' expects %d arguments", name,
                      callee->arity);

    char safe_name[128];
    qbe_sanitize_name(name, safe_name, sizeof(safe_name));

    if (tail) {
        if (ctx->current_typed == callee) {
            char frozen[16][64];
            for (int i = 0; i < argc; i++) {
                const char *copy = qbe_gen_tmp(ctx);
                emit(ctx, "%s =%s copy %s", copy,
                     typed_qbe_type(callee->params[i]), values[i]);
                snprintf(frozen[i], sizeof(frozen[i]), "%s", copy);
            }
            AstNode *param = ctx->current_typed_params;
            for (int i = 0; i < argc; i++) {
                Symbol *symbol = symtab_lookup(
                    ctx->symtab, AST_CAR(param)->as.symbol);
                emit(ctx, "%s =%s copy %s", symbol->as.local.qbe_name,
                     typed_qbe_type(callee->params[i]), frozen[i]);
                param = AST_CDR(param);
            }
            emit(ctx, "jmp @typed_tail");
        } else {
            char boxed[16][64];
            for (int i = 0; i < argc; i++) {
                const char *value = qbe_gen_tmp(ctx);
                const char *box = typed_box(callee->params[i]);
                emit(ctx, "%s =l call $%s(%s %s)", value, box,
                     typed_qbe_type(callee->params[i]), values[i]);
                snprintf(boxed[i], sizeof(boxed[i]), "%s", value);
            }
            const char *argv = "0";
            if (argc > 0) {
                argv = qbe_gen_tmp(ctx);
                emit(ctx, "%s =l alloc8 %d", argv, argc * 8);
                for (int i = 0; i < argc; i++) {
                    const char *slot = argv;
                    if (i > 0) {
                        slot = qbe_gen_tmp(ctx);
                        emit(ctx, "%s =l add %s, %d", slot, argv, i * 8);
                    }
                    emit(ctx, "storel %s, %s", boxed[i], slot);
                }
            }
            emit(ctx,
                 "call $npl_tail_request(l $%s__body, l 2, l %d, l %s)",
                 safe_name, argc, argv);
            emit(ctx, "ret %s",
                 callee->result->kind == TYPE_FLOAT ? "d_0" : "0");
        }
        *terminated = 1;
        return typed_value("0", callee->result);
    }

    const char *result = qbe_gen_tmp(ctx);
    if (argc == 0) {
        emit(ctx, "%s =%s call $%s__typed()", result,
             typed_qbe_type(callee->result), safe_name);
    } else {
        emit_raw(ctx, "%s =%s call $%s__typed(%s %s", result,
                 typed_qbe_type(callee->result), safe_name,
                 typed_qbe_type(callee->params[0]), values[0]);
        for (int i = 1; i < argc; i++)
            fprintf(ctx->out, ", %s %s",
                    typed_qbe_type(callee->params[i]), values[i]);
        fprintf(ctx->out, ")\n");
    }
    return typed_value(result, callee->result);
}

static TypedValue gen_typed_lambda(QbeCtx *ctx, AstNode *expr, TypeEnv *env)
{
    TypedLambda *lambda = ctx->types
        ? type_program_lambda(ctx->types, expr) : NULL;
    if (!unboxed_lambda(lambda))
        codegen_error("typed lambda requires concrete Integer/Bool annotations");

    AstNode *params = AST_CAR(expr);
    AstNode *body = AST_CDR(expr);
    AstNode *free_vars = collect_free_vars_seq(body, ctx->symtab, params);
    Type *capture_types[16];
    int capture_count = 0;
    for (AstNode *fv = free_vars; fv && !AST_IS_NIL(fv); fv = AST_CDR(fv)) {
        AstNode *var = AST_CAR(fv);
        Type *type = var && var->tag == AST_SYMBOL
            ? type_env_lookup(env, var->as.symbol) : NULL;
        if (!unboxed_type(type) || capture_count >= 16)
            codegen_error("typed lambda captures an unsupported value");
        capture_types[capture_count++] = type;
    }

    static int typed_lambda_count = 0;
    char name[64];
    snprintf(name, sizeof(name), "_typed_lambda_%d", typed_lambda_count++);

    FILE *saved_out = ctx->out;
    FILE *definition = tmpfile();
    if (!definition) codegen_error("cannot create typed lambda buffer");
    int saved_depth = ctx->depth;
    int saved_symtab_count = ctx->symtab->count;
    int saved_next_local = ctx->symtab->next_local;
    char saved_if_slot_base[32];
    int saved_next_if_slot = ctx->next_if_slot;
    TypedFunction *saved_typed = ctx->current_typed;
    AstNode *saved_typed_params = ctx->current_typed_params;
    snprintf(saved_if_slot_base, sizeof(saved_if_slot_base), "%s",
             ctx->if_slot_base);
    ctx->out = definition;
    ctx->depth = 1;

    /* Dynamic-facing generic closure body. */
    fprintf(ctx->out,
            "function l $%s(l %%npl_env, l %%npl_argc, l %%npl_argv) {\n",
            name);
    fprintf(ctx->out, "@start\n");
    char raw_args[16][64];
    AstNode *param = params;
    for (int i = 0; i < lambda->arity; i++) {
        const char *slot = "%npl_argv";
        if (i > 0) {
            slot = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l add %%npl_argv, %d", slot, i * 8);
        }
        const char *tagged = qbe_gen_tmp(ctx);
        emit(ctx, "%s =l loadl %s", tagged, slot);
        const char *raw = qbe_gen_tmp(ctx);
        const char *guard = lambda->params[i]->kind == TYPE_BOOL
            ? "npl_guard_bool" : "npl_guard_integer";
        emit(ctx, "%s =w call $%s(l %s)", raw, guard, tagged);
        snprintf(raw_args[i], sizeof(raw_args[i]), "%s", raw);
        param = AST_CDR(param);
    }
    const char *raw_result = qbe_gen_tmp(ctx);
    if (lambda->arity == 0) {
        emit(ctx, "%s =w call $%s__typed(l %%npl_env)", raw_result, name);
    } else {
        emit_raw(ctx, "%s =w call $%s__typed(l %%npl_env, w %s",
                 raw_result, name, raw_args[0]);
        for (int i = 1; i < lambda->arity; i++)
            fprintf(ctx->out, ", w %s", raw_args[i]);
        fprintf(ctx->out, ")\n");
    }
    const char *boxed_result = qbe_gen_tmp(ctx);
    const char *box_result = lambda->result->kind == TYPE_BOOL
        ? "npl_box_bool" : "npl_box_integer";
    emit(ctx, "%s =l call $%s(w %s)", boxed_result, box_result, raw_result);
    emit(ctx, "ret %s", boxed_result);
    fprintf(ctx->out, "}\n\n");

    /* Unboxed implementation. */
    fprintf(ctx->out, "function w $%s__typed(l %%typed_env", name);
    param = params;
    for (int i = 0; i < lambda->arity; i++) {
        fprintf(ctx->out, ", w %s", param_qbe_name(AST_CAR(param)));
        param = AST_CDR(param);
    }
    fprintf(ctx->out, ") {\n@start\n");
    prepare_if_slots(ctx, body, ctx->out);

    TypeEnv lambda_env;
    type_env_init(&lambda_env);
    param = params;
    for (int i = 0; i < lambda->arity; i++) {
        AstNode *var = AST_CAR(param);
        type_env_bind(&lambda_env, var->as.symbol, lambda->params[i]);
        symtab_add_local(ctx->symtab, var->as.symbol);
        param = AST_CDR(param);
    }
    AstNode *fv = free_vars;
    for (int i = 0; i < capture_count; i++) {
        AstNode *var = AST_CAR(fv);
        const char *tagged = qbe_gen_tmp(ctx);
        emit(ctx, "%s =l call $npl_environment_ref(l %%typed_env, l %d)",
             tagged, i);
        const char *raw = qbe_gen_tmp(ctx);
        const char *guard = capture_types[i]->kind == TYPE_BOOL
            ? "npl_guard_bool" : "npl_guard_integer";
        emit(ctx, "%s =w call $%s(l %s)", raw, guard, tagged);
        Symbol *symbol = symtab_add_local(ctx->symtab, var->as.symbol);
        emit(ctx, "%s =w copy %s", symbol->as.local.qbe_name, raw);
        type_env_bind(&lambda_env, var->as.symbol, capture_types[i]);
        fv = AST_CDR(fv);
    }
    fprintf(ctx->out, "    jmp @typed_tail\n@typed_tail\n");
    ctx->current_typed = NULL;
    ctx->current_typed_params = params;
    TypedValue result = typed_value("0", type_void());
    int terminated = 0;
    for (AstNode *item = body; item && !AST_IS_NIL(item);
         item = AST_CDR(item)) {
        int is_last = AST_IS_NIL(AST_CDR(item));
        result = gen_typed_expr_at(ctx, AST_CAR(item), &lambda_env,
                                   is_last, &terminated);
        if (terminated) break;
    }
    if (!type_equal(result.type, lambda->result))
        codegen_error("typed lambda result mismatch");
    if (!terminated) emit(ctx, "ret %s", result.value);
    fprintf(ctx->out, "}\n\n");

    fflush(definition);
    rewind(definition);
    {
        char line[4096];
        while (fgets(line, sizeof(line), definition))
            fputs(line, ctx->lambda_out);
    }
    fclose(definition);
    ctx->out = saved_out;
    ctx->depth = saved_depth;
    ctx->symtab->count = saved_symtab_count;
    ctx->symtab->next_local = saved_next_local;
    snprintf(ctx->if_slot_base, sizeof(ctx->if_slot_base), "%s",
             saved_if_slot_base);
    ctx->next_if_slot = saved_next_if_slot;
    ctx->current_typed = saved_typed;
    ctx->current_typed_params = saved_typed_params;

    /* Box captured raw values into a managed environment. */
    const char *captures = "0";
    if (capture_count > 0) {
        captures = qbe_gen_tmp(ctx);
        emit(ctx, "%s =l alloc8 %d", captures, capture_count * 8);
        fv = free_vars;
        for (int i = 0; i < capture_count; i++) {
            AstNode *var = AST_CAR(fv);
            Symbol *symbol = symtab_lookup(ctx->symtab, var->as.symbol);
            if (!symbol || symbol->kind != SYM_LOCAL)
                codegen_error("missing typed capture '%s'", var->as.symbol);
            const char *boxed = qbe_gen_tmp(ctx);
            const char *box = capture_types[i]->kind == TYPE_BOOL
                ? "npl_box_bool" : "npl_box_integer";
            emit(ctx, "%s =l call $%s(w %s)", boxed, box,
                 symbol->as.local.qbe_name);
            const char *slot = captures;
            if (i > 0) {
                slot = qbe_gen_tmp(ctx);
                emit(ctx, "%s =l add %s, %d", slot, captures, i * 8);
            }
            emit(ctx, "storel %s, %s", boxed, slot);
            fv = AST_CDR(fv);
        }
    }
    const char *environment = qbe_gen_tmp(ctx);
    emit(ctx, "%s =l call $npl_make_environment(l %d, l %s)",
         environment, capture_count, captures);
    const char *closure = qbe_gen_tmp(ctx);
    emit(ctx, "%s =l call $npl_make_closure(l $%s, l %s, l %d)",
         closure, name, environment, lambda->arity);
    return typed_value(closure, typed_lambda_function_type(lambda));
}

static TypedValue gen_typed_expr_at(QbeCtx *ctx, AstNode *expr,
                                    TypeEnv *env, int tail,
                                    int *terminated)
{
    *terminated = 0;
    if (!expr) codegen_error("missing typed expression");
    switch (expr->tag) {
    case AST_FIXNUM: {
        char *literal = tmp_pool[tmp_idx % MAX_TMP];
        tmp_idx++;
        snprintf(literal, 32, "%lld", (long long)expr->as.fixnum);
        return typed_value(literal, type_integer());
    }
    case AST_FLONUM: {
        char *literal = tmp_pool[tmp_idx % MAX_TMP];
        tmp_idx++;
        snprintf(literal, 32, "d_%a", expr->as.flonum);
        return typed_value(literal, type_float());
    }
    case AST_BOOL:
        return typed_value(expr->as.bool_val ? "1" : "0", type_bool());
    case AST_SYMBOL: {
        Type *type = type_env_lookup(env, expr->as.symbol);
        Symbol *symbol = symtab_lookup(ctx->symtab, expr->as.symbol);
        Type *resolved = type_resolve(type);
        if ((!unboxed_type(resolved) &&
             (!resolved || resolved->kind != TYPE_FUNC)) ||
            !symbol || symbol->kind != SYM_LOCAL)
            codegen_error("typed code references Dynamic value '%s'",
                          expr->as.symbol);
        return typed_value(symbol->as.local.qbe_name, type);
    }
    case AST_PAIR:
        return gen_typed_call(ctx, expr, env, tail, terminated);
    case AST_IF: {
        TypedValue test = gen_typed_expr(ctx, AST_CAR(expr), env);
        if (!type_equal(test.type, type_bool()))
            codegen_error("typed if requires a Bool test");
        char slot_buf[32];
        next_if_slot(ctx, slot_buf, sizeof(slot_buf));
        const char *ltrue = qbe_gen_label(ctx);
        const char *lfalse = qbe_gen_label(ctx);
        const char *ldone = qbe_gen_label(ctx);
        emit(ctx, "jnz %s, %s, %s", test.value, ltrue, lfalse);

        fprintf(ctx->out, "%s\n", ltrue);
        int yes_terminated = 0;
        TypedValue yes = gen_typed_expr_at(
            ctx, AST_CAR(AST_CDR(expr)), env, tail, &yes_terminated);
        if (!yes_terminated) {
            emit(ctx, "%s %s, %s", typed_store(yes.type), yes.value,
                 slot_buf);
            emit(ctx, "jmp %s", ldone);
        }

        fprintf(ctx->out, "%s\n", lfalse);
        int no_terminated = 0;
        TypedValue no = gen_typed_expr_at(
            ctx, AST_CAR(AST_CDR(AST_CDR(expr))), env, tail,
            &no_terminated);
        if (!no_terminated) {
            emit(ctx, "%s %s, %s", typed_store(no.type), no.value,
                 slot_buf);
            emit(ctx, "jmp %s", ldone);
        }

        if (yes_terminated && no_terminated) {
            *terminated = 1;
            return typed_value("0", ctx->current_typed->result);
        }
        Type *result_type = yes_terminated ? no.type : yes.type;
        if (!yes_terminated && !no_terminated &&
            !type_equal(yes.type, no.type))
            codegen_error("typed if branches have different types");
        fprintf(ctx->out, "%s\n", ldone);
        const char *result = qbe_gen_tmp(ctx);
        emit(ctx, "%s =%s %s %s", result, typed_qbe_type(result_type),
             typed_load(result_type), slot_buf);
        return typed_value(result, result_type);
    }
    case AST_LET: {
        int saved = env->count;
        int saved_symtab_count = ctx->symtab->count;
        int saved_next_local = ctx->symtab->next_local;
        for (AstNode *bindings = AST_CAR(expr);
             bindings && !AST_IS_NIL(bindings); bindings = AST_CDR(bindings)) {
            AstNode *binding = AST_CAR(bindings);
            AstNode *name = AST_CAR(binding);
            TypedValue value = gen_typed_expr(
                ctx, AST_CAR(AST_CDR(binding)), env);
            if (!name || name->tag != AST_SYMBOL)
                codegen_error("malformed typed let binding");
            Symbol *symbol = symtab_add_local(ctx->symtab, name->as.symbol);
            emit(ctx, "%s =%s copy %s", symbol->as.local.qbe_name,
                 typed_qbe_type(value.type), value.value);
            type_env_bind_generalized(env, name->as.symbol, value.type);
        }
        TypedValue result = typed_value("0", type_void());
        for (AstNode *body = AST_CDR(expr); body && !AST_IS_NIL(body);
             body = AST_CDR(body)) {
            int is_last = AST_IS_NIL(AST_CDR(body));
            result = gen_typed_expr_at(ctx, AST_CAR(body), env,
                                       tail && is_last, terminated);
            if (*terminated) break;
        }
        env->count = saved;
        ctx->symtab->count = saved_symtab_count;
        ctx->symtab->next_local = saved_next_local;
        return result;
    }
    case AST_BEGIN: {
        TypedValue result = typed_value("0", type_void());
        for (AstNode *body = AST_CAR(expr); body && !AST_IS_NIL(body);
             body = AST_CDR(body)) {
            int is_last = AST_IS_NIL(AST_CDR(body));
            result = gen_typed_expr_at(ctx, AST_CAR(body), env,
                                       tail && is_last, terminated);
            if (*terminated) break;
        }
        return result;
    }
    case AST_LAMBDA: {
        TypedLambda *lambda = ctx->types
            ? type_program_lambda(ctx->types, expr) : NULL;
        if (!lambda) codegen_error("missing inferred lambda type");
        if (unboxed_lambda(lambda)) return gen_typed_lambda(ctx, expr, env);
        const char *closure = gen_expr(ctx, expr, 1);
        return typed_value(closure, typed_lambda_function_type(lambda));
    }
    default:
        codegen_error("construct is not supported in unboxed typed code");
    }
    return typed_value("0", type_dynamic());
}

static void gen_typed_function(QbeCtx *ctx, TypedFunction *function,
                               AstNode *params, AstNode *body, FILE *out)
{
    char safe_name[128];
    qbe_sanitize_name(function->name, safe_name, sizeof(safe_name));
    fprintf(out, "function %s $%s__typed(",
            typed_qbe_type(function->result), safe_name);
    AstNode *param = params;
    for (int i = 0; i < function->arity; i++) {
        if (i > 0) fprintf(out, ", ");
        fprintf(out, "%s %s", typed_qbe_type(function->params[i]),
                param_qbe_name(AST_CAR(param)));
        param = AST_CDR(param);
    }
    fprintf(out, ") {\n@start\n");

    char saved_if_slot_base[32];
    int saved_next_if_slot = ctx->next_if_slot;
    snprintf(saved_if_slot_base, sizeof(saved_if_slot_base), "%s",
             ctx->if_slot_base);
    prepare_if_slots(ctx, body, out);

    TypeEnv env;
    type_env_init(&env);
    param = params;
    for (int i = 0; i < function->arity; i++) {
        AstNode *name = AST_CAR(param);
        type_env_bind(&env, name->as.symbol, function->params[i]);
        symtab_add_local(ctx->symtab, name->as.symbol);
        param = AST_CDR(param);
    }
    fprintf(out, "    jmp @typed_tail\n@typed_tail\n");
    TypedFunction *saved_typed = ctx->current_typed;
    AstNode *saved_typed_params = ctx->current_typed_params;
    ctx->current_typed = function;
    ctx->current_typed_params = params;

    TypedValue result = typed_value("0", type_void());
    int terminated = 0;
    for (AstNode *item = body; item && !AST_IS_NIL(item);
         item = AST_CDR(item)) {
        int is_last = AST_IS_NIL(AST_CDR(item));
        result = gen_typed_expr_at(ctx, AST_CAR(item), &env,
                                   is_last, &terminated);
        if (terminated) break;
    }
    if (!type_equal(result.type, function->result))
        codegen_error("typed codegen result mismatch in '%s'", function->name);
    if (!terminated) emit(ctx, "ret %s", result.value);
    fprintf(out, "}\n\n");
    ctx->current_typed = saved_typed;
    ctx->current_typed_params = saved_typed_params;

    snprintf(ctx->if_slot_base, sizeof(ctx->if_slot_base), "%s",
             saved_if_slot_base);
    ctx->next_if_slot = saved_next_if_slot;
}

/* Generate a fixed-ABI wrapper plus a generic trampoline body. */
static void gen_function_to(QbeCtx *ctx, const char *name, AstNode *params,
                            AstNode *body, FILE *out)
{
    char safe_name[128];
    char saved_func[128];
    char saved_if_slot_base[32];
    AstNode *saved_params = ctx->current_params;
    int saved_symtab_count = ctx->symtab->count;
    int saved_next_local = ctx->symtab->next_local;
    int saved_next_if_slot = ctx->next_if_slot;
    int saved_depth = ctx->depth;
    int saved_tail = ctx->in_tail;
    int saved_trampoline_body = ctx->trampoline_body;
    snprintf(saved_func, sizeof(saved_func), "%s", ctx->current_func);
    snprintf(saved_if_slot_base, sizeof(saved_if_slot_base), "%s",
             ctx->if_slot_base);
    qbe_sanitize_name(name, safe_name, sizeof(safe_name));
    TypedFunction *typed = ctx->types
        ? type_program_function(ctx->types, name) : NULL;
    int use_unboxed = unboxed_function(typed);
    snprintf(ctx->current_func, sizeof(ctx->current_func), "%s", safe_name);
    ctx->current_params = strcmp(name, "main") == 0 ? NULL : params;
    ctx->expr_terminated = 0;

    int param_count = 0;
    for (AstNode *p = params; p && !AST_IS_NIL(p); p = AST_CDR(p))
        param_count++;
    if (param_count > 16)
        codegen_error("function '%s' supports at most 16 arguments", name);

    /* Public wrappers retain the source-level fixed arity ABI. */
    if (strcmp(name, "main") == 0) {
        fprintf(out, "export function l $main(l %%npl_argc, l %%npl_argv) {\n");
        fprintf(out, "@start\n");
        fprintf(out, "    call $npl_runtime_init()\n");
        fprintf(out, "    call $npl_store_args(l %%npl_argc, l %%npl_argv)\n");
        if (ctx->has_init) fprintf(out, "    call $npl_global_inits()\n");
        const char *result = qbe_gen_tmp(ctx);
        fprintf(out,
                "    %s =l call $npl_trampoline(l $main__body, l 2, l 0, l 0)\n",
                result);
        fprintf(out, "    ret %s\n}\n\n", result);
    } else {
        fprintf(out, "function l $%s(", safe_name);
        AstNode *p = params;
        int first = 1;
        while (p && !AST_IS_NIL(p)) {
            AstNode *param = AST_CAR(p);
            if (!first) fprintf(out, ", ");
            fprintf(out, "l %s", param_qbe_name(param));
            first = 0;
            p = AST_CDR(p);
        }
        fprintf(out, ") {\n@start\n");

        const char *argv = "0";
        if (param_count > 0) {
            argv = qbe_gen_tmp(ctx);
            fprintf(out, "    %s =l alloc8 %d\n", argv, param_count * 8);
            p = params;
            int index = 0;
            while (p && !AST_IS_NIL(p)) {
                AstNode *param = AST_CAR(p);
                if (index == 0) {
                    fprintf(out, "    storel %s, %s\n",
                            param_qbe_name(param), argv);
                } else {
                    const char *slot = qbe_gen_tmp(ctx);
                    fprintf(out, "    %s =l add %s, %d\n", slot, argv, index * 8);
                    fprintf(out, "    storel %s, %s\n",
                            param_qbe_name(param), slot);
                }
                index++;
                p = AST_CDR(p);
            }
        }
        const char *result = qbe_gen_tmp(ctx);
        fprintf(out,
                "    %s =l call $npl_trampoline(l $%s__body, l 2, l %d, l %s)\n",
                result, safe_name, param_count, argv);
        fprintf(out, "    ret %s\n}\n\n", result);
    }

    if (use_unboxed)
        gen_typed_function(ctx, typed, params, body, out);

    /* Generic bodies are the only functions entered by the dispatcher. */
    fprintf(out,
            "function l $%s__body(l %%npl_env, l %%npl_argc, l %%npl_argv) {\n",
            safe_name);
    fprintf(out, "@start\n");
    if (!use_unboxed) prepare_if_slots(ctx, body, out);

    if (strcmp(name, "main") != 0) {
        AstNode *p = params;
        int index = 0;
        while (p && !AST_IS_NIL(p)) {
            AstNode *param = AST_CAR(p);
            if (!param || param->tag != AST_SYMBOL)
                codegen_error("malformed parameter in '%s'", name);
            symtab_add_local(ctx->symtab, param->as.symbol);
            const char *address = "%npl_argv";
            if (index > 0) {
                address = qbe_gen_tmp(ctx);
                fprintf(out, "    %s =l add %%npl_argv, %d\n",
                        address, index * 8);
            }
            fprintf(out, "    %s =l loadl %s\n",
                    param_qbe_name(param), address);
            index++;
            p = AST_CDR(p);
        }
    }

    if (use_unboxed) {
        char raw_args[16][64];
        AstNode *p = params;
        for (int i = 0; i < typed->arity; i++) {
            const char *raw = qbe_gen_tmp(ctx);
            const char *guard = typed_guard(typed->params[i]);
            emit(ctx, "%s =%s call $%s(l %s)", raw,
                 typed_qbe_type(typed->params[i]), guard,
                 param_qbe_name(AST_CAR(p)));
            snprintf(raw_args[i], sizeof(raw_args[i]), "%s", raw);
            p = AST_CDR(p);
        }
        const char *raw_result = qbe_gen_tmp(ctx);
        if (typed->arity == 0) {
            emit(ctx, "%s =%s call $%s__typed()", raw_result,
                 typed_qbe_type(typed->result), safe_name);
        } else {
            emit_raw(ctx, "%s =%s call $%s__typed(%s %s", raw_result,
                     typed_qbe_type(typed->result), safe_name,
                     typed_qbe_type(typed->params[0]), raw_args[0]);
            for (int i = 1; i < typed->arity; i++)
                fprintf(ctx->out, ", %s %s",
                        typed_qbe_type(typed->params[i]), raw_args[i]);
            fprintf(ctx->out, ")\n");
        }
        const char *boxed = qbe_gen_tmp(ctx);
        const char *box = typed_box(typed->result);
        emit(ctx, "%s =l call $%s(%s %s)", boxed, box,
             typed_qbe_type(typed->result), raw_result);
        emit(ctx, "ret %s", boxed);
    } else {
        fprintf(out, "    jmp @tail\n@tail\n");
        ctx->depth = 1;
        ctx->trampoline_body = 1;
        const char *result = gen_body_at(ctx, body, 1, 1);
        if (!ctx->expr_terminated) emit(ctx, "ret %s", result ? result : "2");
    }
    fprintf(out, "}\n\n");

    ctx->depth = saved_depth;
    ctx->in_tail = saved_tail;
    ctx->expr_terminated = 0;
    ctx->trampoline_body = saved_trampoline_body;
    ctx->current_params = saved_params;
    ctx->symtab->count = saved_symtab_count;
    ctx->symtab->next_local = saved_next_local;
    snprintf(ctx->if_slot_base, sizeof(ctx->if_slot_base), "%s",
             saved_if_slot_base);
    ctx->next_if_slot = saved_next_if_slot;
    snprintf(ctx->current_func, sizeof(ctx->current_func), "%s", saved_func);
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
                            fprintf(ctx->init_out,
                                    "    call $npl_gc_root(l $gv_%s)\n",
                                    safe_vn);
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
        fprintf(ctx->out, "    call $npl_runtime_init()\n");
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
