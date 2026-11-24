#ifndef NPL_AST_H
#define NPL_AST_H

#include <stdint.h>
#include <stddef.h>

/*
 * NPL AST node representation.
 *
 * Uses a tagged union (C99).  Every node is heap-allocated via ast_alloc()
 * and freed in bulk via ast_free_all().  No per-node free to keep things
 * simple for stage0.
 *
 * Node types mirror the Scheme subset we need for bootstrapping:
 *   - atoms:  symbol, fixnum, flonum, string, boolean, nil
 *   - compound: pair (cons cell = list backbone)
 *   - special forms: if, define, lambda, let, begin, quote, set!
 */

/* ---- tag enum ---- */

typedef enum {
    AST_NIL,        /* '() – the empty list */
    AST_BOOL,       /* #t / #f */
    AST_FIXNUM,     /* machine-width integer (int64_t) */
    AST_FLONUM,     /* double-precision float */
    AST_SYMBOL,     /* interned symbol (pointer into symbol table) */
    AST_STRING,     /* string literal */
    AST_PAIR,       /* (car . cdr) */
    /* special forms – represented as tagged pairs for simplicity */
    AST_IF,         /* (if test consequent alternate) */
    AST_DEFINE,     /* (define name value) or (define (name args...) body) */
    AST_LAMBDA,     /* (lambda (params...) body) */
    AST_LET,        /* (let ((name val) ...) body) */
    AST_BEGIN,      /* (begin exprs...) */
    AST_QUOTE,      /* (quote datum) */
    AST_SET,        /* (set! name value) */
} AstTag;

/* ---- forward decl ---- */
typedef struct AstNode AstNode;

/* ---- the node ---- */

struct AstNode {
    AstTag tag;
    union {
        /* AST_BOOL */    int      bool_val;
        /* AST_FIXNUM */   int64_t  fixnum;
        /* AST_FLONUM */   double   flonum;
        /* AST_SYMBOL */   const char *symbol;   /* interned, never freed individually */
        /* AST_STRING */   char    *string;      /* owned copy */
        /* AST_PAIR, AST_IF, AST_DEFINE, AST_LAMBDA, AST_LET, AST_BEGIN, AST_QUOTE, AST_SET */
        struct {
            AstNode *car;
            AstNode *cdr;
        } pair;
    } as;
};

/* ---- constructors (all return heap-allocated nodes) ---- */

AstNode *ast_nil(void);
AstNode *ast_bool(int val);
AstNode *ast_fixnum(int64_t val);
AstNode *ast_flonum(double val);
AstNode *ast_symbol(const char *name);   /* interns the name */
AstNode *ast_string(char *s);            /* takes ownership */
AstNode *ast_pair(AstNode *car, AstNode *cdr);
AstNode *ast_if(AstNode *test, AstNode *conseq, AstNode *alt);
AstNode *ast_define(AstNode *name_or_sig, AstNode *body);
AstNode *ast_lambda(AstNode *params, AstNode *body);
AstNode *ast_let(AstNode *bindings, AstNode *body);
AstNode *ast_begin(AstNode *exprs);
AstNode *ast_quote(AstNode *datum);
AstNode *ast_set(AstNode *name, AstNode *value);

/* ---- symbol table ---- */

/* Intern a string; returns the canonical pointer. */
const char *sym_intern(const char *name);

/* ---- memory management ---- */

/* Bulk-free all nodes allocated since the last ast_free_all(). */
void ast_free_all(void);

/* ---- pretty printer ---- */

/* Print an AST node as an S-expression to stdout. */
void ast_print(const AstNode *node);

/* ---- helper macros ---- */

#define AST_IS_NIL(n)      ((n)->tag == AST_NIL)
#define AST_IS_PAIR(n)     ((n)->tag == AST_PAIR || \
                            (n)->tag == AST_IF || \
                            (n)->tag == AST_DEFINE || \
                            (n)->tag == AST_LAMBDA || \
                            (n)->tag == AST_LET || \
                            (n)->tag == AST_BEGIN || \
                            (n)->tag == AST_QUOTE || \
                            (n)->tag == AST_SET)

#define AST_CAR(n)         ((n)->as.pair.car)
#define AST_CDR(n)         ((n)->as.pair.cdr)

#endif /* NPL_AST_H */
