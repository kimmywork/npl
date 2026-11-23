#ifndef NPL_SYMTAB_H
#define NPL_SYMTAB_H

#include "ast.h"
#include <stdint.h>

/*
 * Symbol table for stage0 compiler.
 *
 * Maps symbol names to their definitions:
 *   - Global functions: (define (name params...) body)
 *   - Global variables: (define name value)
 *   - Local variables: (let ((name val) ...) body) and lambda params
 *
 * The symbol table is used during code generation to resolve references
 * and determine how to access values (direct, load from memory, etc.)
 */

typedef enum {
    SYM_GLOBAL_FUNC,    /* top-level function: generates QBE function */
    SYM_GLOBAL_VAR,     /* top-level variable: generates QBE data */
    SYM_LOCAL,          /* local variable (let/lambda param): stack-allocated */
} SymKind;

typedef struct Symbol {
    const char *name;
    SymKind     kind;
    union {
        struct {
            AstNode *params;   /* parameter list */
            AstNode *body;     /* function body */
        } func;
        struct {
            AstNode *value;    /* initial value */
        } var;
        struct {
            int     index;     /* local index for QBE SSA naming */
            char    qbe_name[32]; /* e.g. "%x" or "%v0" */
        } local;
    } as;
} Symbol;

/* Symbol table: simple array for now (sufficient for stage0) */
#define SYMTAB_MAX 1024

typedef struct {
    Symbol entries[SYMTAB_MAX];
    int    count;
    int    next_local;  /* counter for generating unique local names */
} SymTab;

/* Initialize symbol table */
void symtab_init(SymTab *tab);

/* Add a global function definition */
Symbol *symtab_add_global_func(SymTab *tab, const char *name, AstNode *params, AstNode *body);

/* Add a global variable definition */
Symbol *symtab_add_global_var(SymTab *tab, const char *name, AstNode *value);

/* Add a local variable (returns pointer with qbe_name filled in) */
Symbol *symtab_add_local(SymTab *tab, const char *name);

/* Lookup a symbol by name */
Symbol *symtab_lookup(SymTab *tab, const char *name);

/* Sanitize a name for QBE (replace - with _, etc.) */
void qbe_sanitize_name(const char *src, char *dst, size_t dst_size);

#endif /* NPL_SYMTAB_H */
