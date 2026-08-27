#ifndef NPL_QBE_GEN_H
#define NPL_QBE_GEN_H

#include "ast.h"
#include "symtab.h"
#include <stdio.h>

/*
 * QBE IL code generator for stage0 compiler.
 *
 * Generates QBE intermediate language from AST.
 * Handles the minimal subset needed for Phase 2:
 *   - fixnum arithmetic (+, -, *, /)
 *   - comparisons (<, >, =, <=, >=)
 *   - if/cond
 *   - define (simple values and functions)
 *   - lambda (single-param, no closures)
 *   - string constants (via QBE data + puts)
 *   - main entry point generation
 */

/* Output context */
typedef struct {
    FILE   *out;        /* output file */
    FILE   *lambda_out; /* buffer for lambda definitions */
    FILE   *data_out;   /* buffer for static data (strings/symbols/quotes/globals) */
    FILE   *init_out;   /* buffer for the synthesized global-init function */
    int     has_init;
    SymTab *symtab;     /* symbol table */
    int     next_tmp;   /* counter for temporaries */
    int     next_label; /* counter for labels */
    int     depth;      /* nesting depth for indentation */
    int     in_tail;    /* 1 if current position is a tail position */
    char    current_func[128]; /* current function name (for self-recursion detection) */
} QbeCtx;

/* Initialize code generation context */
void qbe_ctx_init(QbeCtx *ctx, FILE *out, SymTab *symtab);

/* Generate QBE IL for an entire program (list of top-level expressions) */
void qbe_gen_program(QbeCtx *ctx, AstNode *program);

/* Generate QBE IL for a single expression, returns the QBE temporary holding the result */
/* Returns NULL for void expressions */
const char *qbe_gen_expr(QbeCtx *ctx, AstNode *expr);

/* Generate a unique temporary name (e.g., "%t0", "%t1") */
const char *qbe_gen_tmp(QbeCtx *ctx);

/* Generate a unique label name (e.g., "@L0", "@L1") */
const char *qbe_gen_label(QbeCtx *ctx);

#endif /* NPL_QBE_GEN_H */
