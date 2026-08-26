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
static const char *gen_expr(QbeCtx *ctx, AstNode *expr, int depth);

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
    ctx->symtab = symtab;
    ctx->next_tmp = 0;
    ctx->next_label = 0;
    ctx->depth = 0;
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

/* Generate QBE IL for a literal expression (number, string, boolean) */
static const char *gen_literal(QbeCtx *ctx, AstNode *expr)
{
    static char buf[64];
    switch (expr->tag) {
    case AST_FIXNUM:
        /* Fixnum: value << 2 | TAG_FIXNUM (which is 0) */
        snprintf(buf, sizeof(buf), "%lld", (long long)(expr->as.fixnum << 2));
        return buf;
    case AST_BOOL:
        /* Boolean: immediate value */
        return expr->as.bool_val ? "4" : "8";  /* IMM_TRUE << 2 | TAG_IMM, IMM_FALSE << 2 | TAG_IMM */
    case AST_NIL:
        /* Nil: 0 */
        return "0";
    default:
        return "0";
    }
}

/* Forward declaration */
static const char *gen_expr(QbeCtx *ctx, AstNode *expr, int depth);

/* Generate QBE IL for an expression */
static const char *gen_expr(QbeCtx *ctx, AstNode *expr, int depth)
{
    if (!expr) return "0";

    switch (expr->tag) {
    case AST_FIXNUM:
    case AST_BOOL:
        return gen_literal(ctx, expr);

    case AST_SYMBOL: {
        /* Look up in symbol table */
        Symbol *sym = symtab_lookup(ctx->symtab, expr->as.symbol);
        if (sym && sym->kind == SYM_LOCAL) {
            return sym->as.local.qbe_name;
        }
        /* Global — for now return 0 (functions are called directly) */
        return "0";
    }

    case AST_STRING: {
        /* String constants — Phase 2: return 0 (not yet implemented) */
        return "0";
    }

    case AST_IF: {
        /* (if test consequent alternate) */
        AstNode *test   = AST_CAR(expr);
        AstNode *conseq = AST_CAR(AST_CDR(expr));
        AstNode *alt    = AST_CAR(AST_CDR(AST_CDR(expr)));

        const char *result = qbe_gen_tmp(ctx);
        const char *ltrue  = qbe_gen_label(ctx);
        const char *lfalse = qbe_gen_label(ctx);
        const char *ldone  = qbe_gen_label(ctx);

        /* Generate test condition */
        const char *test_val = gen_expr(ctx, test, depth + 1);

        /* Branch on test */
        emit(ctx, "jnz %s, %s, %s", test_val, ltrue, lfalse);

        /* True branch */
        fprintf(ctx->out, "%s\n", ltrue);
        const char *true_val = gen_expr(ctx, conseq, depth + 1);
        emit(ctx, "%s =l copy %s", result, true_val);
        emit(ctx, "jmp %s", ldone);

        /* False branch */
        fprintf(ctx->out, "%s\n", lfalse);
        const char *false_val = gen_expr(ctx, alt, depth + 1);
        emit(ctx, "%s =l copy %s", result, false_val);
        emit(ctx, "jmp %s", ldone);

        /* Merge */
        fprintf(ctx->out, "%s\n", ldone);
        return result;
    }

    case AST_PAIR: {
        /* Function application: (f arg1 arg2 ...) */
        AstNode *func = AST_CAR(expr);
        AstNode *args = AST_CDR(expr);

        if (func->tag != AST_SYMBOL) {
            /* Lambda application — not supported in Phase 2 */
            return "0";
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
            /* Comparison: (< a b) */
            AstNode *arg1 = AST_CAR(args);
            AstNode *arg2 = AST_CAR(AST_CDR(args));

            const char *v1 = gen_expr(ctx, arg1, depth + 1);
            const char *v2 = gen_expr(ctx, arg2, depth + 1);
            const char *result = qbe_gen_tmp(ctx);

            const char *op = qbe_cmp_op(fname);
            /* For comparison: extract values and compare */
            const char *tmp1 = qbe_gen_tmp(ctx);
            const char *tmp2 = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l sar %s, 2", tmp1, v1);  /* extract v1 */
            emit(ctx, "%s =l sar %s, 2", tmp2, v2);  /* extract v2 */
            emit(ctx, "%s =l %s %s, %s", result, op, tmp1, tmp2);
            return result;
        }

        /* Check for display/puts */
        if (strcmp(fname, "display") == 0 || strcmp(fname, "puts") == 0) {
            AstNode *arg = AST_CAR(args);
            if (arg->tag == AST_STRING) {
                /* Emit string data and call puts */
                const char *data_name = qbe_gen_tmp(ctx);
                /* For now, emit inline data reference */
                /* We'll handle this in the string section */
                const char *result = qbe_gen_tmp(ctx);
                emit(ctx, "%s =l call $puts(l %s)", result, data_name);
                return result;
            }
            const char *val = gen_expr(ctx, arg, depth + 1);
            const char *result = qbe_gen_tmp(ctx);
            emit(ctx, "%s =l call $puts(l %s)", result, val);
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

        /* Check for null? (check if nil) */
        if (strcmp(fname, "null?") == 0) {
            AstNode *arg = AST_CAR(args);
            const char *val = gen_expr(ctx, arg, depth + 1);
            const char *result = qbe_gen_tmp(ctx);
            /* nil is 0 in our representation */
            emit(ctx, "%s =l ceqw %s, 0", result, val);
            return result;
        }

        /* User-defined function call */
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
            if (arg_count == 0) {
                emit(ctx, "%s =l call $%s()", result, safe_fname);
            } else if (arg_count == 1) {
                emit(ctx, "%s =l call $%s(l %s)", result, safe_fname, arg_bufs[0]);
            } else if (arg_count == 2) {
                emit(ctx, "%s =l call $%s(l %s, l %s)", result, safe_fname, arg_bufs[0], arg_bufs[1]);
            } else {
                /* More args — build call string */
                emit_raw(ctx, "%s =l call $%s(l %s", result, safe_fname, arg_bufs[0]);
                for (int i = 1; i < arg_count; i++) {
                    fprintf(ctx->out, ", l %s", arg_bufs[i]);
                }
                fprintf(ctx->out, ")\n");
            }
        }
        return result;
    }

    case AST_DEFINE: {
        /* (define name value) or (define (name params...) body) */
        AstNode *name_or_sig = AST_CAR(expr);
        AstNode *body = AST_CDR(expr);

        if (name_or_sig->tag == AST_SYMBOL) {
            /* Simple variable: (define x value) */
            /* For Phase 2, we'll just evaluate and ignore */
            /* Later this will store to a global */
            return gen_expr(ctx, body, depth + 1);
        } else if (name_or_sig->tag == AST_PAIR) {
            /* Function: (define (name params...) body) */
            AstNode *name_node = AST_CAR(name_or_sig);
            AstNode *params = AST_CDR(name_or_sig);

            if (name_node->tag == AST_SYMBOL) {
                /* Generate function definition */
                gen_function(ctx, name_node->as.symbol, params, body);
                return NULL;
            }
        }
        return NULL;
    }

    case AST_LAMBDA: {
        /* Lambda expression — for Phase 2, generate an anonymous function */
        /* This is complex; for now return 0 */
        return "0";
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

        /* Generate body */
        if (body && !AST_IS_NIL(body)) {
            /* Process all body expressions, return the last one */
            const char *result = "0";
            while (body && !AST_IS_NIL(body)) {
                result = gen_expr(ctx, AST_CAR(body), depth + 1);
                body = AST_CDR(body);
            }
            return result;
        }
        return "0";
    }

    case AST_BEGIN: {
        /* (begin exprs...) */
        AstNode *exprs = AST_CAR(expr);
        const char *result = "0";

        while (exprs && !AST_IS_NIL(exprs)) {
            result = gen_expr(ctx, AST_CAR(exprs), depth + 1);
            exprs = AST_CDR(exprs);
        }
        return result;
    }

    case AST_SET:
    case AST_QUOTE:
        /* Not implemented in Phase 2 */
        return "0";

    default:
        return "0";
    }
}

/* Sanitize a name for use as a QBE function/variable name */
static void qbe_emit_name(QbeCtx *ctx, const char *name)
{
    char buf[128];
    qbe_sanitize_name(name, buf, sizeof(buf));
    fprintf(ctx->out, "%s", buf);
}

/* Generate a function definition */
static void gen_function(QbeCtx *ctx, const char *name, AstNode *params, AstNode *body)
{
    /* Generate function header */
    if (strcmp(name, "main") == 0) {
        fprintf(ctx->out, "export ");
    }
    /* Use 'l' (long/64-bit) for all values since we use tagged pointers */
    fprintf(ctx->out, "function l $");
    qbe_emit_name(ctx, name);
    fprintf(ctx->out, "(");

    /* Generate parameters */
    AstNode *p = params;
    int first = 1;
    while (p && !AST_IS_NIL(p)) {
        AstNode *param = AST_CAR(p);
        if (!first) fprintf(ctx->out, ", ");
        fprintf(ctx->out, "l %s", param_qbe_name(param));
        /* Add to symbol table as local */
        if (param->tag == AST_SYMBOL) {
            symtab_add_local(ctx->symtab, param->as.symbol);
        }
        first = 0;
        p = AST_CDR(p);
    }
    fprintf(ctx->out, ") {\n");

    /* Generate body */
    fprintf(ctx->out, "@start\n");
    ctx->depth = 1;

    /* For a begin block or single expression */
    if (body->tag == AST_PAIR && AST_CAR(body) && AST_CAR(body)->tag == AST_SYMBOL &&
        strcmp(AST_CAR(body)->as.symbol, "begin") == 0) {
        /* Multiple expressions in begin block */
        AstNode *exprs = AST_CDR(body);
        const char *last_val = NULL;
        while (exprs && !AST_IS_NIL(exprs)) {
            last_val = gen_expr(ctx, AST_CAR(exprs), 1);
            exprs = AST_CDR(exprs);
        }
        if (last_val) {
            emit(ctx, "ret %s", last_val);
        } else {
            emit(ctx, "ret 0");
        }
    } else {
        /* Single expression */
        const char *result = gen_expr(ctx, body, 1);
        if (result) {
            emit(ctx, "ret %s", result);
        } else {
            emit(ctx, "ret 0");
        }
    }

    fprintf(ctx->out, "}\n\n");
    ctx->depth = 0;
}

/* Main entry point: generate QBE IL for entire program */
void qbe_gen_program(QbeCtx *ctx, AstNode *program)
{
    /* First pass: collect all definitions and emit string data */
    AstNode *cur = program;
    int has_main = 0;

    /* Emit data section for strings */
    /* For Phase 2, we'll use a simple approach: collect strings during codegen */

    /* Emit function definitions */
    cur = program;
    while (cur && !AST_IS_NIL(cur)) {
        AstNode *expr = AST_CAR(cur);

        if (expr->tag == AST_DEFINE) {
            AstNode *name_or_sig = AST_CAR(expr);
            AstNode *body = AST_CDR(expr);

            if (name_or_sig->tag == AST_SYMBOL) {
                /* Global variable — skip for now */
            } else if (name_or_sig->tag == AST_PAIR) {
                AstNode *name_node = AST_CAR(name_or_sig);
                AstNode *params = AST_CDR(name_or_sig);
                if (name_node->tag == AST_SYMBOL) {
                    if (strcmp(name_node->as.symbol, "main") == 0) {
                        has_main = 1;
                    }
                    gen_function(ctx, name_node->as.symbol, params, body);
                }
            }
        }

        cur = AST_CDR(cur);
    }

    /* Generate main if not defined */
    if (!has_main) {
        /* Look for a main function or generate a default one */
        /* For Phase 2, just generate an empty main */
        fprintf(ctx->out, "export function w $main() {\n");
        fprintf(ctx->out, "@start\n");
        fprintf(ctx->out, "    ret 0\n");
        fprintf(ctx->out, "}\n");
    }
}
