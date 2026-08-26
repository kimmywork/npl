/*
 * desugar.c — desugaring pass for core forms (see desugar.h).
 */

#include "desugar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void desugar_error(const char *msg)
{
    fprintf(stderr, "desugar error: %s\n", msg);
    exit(1);
}

/* Build a chain of nested if nodes from cond clauses.
 * clauses: ((test body...) ...) possibly ending with (else body...)
 */
static AstNode *expand_cond_clauses(AstNode *clauses)
{
    if (!clauses || AST_IS_NIL(clauses)) {
        /* (cond) with no clauses — no value; R7RS leaves unspecified.
         * Compile-time error is friendlier than a silent 0. */
        desugar_error("(cond) requires at least one clause");
    }

    AstNode *clause = AST_CAR(clauses);

    if (!clause || !AST_IS_PAIR(clause)) {
        desugar_error("cond: clause must be a pair (test expr...)");
    }

    AstNode *test = AST_CAR(clause);
    AstNode *body = AST_CDR(clause);

    int is_else = 0;
    if (test->tag == AST_SYMBOL && strcmp(test->as.symbol, "else") == 0) {
        is_else = 1;
    }

    /* Validate: else must be the last clause */
    AstNode *rest = AST_CDR(clauses);
    if (is_else && rest && !AST_IS_NIL(rest)) {
        desugar_error("cond: else clause must be last");
    }
    if (!is_else && test->tag != AST_SYMBOL && test->tag != AST_PAIR &&
        test->tag != AST_FIXNUM && test->tag != AST_BOOL) {
        desugar_error("cond: clause head must be an expression");
    }

    /* Body: empty -> the test itself is the value (R7RS).
     * Multiple/any body exprs -> wrap in (begin ...). */
    AstNode *result_expr;
    if (!body || AST_IS_NIL(body)) {
        result_expr = test;
    } else if (AST_IS_NIL(AST_CDR(body))) {
        result_expr = AST_CAR(body);   /* single expr: no begin needed */
    } else {
        result_expr = ast_begin(body);
    }

    if (is_else) {
        return result_expr;
    }

    AstNode *rest_value = expand_cond_clauses(rest);

    return ast_if(test, result_expr, rest_value);
}

/* Rewrite every (cond ...) inside an expression tree.
 * CRITICAL: structured nodes (AST_IF/AST_BEGIN/...) must keep their tags —
 * only real AST_PAIR lists may be rebuilt via ast_pair(). */
AstNode *desugar_expr(AstNode *expr)
{
    if (!expr) return expr;

    switch (expr->tag) {
    case AST_SYMBOL:
    case AST_FIXNUM:
    case AST_FLONUM:
    case AST_STRING:
    case AST_BOOL:
    case AST_NIL:
        return expr;

    case AST_QUOTE:
        /* quoted data must not be rewritten */
        return expr;

    case AST_IF: {
        /* pair layout: car=test, cdr=(conseq alt) */
        expr->as.pair.car = desugar_expr(expr->as.pair.car);
        AstNode *branches = expr->as.pair.cdr;
        while (branches && !AST_IS_NIL(branches)) {
            branches->as.pair.car = desugar_expr(branches->as.pair.car);
            branches = AST_CDR(branches);
        }
        return expr;
    }

    case AST_BEGIN: {
        /* begin: car = exprs list */
        AstNode *b = expr->as.pair.car;
        while (b && !AST_IS_NIL(b)) {
            b->as.pair.car = desugar_expr(b->as.pair.car);
            b = AST_CDR(b);
        }
        return expr;
    }

    case AST_LAMBDA: {
        /* lambda: car=params, cdr=body exprs */
        AstNode *b = expr->as.pair.cdr;
        while (b && !AST_IS_NIL(b)) {
            b->as.pair.car = desugar_expr(b->as.pair.car);
            b = AST_CDR(b);
        }
        return expr;
    }

    case AST_LET: {
        /* let: car=bindings ((name val)...), cdr=body */
        AstNode *binds = expr->as.pair.car;
        while (binds && !AST_IS_NIL(binds)) {
            AstNode *binding = binds->as.pair.car;   /* (name val) */
            if (binding && AST_IS_PAIR(binding)) {
                AstNode *val = binding->as.pair.cdr; /* (val) list */
                while (val && !AST_IS_NIL(val)) {
                    val->as.pair.car = desugar_expr(val->as.pair.car);
                    val = AST_CDR(val);
                }
            }
            binds = AST_CDR(binds);
        }
        AstNode *b = expr->as.pair.cdr;
        while (b && !AST_IS_NIL(b)) {
            b->as.pair.car = desugar_expr(b->as.pair.car);
            b = AST_CDR(b);
        }
        return expr;
    }

    case AST_PAIR: {
        /* Application or special-form-shaped list */
        AstNode *head = AST_CAR(expr);

        if (head && head->tag == AST_SYMBOL &&
            strcmp(head->as.symbol, "quote") == 0) {
            return expr;   /* defensive: unconverted quote keeps its datum */
        }
        if (head && head->tag == AST_SYMBOL &&
            strcmp(head->as.symbol, "cond") == 0) {
            return expand_cond_clauses(AST_CDR(expr));
        }

        /* rewrite head and all argument expressions in place-ish:
         * rebuild the spine only where children changed */
        AstNode *new_head = desugar_expr(head);
        AstNode *acc = NULL;
        AstNode **tail_ptr = &acc;
        int changed = (new_head != head);
        AstNode *cur = AST_CDR(expr);
        while (cur && !AST_IS_NIL(cur)) {
            AstNode *rewritten = desugar_expr(cur->as.pair.car);
            if (rewritten != cur->as.pair.car) changed = 1;
            AstNode *cell = ast_pair(rewritten, ast_nil());
            *tail_ptr = cell;
            tail_ptr = &cell->as.pair.cdr;
            cur = AST_CDR(cur);
        }
        if (!changed) return expr;
        return ast_pair(new_head, acc);
    }

    default:
        return expr;
    }
}

AstNode *desugar_program(AstNode *program)
{
    if (!program || AST_IS_NIL(program)) return program;

    AstNode *cur = program;
    while (cur && !AST_IS_NIL(cur)) {
        AstNode *top = AST_CAR(cur);
        /* Top-level defines: rewrite everything after the name/signature */
        if (top && top->tag == AST_DEFINE) {
            AstNode *name_or_sig = AST_CAR(top);
            AstNode *body = AST_CDR(top);
            if (name_or_sig && name_or_sig->tag == AST_PAIR) {
                /* function define: (define (f params...) body...) */
                AstNode *sig = name_or_sig;
                AstNode *b = body;
                while (b && !AST_IS_NIL(b)) {
                    b->as.pair.car = desugar_expr(b->as.pair.car);
                    b = AST_CDR(b);
                }
                (void)sig;
            } else {
                /* variable define: (define x e) */
                top->as.pair.cdr = desugar_expr(body);
            }
        } else {
            top->as.pair.car = desugar_expr(top);
        }
        cur = AST_CDR(cur);
    }
    return program;
}
